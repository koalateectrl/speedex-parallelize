#include <iostream>

int main() {
  int n = 1;
  int a[5] = {1, 2, 3, 4, 5};

  std::pair<int, int> p1 = std::make_pair(n, a[1]);
  std::cout << "The value of p1 is " << p1.first << ", " << p1.second << std::endl;
}
