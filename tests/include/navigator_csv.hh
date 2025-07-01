#ifndef NAVIGATOR_CSV_HH
#define NAVIGATOR_CSV_HH

#include "csv.hh"
#include "navigator.hh"
#include <chrono>
#include <string>

class NavigatorCSV : public NavigatorCommon {
public:
  std::chrono::milliseconds GENERATION_PERIOD{ 100 };

  NavigatorCSV(const std::string &filename, Topology topology,
               double comm_range)
      : NavigatorCommon(topology, comm_range), _reader(filename) {
  }

  Coordinate get_location() override {
    if (is_new_generation_needed()) {
      generate_new_location();
    }

    return _cur_point;
  }

private:
  bool is_new_generation_needed() {
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<double> delta = now - _last_timepoint;

    assert(delta < 2 * GENERATION_PERIOD);

    if (delta >= GENERATION_PERIOD) {
      _last_timepoint += GENERATION_PERIOD;

      return true;
    }

    return false;
  }

  void generate_new_location() {
    csv::CSVRow row;
    _reader.read_row(row);

    _cur_point = { row["x"].get<double>(), row["y"].get<double>() };
  }

  std::chrono::steady_clock::time_point _last_timepoint;
  csv::CSVReader _reader;
  Coordinate _cur_point;
};

#endif
