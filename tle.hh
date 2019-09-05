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
    double inclination; // radians
    double ran;         // radians
    double e;       
    double ecefX;  // m
    double ecefY;  // m
    double ecefZ;  // m

    double eciX, eciY, eciZ; // m
    double distance{-1}; // m

    double latitude, longitude, altitude;
  };
  
  Match getBestMatch(time_t, double x, double y, double z, Match* secondbest=0);

  
private:
  std::map<std::string,
           std::unique_ptr<std::tuple<SGP4, Tle>>
          > d_sgp4s;
};
