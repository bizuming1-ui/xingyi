#include "../firmware/include/ButtonCounter.h"
#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
  ButtonCounter b;
  assert(b.tick(true, 10) == 0);
  assert(b.tick(false, 18) == 0);  // contact bounce
  assert(b.tick(true, 22) == 0);
  assert(b.tick(true, 46) == 0);
  assert(b.tick(true, 47) == 1);   // stable press
  assert(b.tick(true, 546) == 0);
  assert(b.tick(true, 547) == 1);  // 500ms after stable press
  assert(b.tick(true, 847) == 3);  // missed loop iterations are caught up
  assert(b.tick(false, 850) == 0); // no repeat while release debounces
  assert(b.tick(false, 875) == 0);
  assert(b.tick(true, 900) == 0);
  assert(b.tick(true, 925) == 1);

  ButtonCounter wrap(false, UINT32_MAX - 40);
  assert(wrap.tick(true, UINT32_MAX - 30) == 0);
  assert(wrap.tick(true, 0) == 1); // millis rollover
  assert(wrap.tick(true, 500) == 1);
  std::cout << "button counter tests passed\n";
}
