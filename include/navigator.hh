#ifndef NAVIGATOR_HH
#define NAVIGATOR_HH

#include "topology.hh"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <random>
#include <tuple>
#include <utility>
#include <vector>

class NavigatorCommon {
public:
  using Coordinate = std::pair<double, double>;
  using Dimension = std::pair<double, double>;

  NavigatorCommon(Topology topology, double comm_range, double speed = 1)
      : _last_timepoint(std::chrono::steady_clock::now()), _speed(speed), _x(0),
        _y(0), _comm_range(comm_range), _topology(topology) {
  }

  virtual Coordinate get_location() = 0;

  bool is_in_range(Coordinate coord) const {
    auto [other_x, other_y] = coord;

    auto delta_x = std::abs(_x - other_x);
    auto delta_y = std::abs(_y - other_y);

    return delta_x * delta_x + delta_y * delta_y <= _comm_range * _comm_range;
  }

  Topology get_topology() const {
    return _topology;
  }

protected:
  double compute_delta_time() {
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<double> delta = now - _last_timepoint;
    _last_timepoint = now;
    return delta.count();
  }

  // Restrict the coordinate to stay inside the network topology.
  Coordinate clamp_coordinate(Coordinate coord) const {
    auto [topo_dim_x, topo_dim_y] = _topology.get_dimension();
    auto [new_x, new_y] = coord;

    new_x = std::min(topo_dim_x / 2, std::max(-topo_dim_x / 2, new_x));
    new_y = std::min(topo_dim_y / 2, std::max(-topo_dim_y / 2, new_y));

    return { new_x, new_y };
  }

  std::chrono::steady_clock::time_point _last_timepoint;
  double _speed;
  double _x, _y;

  const double _comm_range;
  const Topology _topology;
};

class NavigatorRandomWalk : public NavigatorCommon {
public:
  NavigatorRandomWalk(Topology topology, double comm_range, double speed = 1)
      : NavigatorCommon(topology, comm_range, speed),
        _rng(std::random_device{}()), _dist(0, 1), _angle(0), _angular_vel(0) {
  }

  Coordinate get_location() override {
    double dt = compute_delta_time();

    double ang_accel = _ang_accel_scale * _dist(_rng);
    _angular_vel = _angular_vel * (1 - _ang_damping * dt) + ang_accel * dt;
    _angle += _angular_vel * dt;

    double new_x = _x + _speed * std::cos(_angle) * dt;
    double new_y = _y + _speed * std::sin(_angle) * dt;

    std::tie(_x, _y) = clamp_coordinate({ new_x, new_y });
    return { _x, _y };
  }

private:
  std::mt19937 _rng;
  std::normal_distribution<double> _dist;

  double _angle;
  double _angular_vel;

  static constexpr double _ang_accel_scale = 0.75;
  static constexpr double _ang_damping = 0.5;
};

class NavigatorDirected : public NavigatorCommon {
public:
  NavigatorDirected(const std::vector<Coordinate> &points, Topology topology,
                    double comm_range, double speed = 1)
      : NavigatorCommon(topology, comm_range, speed), _points(points),
        _cur_point(0), _next_point(1), _seg_len_remaining(0), _unit_x(0),
        _unit_y(0) {
    for (auto &point : _points) {
      point = clamp_coordinate(point);
    }

    if (!_points.empty()) {
      std::tie(_x, _y) = _points[0];

      if (_points.size() > 1) {
        calc_line_segment();
      }
    }
  }

  Coordinate get_location() override {
    if (_points.size() < 2 || _speed <= 0) {
      return { _x, _y };
    }

    double dist_walked = _speed * compute_delta_time();
    while (dist_walked > 0) {
      if (dist_walked < _seg_len_remaining) {
        // Did not reach the next point.
        _x += _unit_x * dist_walked;
        _y += _unit_y * dist_walked;
        _seg_len_remaining -= dist_walked;
        break;
      }
      dist_walked -= _seg_len_remaining;

      // Reached the next point.
      std::tie(_x, _y) = _points[_next_point];

      _cur_point = _next_point;
      _next_point = (_next_point + 1) % _points.size();
      calc_line_segment();
    }

    return { _x, _y };
  }

private:
  void calc_line_segment() {
    double dx = _points[_next_point].first - _points[_cur_point].first;
    double dy = _points[_next_point].second - _points[_cur_point].second;

    _seg_len_remaining = std::hypot(dx, dy);
    if (_seg_len_remaining > 0) {
      _unit_x = dx / _seg_len_remaining;
      _unit_y = dy / _seg_len_remaining;
    } else {
      _unit_x = 0;
      _unit_y = 0;
    }
  }

  std::vector<Coordinate> _points;
  size_t _cur_point, _next_point;
  double _seg_len_remaining, _unit_x, _unit_y;
};

#endif
