#ifndef CAM_TRANSDUCER_HH
#define CAM_TRANSDUCER_HH

#include "csv.hh"
#include "smart_unit.hh"
#include "transducer.hh"
#include <array>
#include <cassert>
#include <chrono>

// TODO: verificar se o valor está certo e adicionar método em SmartUnit para
// gerar isso.
constexpr SmartUnit CAM_unit(sizeof(double) * 10);

class CAMTransducer : public TransducerCommon<CAM_unit> {
public:
  std::chrono::milliseconds GENERATION_PERIOD{ 100 };

  CAMTransducer(const std::string &filename)
      : _last_timepoint(std::chrono::steady_clock::now()), _reader(filename) {
    generate_new_cam();
  }

  void get_data(std::byte *data) {
    if (is_new_generation_needed()) {
      generate_new_cam();
    }

    double *a = reinterpret_cast<double *>(data);
    for (int i = 0; i < 10; ++i) {
      a[i] = _cur_cam[i];
    }
  }

private:
  bool is_new_generation_needed() {
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<double> delta = now - _last_timepoint;

    // Caso assert falhar, uma mensagem CAM foi perdida.
    assert(delta < 2 * GENERATION_PERIOD);

    if (delta >= GENERATION_PERIOD) {
      _last_timepoint += GENERATION_PERIOD;

      return true;
    }

    return false;
  }

  void generate_new_cam() {
    csv::CSVRow row;
    _reader.read_row(row);

    int cur_col = 0;
    for (auto &col_name : row.get_col_names()) {
      if (col_name == "timestamp" || col_name == "id") {
        continue;
      }

      _cur_cam[cur_col] = row[col_name].get<double>();
      ++cur_col;
    }
  }

  std::chrono::steady_clock::time_point _last_timepoint;
  csv::CSVReader _reader;
  std::array<double, 10> _cur_cam;
};

#endif
