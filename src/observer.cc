#include "observer.hh"

template <typename D, typename C>
Concurrent_Observer<D, C>::Concurrent_Observer() : _semaphore(0) {
}

template <typename D, typename C>
void Concurrent_Observer<D, C>::update(C c, D *d) {
  _data.push(d);
  _semaphore.release();
}

template <typename D, typename C>
D *Concurrent_Observer<D, C>::updated() {
  _semaphore.acquire();

  auto datum = _data.top();
  _data.pop();

  return datum;
}
