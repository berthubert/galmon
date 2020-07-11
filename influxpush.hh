#pragma once
#include <string>
#include <set>
#include <optional>
#include <initializer_list>
#include "navparse.hh"
#include <variant>

struct InfluxPusher
{
  typedef std::variant<double, int32_t, uint32_t, int64_t, string> var_t;
  explicit InfluxPusher(std::string_view dbname);
  void addValueObserver(int src, std::string_view name, const std::initializer_list<std::pair<const char*, double>>& values, double t, std::optional<SatID> satid=std::optional<SatID>());
  void addValue(const SatID& id, std::string_view name, const std::initializer_list<std::pair<const char*, var_t>>& values, double t, std::optional<int> src = std::optional<int>(), std::optional<string> tag = std::optional<string>("src"));


  void addValue(const vector<pair<string, var_t>>& tags, string_view name, const initializer_list<pair<const char*, var_t>>& values, double t);
  void checkSend();
  void doSend(const std::set<std::string>& buffer);
  ~InfluxPusher();
  std::set<std::string> d_buffer;
  void queueValue(const std::string& line);
  
  time_t d_lastsent{0};
  string d_dbname;
  bool d_mute{false};
  int64_t d_nummsmts{0};
  int64_t d_numvalues{0};
  int64_t d_numdedupmsmts{0};
  map<std::string, int64_t> d_msmtmap;
};
