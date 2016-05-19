/*
* LTest
*
* Copyright (c) 2016 "0of" Magnus
* Licensed under the MIT license.
* https://github.com/0of/LTest/blob/master/LICENSE
*/
#ifndef TEST_SUITE_H
#define TEST_SUITE_H

#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <chrono>
#include <string>
#include <atomic>

#include "declfn.h"

//! apply chrono literals
using namespace std::literals::chrono_literals;

namespace LTest {
  
  class TestRunnableContainer;

  class TestRunable {
  public:
    virtual ~TestRunable() = default;

  public:
    virtual void run(TestRunnableContainer& container) noexcept = 0;
  };

  using SharedTestRunnable = std::shared_ptr<TestRunable>;

  // all the cases run inside the container
  class TestRunnableContainer {
  public:
    virtual ~TestRunnableContainer() = default;

  public:
    virtual void scheduleToRun(const SharedTestRunnable& runnable) = 0;

    // whenever a case begin to run
    virtual void beginRun() = 0;
    virtual void endRun() = 0;

    virtual bool isTimeout(const SharedTestRunnable& ) const = 0;
  };

  class SequentialTestRunnableContainer : public TestRunnableContainer {
  private:
    std::once_flag _called;

    SharedTestRunnable _aboutToRun;
    SharedTestRunnable _running;

    class MonitorThread {
    public:
      static std::unique_ptr<MonitorThread> New() {
        auto thread = std::make_unique<MonitorThread>();
        if (!thread->_activationMutex.try_lock_for(1s)) {
          // timeout
          throw 0;
        }

        return std::move(thread);
      }

    private:
      std::thread _thread;

      std::atomic<bool> _isIdle;
      std::atomic<bool> _needToShutdown;
      std::timed_mutex _timedMutex;
      std::timed_mutex _activationMutex;

    public:
      std::atomic<bool> isTimeout;
    
    public:
      MonitorThread()
        : _thread() {

        _thread = std::move(std::thread {
          [this] {
            // monitor thread has been activated
            _activationMutex.unlock();

            while (!_needToShutdown) {
              if (_isIdle) {
                std::this_thread::yield();
                continue;
              }

              std::unique_lock<decltype(_timedMutex)> timedLock{ _timedMutex, std::defer_lock };
              if (!timedLock.try_lock_for(500ms)) {
                // time out
                isTimeout = true;
                _isIdle = true;
              }
            }
          }
          // end of constructor
        });

        _thread.detach();
      }

      ~MonitorThread() = default;

    public:
      void notifyBeginRun() {
        _timedMutex.lock();
        _isIdle = false;
      }

      void notifyEndRun() {
        _isIdle = true;
        _timedMutex.unlock();
      }
    };

    decltype(MonitorThread::New()) _monitorThread;

  public:
    virtual ~SequentialTestRunnableContainer() = default;

  public:
    virtual void scheduleToRun(const SharedTestRunnable& runnable) override {
      _aboutToRun = runnable;
    }

    virtual void beginRun() override {
      redirectCout();

      _monitorThread->notifyBeginRun();
    }
    virtual void endRun() override {
      restoreCout();

      _monitorThread->notifyEndRun();
      _running = nullptr;
    }

    virtual bool isTimeout(const SharedTestRunnable& /* ignore */) const override {
      return _monitorThread->isTimeout;
    }
  public:
    void start() {
      std::call_once(_called, [this]{
        if (_aboutToRun) {
          // init monitor thread
          _monitorThread = std::move(MonitorThread::New());

          startTheLoop();
        }
      });
    }

  protected:
    virtual void startTheLoop() {
      while (true) {
        if (!_aboutToRun) {
          if (!_running) {
            break;
          } else {
            std::this_thread::yield();
          }
        } else {
          if (!_running) {
            _monitorThread->isTimeout = false;

            // start the runnable
            _running = std::move(_aboutToRun);
            _running->run(*this);
          } else {
            std::this_thread::yield();
          }
        }
      }
    }

    void redirectCout() {
      // TODO:
    }

    void restoreCout() {
      // TODO:
    }
  };

  class CaseEndNotifier {
  public:
    virtual ~CaseEndNotifier() = default;

  public:
    virtual void fail(std::exception_ptr e) noexcept = 0;
    virtual void done() noexcept = 0;
  };

  using SharedCaseEndNotifier = std::shared_ptr<CaseEndNotifier>;

  // the structure of the test cases
  class SequentialTestSpec : public TestRunable {
  private:
    struct TestCase {
      std::string should;
      std::function<void()> verifyBehaviour;

      std::shared_ptr<TestCase> next;

    public:
      template<typename String>
      TestCase(String&& should, std::function<void()>&& behaviour)
        : should{ std::forward<String>(should) }
        , verifyBehaviour{ std::move(behaviour) }
        , next{ nullptr }
      {}
    };

    using SharedTestCase = std::shared_ptr<TestCase>;

    class TestCaseRunnable : public TestRunable {
    private:
      SharedTestCase _testCase;

    public:
      TestCaseRunnable(const SharedTestCase& testCase)
        : _testCase{ testCase } 
      {}

    public:
      virtual void run(TestRunnableContainer& container) noexcept override {
        container.beginRun();
        _testCase->verifyBehaviour();
      }
    };

