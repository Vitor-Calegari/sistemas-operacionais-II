#ifndef CONDITIONALLY_DATA_OBSERVED_HH
#define CONDITIONALLY_DATA_OBSERVED_HH

#include "conditional_data_observer.hh"
#include "ordered_list.hh"

template <typename T, typename Condition = void>
class Conditionally_Data_Observed {
  friend class Conditional_Data_Observer<T, Condition>;

public:
  using Observer = Conditional_Data_Observer<T, Condition>;
  using Observers = Ordered_List<Observer, Condition>;
  using Observed_Data = T;

public:
  virtual void attach(Observer *o, Condition c) {
    _observers.insert(o, c);
  }

  virtual void detach(Observer *o, Condition c) {
    _observers.remove(o, c);
  }

  virtual bool notify(Condition c, Observed_Data *d) {
    bool notified = false;

    for (auto obs = _observers.begin(); obs != _observers.end(); ++obs) {
      if (obs->rank() == c) {
        obs->value()->update(this, c, d);
        notified = true;
      }
    }

    return notified;
  }

private:
  Observers _observers;
};

#endif
