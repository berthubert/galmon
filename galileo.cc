#include "bits.hh"
#include "galileo.hh"

bool getTOWFromInav(std::basic_string_view<uint8_t> inav, uint32_t *satTOW, uint16_t *wn)
{
  unsigned int wtype = getbitu(&inav[0], 0, 6);
  if(wtype==0) {
    if(getbitu(&inav[0], 6,2) == 2) {
      *wn = getbitu(&inav[0], 96, 12);
      *satTOW = getbitu(&inav[0], 108, 20);
      return true;
    }
  }
  else if(wtype==5) {
    *wn = getbitu(&inav[0], 73, 12);
    *satTOW = getbitu(&inav[0], 85, 20);
    return true;
  }
  else if(wtype==6) {
    // !! NO WN!!
    *satTOW=getbitu(&inav[0], 105, 20);
    return true;
  }

  return false;
}
