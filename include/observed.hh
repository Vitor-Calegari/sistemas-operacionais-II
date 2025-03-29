#ifndef OBSERVED_HH
#define OBSERVED_HH

#include "observer.hh"
#include <list>

// TODO!
template <typename T, typename Condition = void>
class Conditionally_Data_Observed;

template <typename D, typename C = void>
class Concurrent_Observed {
  friend class Concurrent_Observer<D, C>;

public:
  typedef D Observed_Data;
  typedef C Observing_Condition;
  // TODO! Implementar a classe Ordered_List ou ver se std::list é suficiente.
  // Além disso, ver para que serve parâmetro C (provavelmente para o rank()):
  // typedef Ordered_List<Concurrent_Observer<D, C>, C> Observers;
  typedef std::list<Concurrent_Observer<D, C>> Observers;

public:
  Concurrent_Observed() = default;

  ~Concurrent_Observed() = default;

  void attach(Concurrent_Observer<D, C> *o, C c);

  void detach(Concurrent_Observer<D, C> *o, C c);

  bool notify(C c, D *d);

private:
  Observers _observers;
};

#endif
