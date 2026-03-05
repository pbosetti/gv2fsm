#include <unistd.h>
#include <thread>
#include "sm.hpp"

struct Data {
  int count;
};

int main() {
  Data data = {1};
  auto fsm = sm::FiniteStateMachine(&data);
  fsm.set_timing_function([]() {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  });
  try {
  fsm.run([&](Data &s) {
      std::cout << "State: " << fsm.state() << " data: " << s.count << std::endl;
    });
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
  return 0;
}