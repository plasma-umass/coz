#include <coz.h>

#include <thread>
#include <vector>

namespace {
volatile size_t ax;
volatile size_t ay;
constexpr size_t iterations = 5000000;

void a() {
  for(size_t i = 0; i < iterations; ++i) {
    ax = i;
  }
}

void b() {
  for(size_t i = 0; i < iterations; ++i) {
    ay = i;
  }
}
}  // namespace

int main() {
  for(int i = 0; i < 60; ++i) {
    std::thread at(a);
    std::thread bt(b);
    at.join();
    bt.join();
    COZ_PROGRESS;
  }
  return 0;
}
