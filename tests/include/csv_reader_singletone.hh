#ifndef CSV_READER_HH
#define CSV_READER_HH
#include "csv.hh"

class CSVReaderSingleTone : public csv::CSVReader {
public:
  static CSVReaderSingleTone &getInstance(std::string filename = "") {
    static CSVReaderSingleTone instance(filename);
    return instance;
  }

private:
  CSVReaderSingleTone(CSVReaderSingleTone const &) = delete;
  void operator=(CSVReaderSingleTone const &) = delete;
  CSVReaderSingleTone(std::string filename) : csv::CSVReader(filename) {
  }
};

#endif