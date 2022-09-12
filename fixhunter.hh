#pragma once
#include <string>
#include "galileo.hh"

class FixHunter
{
public:
  void reportInav(const std::basic_string<uint8_t>& inav, int32_t gst);
private:
  void tryFix(int32_t gst);
  struct GalileoMessage fillGMFromRS(const std::string& out);
  std::basic_string<uint8_t> inav1, inav2, inav3, inav4, inav16, inav17, inav18, inav19, inav20;
  int d_latestiod;
  uint32_t d_inav16t0r;
};
