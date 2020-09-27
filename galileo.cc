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

int GalileoMessage::parseFnav(std::basic_string_view<uint8_t> page)
{
  const uint8_t* ptr = &page[0];
  int offset=0;
  auto gbum=[&ptr, &offset](int bits) {
    unsigned int ret = getbitu(ptr, offset, bits);
    offset += bits;
    return ret;
  };
  
  auto gbsm=[&ptr, &offset](int bits) {
    int ret = getbits(ptr, offset, bits);
    offset += bits;
    return ret;
  };
  
  wtype = gbum(6);
  if(wtype == 1) {
    /*int sv = */    (void)gbum(6);
    iodnav = gbum(10);
    t0c =    gbum(14);
    af0 =    gbsm(31);
    af1 =    gbsm(21);
    af2 =    gbsm(6);
    sisa =   gbum(8);
    ai0 =    gbum(11);
    ai1 =    gbsm(11);
    ai2 =    gbsm(14);
    sf1 =    gbum(1);
    sf2 =    gbum(1);
    sf3 =    gbum(1);
    sf4 =    gbum(1);
    sf5 =    gbum(1);
    BGDE1E5a = gbsm(10);
    e5ahs = gbum(2);
    wn = gbum(12);
    tow = gbum(20);
    e5advs=gbum(1);
  }
  else if(wtype==2)  {
    iodnav = gbum(10);
    m0 = gbsm(32);
    omegadot = gbsm(24);
    e = gbum(32);
    sqrtA = gbum(32);
    omega0 = gbsm(32);
    idot = gbsm(14);
    wn = gbum(12);
    tow = gbum(20);
  }
  else if(wtype == 3) {
    iodnav = gbum(10);
    i0 = gbsm(32);
    omega = gbsm(32);
    deltan = gbsm(16);
    cuc = gbsm(16);
    cus = gbsm(16);
    crc =gbsm(16);
    crs = gbsm(16);
    t0e = gbum(14);
    wn = gbum(12);
    tow = gbum(20);
  }
  else if(wtype == 4) {
    iodnav = gbum(10);
    cic = gbsm(16);
    cis = gbsm(16);

    a0 = gbsm(32);
    a1 = gbsm(24);

    dtLS= gbsm(8);
    t0t = gbum(8);
    wn0t = gbum(8);
    wnLSF = gbum(8);
    
    dn = gbum(3);
    dtLSF = gbsm(8);
    
    t0g = gbum(8);
    a0g = gbsm(16);
    a1g = gbsm(12);
    wn0g = gbum(6);
    tow = gbum(20);
  }
  return wtype;
}
