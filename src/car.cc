#include "ordered_list.hh"
#include <iostream>

// Main temporária para não dar erro de compilação.
int main() {
  Ordered_List<int, int> k;
  int v1 = 3;
  int v2 = 1;
  int v3 = 22;
  k.insert(v1, 3);
  k.insert(v2, 2);
  k.insert(v2, 1);
  k.insert(v3, 12);
  k.insert(v3, 2);
  k.insert(v1, 2);
  k.insert(v3, 0);

  k.remove(v3, 0);
  k.remove(v2, 2);
  k.remove(v3, 12);

  for (Ordered_List<int, int>::Iterator it = k.begin(); it != k.end(); ++it) {
    std::cout << it->value() << ' ' << it->rank() << '\n';
  }

  return 0;
}
