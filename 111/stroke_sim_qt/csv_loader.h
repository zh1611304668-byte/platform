#pragma once

#include <string>
#include <vector>

struct CsvData {
  std::vector<double> timestamps_ms;
  std::vector<double> acc_x;
  std::vector<double> acc_y;
  std::vector<double> acc_z;
};

bool load_csv(const std::string &path, CsvData &out, std::string &error);
