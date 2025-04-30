#ifndef OBSERVED_HH
#define OBSERVED_HH

#include "concurrent_observer.hh"
#include "ordered_list.hh"

template <typename D, typename C = void>
class Concurrent_Observed {
  friend class Concurrent_Observer<D, C>;

public:
  typedef D Observed_Data;
  typedef C Observing_Condition;
  using Observers = Ordered_List<Concurrent_Observer<D, C> *, C>;

public:
  Concurrent_Observed() = default;

  ~Concurrent_Observed() = default;

  void attach(Concurrent_Observer<D, C> *o, C c) {
    std::lock_guard<std::mutex> lock(_mutex);
    _observers.insert(o, c);
  }

  void detach(Concurrent_Observer<D, C> *o, C c) {
    std::lock_guard<std::mutex> lock(_mutex);
    _observers.remove(o, c);
  }

  bool notify(C c, D *d) {
    std::lock_guard<std::mutex> lock(_mutex);
    bool notified = false;

    for (auto obs = _observers.begin(); obs != _observers.end(); ++obs) {
      if (obs->rank() == c) {
        obs->value()->update(c, d);
        notified = true;
      }
    }

    return notified;
  }

  std::vector<C> getObservsCond() {
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<C> ret;
    for (auto obs = _observers.begin(); obs != _observers.end(); ++obs) {
      ret.push_back(obs->rank());
    }
    return ret;
  }

private:
  Observers _observers;
  std::mutex _mutex{};
};

#endif
