#ifndef CONDITIONAL_DATA_OBSERVER_HH
#define CONDITIONAL_DATA_OBSERVER_HH

template <typename T, typename Condition>
class Conditionally_Data_Observed;

template <typename T, typename Condition = void>
class Conditional_Data_Observer {
  friend class Conditionally_Data_Observed<T, Condition>;

public:
  typedef T Observed_Data;
  typedef Condition Observing_Condition;
};

template <typename T>
class Conditional_Data_Observer<T, void> {
  friend class Conditionally_Data_Observed<T, void>;

public:
  typedef T Observed_Data;
};

#endif
