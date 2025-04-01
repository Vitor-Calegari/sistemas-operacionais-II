#ifndef OBSERVER_HH
#define OBSERVER_HH

#include <queue>
#include <semaphore>

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
  Concurrent_Observer() : _semaphore(0) {
  }

  ~Concurrent_Observer() = default;

  void update(C c, D *d) {
    _data.push(d);
    _semaphore.release();
  }

  D *updated() {
    _semaphore.acquire();

    auto datum = _data.front();
    _data.pop();

    return datum;
  }

private:
  std::counting_semaphore<> _semaphore;
  std::queue<D> _data;
};

#endif
