#ifndef OBSERVER_HH
#define OBSERVER_HH

#include <semaphore>
#include <stack>

template <typename D, typename C>
class Concurrent_Observed;

// TODO!
template <typename T, typename Condition = void>
class Conditional_Data_Observer;

template <typename D, typename C>
class Concurrent_Observer {
  friend class Concurrent_Observed<D, C>;

public:
  typedef D Observed_Data;
  typedef C Observing_Condition;

public:
  Concurrent_Observer();

  ~Concurrent_Observer() = default;

  void update(C c, D *d);

  D *updated();

private:
  std::counting_semaphore<> _semaphore;
  // Isso parece ser uma pilha. Se não for, usar std::list.
  std::stack<D> _data;
};

#endif
