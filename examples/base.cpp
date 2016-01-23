#include "../TestSuite.h"

void throwSomething() {
  throw std::exception();
}

void initSpec(LTest::SequentialTestSpec& spec) {
  spec.it("should be ok", []{
    std::cout << "ok";
  })
  .it("should throw an exception", []{
    try {
      throwSomething();
    } catch (const std::exception& e) {
      std::cout << "ok";
    }
  })
  .it("should work asynchronously", [](const LTest::SharedCaseEndNotifier& notifier) {
    std::cout << "ok";
    notifier->done();
  })
  .it("should be ok, but actually not)", []{
    throwSomething();
  })
  .it("should be ok, but timeout", []{
    std::this_thread::sleep_for(1s);
  });
}

int main() {
  // init container
  auto container = std::make_unique<LTest::SequentialTestRunnableContainer>();

  // alloc spec
  auto spec = std::make_shared<LTest::SequentialTestSpec>();

  // init spec
  initSpec(*spec);

  // assign the spec as first runnable to run
  container->scheduleToRun(spec);

  // run the container
  container->start();

  return 0;
}