#pragma once
#include <string>
#include <map>
#include <memory>
class SGP4;
class Tle;

class TLERepo
{
public:
  TLERepo();
  ~TLERepo();
  void parseFile(std::string_view fname);
  struct Match
  {
    std::string name;
    int norad;
    std::string internat;
    double inclination{360}; // radians
    double ran;         // radians
    double e{-1};       
    double ecefX{0};  // m
    double ecefY{0};  // m
    double ecefZ{0};  // m

    double eciX{0}, eciY{0}, eciZ{0}; // m
    double distance{-1}; // m

    double latitude, longitude, altitude;
  };
  
  Match getBestMatch(time_t, double x, double y, double z, Match* secondbest=0);

  
private:
  std::map<std::string,
           std::unique_ptr<std::tuple<SGP4, Tle>>
          > d_sgp4s;
};
