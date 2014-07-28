#include <thread>

volatile size_t x, y;

void a() {
  for(x=0; x<3000000000; x++) {}
}

void b() {
  for(y=0; y<2990000000; y++) {}
}

int main() {
  std::thread a_thread(a);
  std::thread b_thread(b);
  
  a_thread.join();
  b_thread.join();
}
