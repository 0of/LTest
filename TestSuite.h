/*
* LTest
*
* Copyright (c) 2016 "0of" Magnus
* Licensed under the MIT license.
* https://github.com/0of/LTest/blob/master/LICENSE
*/
#include <iostream>
#include <sstream>
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

    std::ostringstream _redirectCout;
    std::streambuf *_coutStreamBuf{ nullptr };

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
            // restore cout
            restoreCout();
            break;
          } else {
            std::this_thread::yield();
          }
        } else {
          if (!_running) {
            restoreCout();

            // start the runnable
            _running = std::move(_aboutToRun);

            redirectCout();

            _running->run(*this);
          } else {
            std::this_thread::yield();
          }
        }
      }
    }

    void redirectCout() {
      // redirect cout
      _coutStreamBuf = std::cout.rdbuf();
      std::cout.rdbuf(_redirectCout.rdbuf());
    }

    void restoreCout() {
      if (_coutStreamBuf) {
        std::cout.rdbuf(_coutStreamBuf);
        _coutStreamBuf = nullptr;

        // flush 
        std::cout << _redirectCout.str() << std::endl;
        _redirectCout.str("");
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
        std::cout << "\x1b[4;36m" << "it " << _testCase->should;
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
        std::cout << "\x1b[22;31m" << u8"\xE2\x9D\x8C" /* cross mark */ << "\x1b[0m" << std::endl;

        SharedTestCase next = _currentCase->next;

        if (next) {
          _container->scheduleToRun(std::make_shared<TestCaseRunnable>(next));
        }
        
        _container->endRun();

        _currentCase = next;
      }

      virtual void done() noexcept override {
        std::cout << "\x1b[22;32m" << u8"\xE2\x9C\x93" /* check mark */ << "\x1b[0m" << std::endl;

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
      void letItRun(std::uint32_t totalCount, TestRunnableContainer& container) {
        // hold the container
        _container = &container;
        _totalCaseCount = totalCount;

        if (_currentCase) {
          _container->scheduleToRun(std::make_shared<TestCaseRunnable>(_currentCase));
        }
      }
      bool isRunning() const { return _container; }
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
      ).incCaseCount();
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

