#ifndef TOPOLOGY_HH
#define TOPOLOGY_HH

#include <cmath>
#include <utility>

class Topology {
  using Coordinate = std::pair<double, double>;
  using Dimension = std::pair<double, double>;

  Topology(Dimension dim, double rsu_range) : _dim(dim), _rsu_range(rsu_range) {
  }

  // TODO: Verificar se o cálculo está correto nos testes.
  int get_quadrant_id(Coordinate coord) {
    auto [x, y] = coord;

    x /= _rsu_range;
    x += _dim.first;

    int x_int = floor(x);
    if (x_int == _dim.first) {
      --x_int;
    }

    y /= _rsu_range;
    y -= _dim.second;

    int y_int = floor(y);
    if (y_int == _dim.second) {
      --y_int;
    }

    return x_int + y_int * _dim.second;
  }

  Dimension get_dimension() const {
    return _dim;
  }

  double get_range() const {
    return _rsu_range;
  }

private:
  Dimension _dim;
  double _rsu_range;
};

#endif
