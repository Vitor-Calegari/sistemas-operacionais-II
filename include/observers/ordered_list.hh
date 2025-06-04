#ifndef ORDERED_LIST_HH
#define ORDERED_LIST_HH

#include <algorithm>
#include <vector>

template <typename D, typename C>
class Ordered_Node {
public:
  Ordered_Node(const D d, const C c = 0) : _value(d), _rank(c) {
  }

  const D &value() const {
    return _value;
  }

  const C &rank() const {
    return _rank;
  }

  friend bool operator==(const Ordered_Node<D, C> &lhs,
                         const Ordered_Node<D, C> &rhs) {
    return lhs.value() == rhs.value() && lhs.rank() == rhs.rank();
  }

  friend bool operator<(const Ordered_Node<D, C> &lhs,
                        const Ordered_Node<D, C> &rhs) {
    return lhs._rank < rhs._rank;
  }

private:
  D _value;
  C _rank;
};

template <typename D, typename C>
class Ordered_List {
private:
  using List = std::vector<Ordered_Node<D, C>>;
  List _list;

public:
  using Iterator = typename List::iterator;
  using ConstIterator = typename List::const_iterator;

  Iterator begin() {
    return _list.begin();
  }

  Iterator end() {
    return _list.end();
  }

  ConstIterator begin() const {
    return _list.begin();
  }

  ConstIterator end() const {
    return _list.end();
  }

  Ordered_List() = default;

  void insert(D d, C c) {
    Ordered_Node<D, C> new_node(d, c);
    _list.push_back(new_node);
    std::sort(_list.begin(), _list.end());
  }

  void remove(D d, C c) {
    auto it = std::find_if(_list.begin(), _list.end(),
                           [&](const Ordered_Node<D, C> &node) {
                             return node.value() == d && node.rank() == c;
                           });
    if (it != _list.end()) {
      _list.erase(it);
    }
  }
};

#endif
