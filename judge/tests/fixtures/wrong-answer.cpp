// Prints a deliberately wrong answer for the range-sum-query cases: one token instead of one per query.
#include <iostream>

int main() {
  long long value = 0;
  long long count = 0;
  if (!(std::cin >> count >> value)) {
    return 0;
  }
  std::cout << 0 << '\n';
  return 0;
}
