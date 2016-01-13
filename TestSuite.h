/*
* LTest
*
* Copyright (c) 2016 "0of" Magnus
* Licensed under the MIT license.
* https://github.com/0of/LTest/blob/master/LICENSE
*/
#include <iostream>

namespace LTest {
	
	class TestRunnableContainer;

	class TestRunable {
	public:
		virtual ~TestRunable() = default;

	public:
		virtual void run(TestRunnableContainer& container) = 0;
	};

	using SharedTestRunnable = std::shared_ptr<TestRunable>;

	// all the cases run inside the container
	class TestRunnableContainer {
	public:
		virtual ~TestRunnableContainer() = default;

	public:
		virtual void scheduleToRun(const SharedTestRunnable& runnable);

		// whenever a case begin to run
		virtual void beginRun();
		virtual void endRun();
	};

	class SequenceTestRunnableContainer : public TestRunnableContainer {
	private:
		SharedTestRunnable _aboutToRun;
		SharedTestRunnable _running;

	public:
		virtual ~SequenceTestRunnableContainer() = default;

	public:
		virtual scheduleToRun(const SharedTestRunnable& runnable);

	public:
		void start();
	};

  class CaseEndNotifier {
  public:
    virtual ~CaseEndNotifier() = default;

  public:
    virtual void done(std::exception_ptr e) = 0;
  };

  using SharedCaseEndNotifier = std::shared_ptr<CaseEndNotifier>;

  // the structure of the test cases
  class SequenceTestSpec : public std::enable_shared_from_this<TestRunable>, public TestRunable {
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
        , next { nullptr }
      {}
    };

    class TestCaseLinkedHead : public CaseEndNotifier {
    private:
      std::shared_ptr<TestRunable> _runnable;
      std::shared_ptr<TestCase> _currentCase;

    public:
      virtual void done(std::exception_ptr e) override;
    };  

  private:
    std::shared_ptr<TestCaseLinkedHead> _head;
    std::weak_ptr<TestCase> _tail;

  public:
    // sync
    template<typename String>
    SequenceTestSpec& it(String&& should, std::function<void()>&& verifyBehaviour) {
      return *this;
    }

    // asnyc
    template<typename String>
    SequenceTestSpec& it(String&& should, std::function<void(const SharedCaseEndNotifier&)>&& verifyBehaviour) {
      return *this;
    }

  public:
    virtual void run(TestRunnableContainer& container) {

    }

    virtual void scheduleToRun(const SharedTestRunnable& runnable);

    // whenever a case begin to run
    virtual void beginRun();
    virtual void endRun();
  };

} // LTest

