#include "csv_loader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

std::string trim(const std::string &s) {
  size_t start = 0;
  while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
    ++start;
  }
  size_t end = s.size();
  while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
    --end;
  }
  return s.substr(start, end - start);
}

std::string to_lower(std::string s) {
  for (char &c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

std::vector<std::string> split_csv_line(const std::string &line) {
  std::vector<std::string> fields;
  std::string field;
  bool in_quotes = false;
  for (char c : line) {
    if (c == '"') {
      in_quotes = !in_quotes;
      continue;
    }
    if (c == ',' && !in_quotes) {
      fields.push_back(trim(field));
      field.clear();
    } else {
      field.push_back(c);
    }
  }
  fields.push_back(trim(field));
  return fields;
}

bool parse_double(const std::string &s, double &out) {
  const char *cstr = s.c_str();
  char *end = nullptr;
  out = std::strtod(cstr, &end);
  if (end == cstr || *end != '\0') {
    return false;
  }
  if (!std::isfinite(out)) {
    return false;
  }
  return true;
}

} // namespace

bool load_csv(const std::string &path, CsvData &out, std::string &error) {
  std::ifstream file(path);
  if (!file) {
    error = "Failed to open file: " + path;
    return false;
  }

  std::string header;
  if (!std::getline(file, header)) {
    error = "CSV is empty: " + path;
    return false;
  }

  auto headers = split_csv_line(header);
  int idx_ts = -1;
  int idx_x = -1;
  int idx_y = -1;
  int idx_z = -1;
  for (size_t i = 0; i < headers.size(); ++i) {
    std::string key = to_lower(headers[i]);
    if (key == "timestamp") {
      idx_ts = static_cast<int>(i);
    } else if (key == "acc_x") {
      idx_x = static_cast<int>(i);
    } else if (key == "acc_y") {
      idx_y = static_cast<int>(i);
    } else if (key == "acc_z") {
      idx_z = static_cast<int>(i);
    }
  }

  if (idx_ts < 0 || idx_x < 0 || idx_y < 0 || idx_z < 0) {
    error = "CSV must contain headers: timestamp, acc_x, acc_y, acc_z";
    return false;
  }

  std::string line;
  while (std::getline(file, line)) {
    if (line.empty()) {
      continue;
    }
    auto fields = split_csv_line(line);
    if (static_cast<int>(fields.size()) <= std::max({idx_ts, idx_x, idx_y, idx_z})) {
      continue;
    }

    double ts = 0.0;
    double ax = 0.0;
    double ay = 0.0;
    double az = 0.0;
    if (!parse_double(fields[idx_ts], ts) || !parse_double(fields[idx_x], ax) ||
        !parse_double(fields[idx_y], ay) || !parse_double(fields[idx_z], az)) {
      continue;
    }

    out.timestamps_ms.push_back(ts);
    out.acc_x.push_back(ax);
    out.acc_y.push_back(ay);
    out.acc_z.push_back(az);
  }

  if (out.timestamps_ms.empty()) {
    error = "No valid rows found in CSV";
    return false;
  }

  return true;
}
