#ifndef TOPOLOGY_HH
#define TOPOLOGY_HH

#include <cmath>
#include <utility>

class Topology {
public:
  using Coordinate = std::pair<double, double>;
  using Size = std::pair<int, int>;
  using Dimension = std::pair<double, double>;

  Topology(Size size, double rsu_range)
      : _size(size), _rsu_range(rsu_range),
        _dimension(
            { _size.first * 2 * _rsu_range, _size.second * 2 * _rsu_range }) {
  }

  // TODO: Verificar se o cálculo está correto nos testes.
  int get_quadrant_id(Coordinate coord) {
    auto [x, y] = coord;

    x /= 2 * _rsu_range;
    x += double(_size.first) / 2;

    int x_int = floor(x);
    if (x_int == _size.first) {
      --x_int;
    }

    y /= 2 * _rsu_range;
    y -= double(_size.second) / 2;

    int y_int = floor(y);
    if (y_int == _size.second) {
      --y_int;
    }

    return x_int + y_int * _size.second;
  }

  Size get_size() const {
    return _size;
  }

  Dimension get_dimension() const {
    return _dimension;
  }

  double get_range() const {
    return _rsu_range;
  }

private:
  Size _size;
  double _rsu_range;
  Dimension _dimension;
};

#endif
