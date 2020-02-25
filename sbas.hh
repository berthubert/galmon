#pragma once
#include <cstdint>
#include <string>
#include <map>
#include "navmon.hh"
#include <vector>
#include "minivec.hh"
      
// 0 do not use
// 1 PRN mask
// 2-5 fast corrections
// 7 Fast correction degradation factor
// 10 Degradation parameters
// 18 Ionospheric grid point masks
// 24 Mixed fastllong-term satellite error corrections
// 25 half-message
// 26 Ionospheric delay corrections
// 27 SBAS service message

// GSA HQ Prague
const Point c_egnosCenter{3970085, 1021937, 4869792};

// Somewhere in Minnesota, Dakota, Canada border
const Point c_waasCenter{-510062, -4166466, 4786089};

struct SBASState
{
  struct FastCorrection
  {
    SatID id;
    double correction;
    int udrei;
    time_t lastUpdate{-1};
  };
  
  struct LongTermCorrection
  {
    SatID id;
    int iod8;
    int toa;
    int iodp;
    double dx, dy, dz, dai;
    double ddx, ddy, ddz, ddai;
    bool velocity{false};
    time_t lastUpdate{-1};
  };

  void parse(const std::basic_string<uint8_t>& sbas, time_t now);
  
  void parse0(const std::basic_string<uint8_t>& message, time_t now);

  // updates slot2prn mapping
  void parse1(const std::basic_string<uint8_t>& message, time_t now);


  std::vector<FastCorrection> parse2_5(const std::basic_string<uint8_t>& message, time_t now);

  std::vector<FastCorrection> parse6(const std::basic_string<uint8_t>& message, time_t now);
  void parse7(const std::basic_string<uint8_t>& message, time_t now);

  std::pair<std::vector<FastCorrection>, std::vector<LongTermCorrection>> parse24(const std::basic_string<uint8_t>& message, time_t now);

  std::vector<LongTermCorrection> parse25(const std::basic_string<uint8_t>& message, time_t now);

  int getSBASNumber(int slot) const;
  SatID getSBASSatID(int slot) const;
  
  std::map<SatID, FastCorrection> d_fast;
  std::map<SatID, LongTermCorrection> d_longterm;

  time_t d_lastDNU{-1};
  std::map<int,int> d_slot2prn;
  int d_latency = -1;
  time_t d_lastSeen{-1};
  void parse25H(const std::basic_string<uint8_t>& sbas, time_t t, int offset, std::vector<LongTermCorrection>& ret);

};

struct SBASCombo
{
  SBASState::FastCorrection fast;
  SBASState::LongTermCorrection longterm;
};
