#include <thread>

  volatile size_t x;
  char padding[128];
  volatile size_t y;


void a() {
  for(x=0; x<2000000000; x++) {}
}

void b() {
  for(y=0; y<1900000000; y++) {}
}

int main() {
  std::thread a_thread(a);
  std::thread b_thread(b);
  
  a_thread.join();
  b_thread.join();
}
