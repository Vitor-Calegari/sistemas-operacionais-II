#ifndef ORDERED_LIST_HH
#define ORDERED_LIST_HH

#include <forward_list>

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

private:
  const D _value;
  const C _rank;
};

template <typename D, typename C>
class Ordered_List : public std::forward_list<Ordered_Node<D, C>> {
private:
  using List = std::forward_list<Ordered_Node<D, C>>;

public:
  using Iterator = List::iterator;

  Ordered_List() : List() {
  }

  void insert(D el, C c) {
    if (List::empty()) {
      List::push_front(Ordered_Node<D, C>{ el, c });
      return;
    }

    Iterator prev, cur;
    for (prev = List::before_begin(), cur = List::begin();
         cur != List::end() && cur->rank() <= c; prev = cur, ++cur)
      ;

    if (cur == List::end()) {
      List::insert_after(prev, Ordered_Node<D, C>{ el, c });
    } else if (prev == List::before_begin()) {
      List::push_front(Ordered_Node<D, C>{ el, c });
    } else {
      List::insert_after(prev, Ordered_Node<D, C>{ el, c });
    }
  }

  void remove(D el, C c) {
    List::remove(Ordered_Node<D, C>{ el, c });
  }
};

#endif
