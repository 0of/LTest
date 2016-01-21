/*
* LTest
*
* Copyright (c) 2016 "0of" Magnus
* Licensed under the MIT license.
* https://github.com/0of/LTest/blob/master/LICENSE
*/
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <chrono>

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
      MonitorThread()
        : _thread([this] {
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
              _isIdle = true;
            }
          }
        }) {
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
      _monitorThread->notifyBeginRun();
    }
    virtual void endRun() override {
      _monitorThread->notifyEndRun();
      _running = nullptr;
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

  private:
    void startTheLoop() {
      while (true) {
        if (!_aboutToRun) {
          if (!_running) {
            break;
          } else {
            std::this_thread::yield();
          }
        } else {
          if (!_running) {
            // start the runnable
            _running = std::move(_aboutToRun);
            _running->run(*this);
          } else {
            std::this_thread::yield();
          }
        }
      }
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
  class SequentialTestSpec : public std::enable_shared_from_this<TestRunable>, public TestRunable {
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

    public:
      TestCaseLinkedHead()
        : _container{ nullptr }
        , _currentCase{}
      {}

    public:
      virtual void fail(std::exception_ptr e) noexcept override {
        SharedTestCase next = _currentCase->next;

        if (next) {
          _container->scheduleToRun(std::make_shared<TestCaseRunnable>(next));
        }
        
        _container->endRun();

        _currentCase = next;
      }

      virtual void done() noexcept override {
        SharedTestCase next = _currentCase->next;

        if (next) {
          _container->scheduleToRun(std::make_shared<TestCaseRunnable>(next));
        }
        
        _container->endRun();

        _currentCase = next;
      }

    public:
      TestCaseLinkedHead& operator = (const SharedTestCase& headTestCase) {
        _currentCase = headTestCase;
        return *this;
      }

    public:
      void letItRun(TestRunnableContainer& container) {
        // hold the container
        _container = &container;

        if (_currentCase) {
          _container->scheduleToRun(std::make_shared<TestCaseRunnable>(_currentCase));
        }
      }
      bool isRunning() const { return _container; }
    };  

  private:
    std::shared_ptr<TestCaseLinkedHead> _head;
    std::weak_ptr<TestCase> _tail;

  public:
    SequentialTestSpec()
      : _head{std::make_shared<TestCaseLinkedHead>()}
      , _tail{}
    {}

  public:
    // sync
    template<typename String>
    SequentialTestSpec& it(String&& should, std::function<void()>&& verifyBehaviour) {
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
      );
    }

    // asnyc
    template<typename String>
    SequentialTestSpec& it(String&& should, std::function<void(const SharedCaseEndNotifier&)>&& verifyBehaviour) {
      if (_head->isRunning()) {
        throw 0;
      }

      return append(
        std::make_shared<SharedTestCase::element_type>(std::forward<String>(should), 
                                                       std::bind(verifyBehaviour, _head))
      );
    }

  public:
    virtual void run(TestRunnableContainer& container) noexcept {
      container.beginRun();
      _head->letItRun(container);
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
  };

} // LTest

