#include "observed.hh"

#include <algorithm>

template <typename D, typename C>
void Concurrent_Observed<D, C>::attach(Concurrent_Observer<D, C> *o, C c) {
  _observers.insert(std::lower_bound(_observers.cbegin(), _observers.cend(), o),
                    o);
}

template <typename D, typename C>
void Concurrent_Observed<D, C>::detach(Concurrent_Observer<D, C> *o, C c) {
  _observers.remove(o);
}

template <typename D, typename C>
bool Concurrent_Observed<D, C>::notify(C c, D *d) {
  bool notified = false;

  for (auto &obs = _observers.begin(); obs != _observers.end(); ++obs) {
    if (obs->rank() == c) {
      obs->update(c, d);
      notified = true;
    }
  }

  return notified;
}