    class TestCaseLinkedHead : public CaseEndNotifier {
    private:
      TestRunnableContainer *_container;
      SharedTestCase _currentCase;

      std::uint32_t _totalCaseCount { 0 };
      std::uint32_t _succeededCaseCount { 0 };

    public:
      TestCaseLinkedHead()
        : _container{ nullptr }
        , _currentCase{}
      {}

    public:
      virtual void fail(std::exception_ptr e) noexcept override {
        SharedTestCase next = _currentCase->next;

        bool allFinished = true;
        if (next) {
          _container->scheduleToRun(std::make_shared<TestCaseRunnable>(next));
          allFinished = false;
        }

        std::cout << std::endl;
        std::cout << "\x1b[4;22;31m" << "it " << _currentCase->should
                  << "\x1b[22;24;31m" << '\x20' << "\xE2\x9D\x8C" /* cross mark */ << "\x1b[0m" 
                  << std::endl;
        
        _currentCase = next;
      
        if (allFinished) {
          outputWhenAllFinished();
        }

        _container->endRun();
      }

      virtual void done() noexcept override {
        SharedTestCase next = _currentCase->next;

        bool allFinished = true;
        if (next) {
          _container->scheduleToRun(std::make_shared<TestCaseRunnable>(next));
          allFinished = false;
        }

        std::cout << std::endl;
        
        if (_container->isTimeout(nullptr)) {
          std::cout << "\x1b[4;22;33m" << "it " << _currentCase->should
                    << "\x1b[22;24;33m" << '\x20' << "\xE2\x9C\x93" /* check mark */ << "\x20(timeout)" << "\x1b[0m" 
                    << std::endl;
        } else {
          std::cout << "\x1b[4;22;32m" << "it " << _currentCase->should
                    << "\x1b[22;24;32m" << '\x20' << "\xE2\x9C\x93" /* check mark */ << "\x1b[0m" 
                    << std::endl;
        }

        ++_succeededCaseCount;
        _currentCase = next;

        if (allFinished) {
          outputWhenAllFinished();
        }

        _container->endRun();
      }

    public:
      TestCaseLinkedHead& operator = (const SharedTestCase& headTestCase) {
        _currentCase = headTestCase;
        return *this;
      }

    public:
      void letItRun(std::uint32_t totalCount, TestRunnableContainer& container) {
        // hold the container
        _container = &container;
        _totalCaseCount = totalCount;

        if (_currentCase) {
          _container->scheduleToRun(std::make_shared<TestCaseRunnable>(_currentCase));
        }
      }
      bool isRunning() const { return _container; }

    private:
      void outputWhenAllFinished() {
        std::cout << std::endl;
        std::cout << "total:" << "\x1b[1m" << _totalCaseCount << "\x1b[0m"
                    << '\x20' << "pass:" << "\x1b[1;22;32m" << _succeededCaseCount << "\x1b[0m"
                    << '\x20' << "fail:" << "\x1b[1;22;31m" << (_totalCaseCount - _succeededCaseCount) << "\x1b[0m"
                    << std::endl;
      }
    };  

  private:
    std::shared_ptr<TestCaseLinkedHead> _head;
    std::weak_ptr<TestCase> _tail;

    std::uint32_t _totalCaseCount { 0 };

  public:
    SequentialTestSpec()
      : _head{std::make_shared<TestCaseLinkedHead>()}
      , _tail{}
    {}

  public:
    template<typename String, typename VerifyBehaviour>
    SequentialTestSpec& it(String&& should, VerifyBehaviour&& verifyBehaviour) {

      auto verifyBehaviourFn = declfn(verifyBehaviour){ std::move(verifyBehaviour) };

      static_assert(!std::is_same<decltype(verifyBehaviourFn), std::false_type>::value, "you need to provide a callable");
      return _it(std::forward<String>(should), std::move(verifyBehaviourFn));
    }

  private:
    // sync
    template<typename String>
    SequentialTestSpec& _it(String&& should, std::function<void()>&& verifyBehaviour) {
      if (_head->isRunning()) {
        throw 0;
      }

      return append(
        std::make_shared<SharedTestCase::element_type>(std::forward<String>(should), [behaviour = std::move(verifyBehaviour), notifier = _head] {
          try {
            behaviour();
            notifier->done();
          }
          catch (...) {
            notifier->fail(std::current_exception());
          }
        })
      ).incCaseCount();
    }

    // asnyc
    template<typename String>
    SequentialTestSpec& _it(String&& should, std::function<void(const SharedCaseEndNotifier&)>&& verifyBehaviour) {
      if (_head->isRunning()) {
        throw 0;
      }

      return append(
        std::make_shared<SharedTestCase::element_type>(std::forward<String>(should), 
                                                       std::bind(verifyBehaviour, _head))
      ).incCaseCount();
    }

  public:
    virtual void run(TestRunnableContainer& container) noexcept {
      container.beginRun();
      _head->letItRun(_totalCaseCount, container);
      container.endRun();
    }

  private:
    SequentialTestSpec& append(const SharedTestCase& testCase) {
      SharedTestCase tail = _tail.lock();

      if (tail) {
         tail->next = testCase;
      } else {
        *_head = testCase;
      }

      _tail = testCase;
      return *this;
    }

    SequentialTestSpec& incCaseCount() {
      ++_totalCaseCount;
      return *this;
    }
  };

} // LTest

#endif // TEST_SUITE_H


