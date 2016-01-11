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

} // LTest

