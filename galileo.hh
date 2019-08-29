#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include <functional>

bool getTOWFromInav(std::basic_string_view<uint8_t> inav, uint32_t *satTOW, uint16_t *wn);

struct GalileoMessage
{
  uint8_t wtype;

  typedef void (GalileoMessage::*func_t)(std::basic_string_view<uint8_t> page);
  std::vector<func_t> parsers{
    &GalileoMessage::parse0,
    &GalileoMessage::parse1,
    &GalileoMessage::parse2,
    &GalileoMessage::parse3,
    &GalileoMessage::parse4,
    &GalileoMessage::parse5,
    &GalileoMessage::parse6,
    &GalileoMessage::parse7,
    &GalileoMessage::parse8,
    &GalileoMessage::parse9,
    &GalileoMessage::parse10
      };

  
  int parse(std::basic_string_view<uint8_t> page)
  {
    wtype = getbitu(&page[0], 0, 6);
    std::invoke(parsers.at(wtype), this, page);
    return wtype;
  }

  uint8_t sparetime{0};
  uint16_t wn{0};  // we put the "unrolled" week number here!
  uint32_t tow{0}; // "last seen"
  int iodalmanac;
  int wnalmanac;
  int t0almanac;
  int svid1, svid2, svid3;

  
  // spare word, only contains a WN and a TOW, but only if the 'time' field is set to 2
  void parse0(std::basic_string_view<uint8_t> page)
  {
    sparetime = getbitu(&page[0], 6, 2);
    if(sparetime == 2) {
      wn = getbitu(&page[0], 96, 12);
      tow = getbitu(&page[0], 108, 20);
    }
  }

  GalileoMessage()
  {
  }

  uint8_t e5bhs{0}, e1bhs{0};
  uint8_t gpshealth{0};
  uint16_t ai0{0};
  int16_t ai1{0}, ai2{0};
  bool sf1{0}, sf2{0}, sf3{0}, sf4{0}, sf5{0};
  int BGDE1E5a{0}, BGDE1E5b{0};
  bool e5bdvs{false}, e1bdvs{false};
  bool disturb1{false}, disturb2{false}, disturb3{false}, disturb4{false}, disturb5{false};
  
  //                     
  //      2^-30   2^-50  1      8-bit week
  int32_t a0{0}, a1{0}, t0t{0}, wn0t{0};  
  int32_t a0g{0}, a1g{0}, t0g{0}, wn0g{0};
  int8_t dtLS{0}, dtLSF{0};
  uint16_t wnLSF{0};
  uint8_t dn; // leap second day number


  uint32_t t0e; 
  uint32_t e, sqrtA;
  int32_t m0, omega0, i0, omega, idot, omegadot, deltan;
  
  int16_t cuc{0}, cus{0}, crc{0}, crs{0}, cic{0}, cis{0};
  //        60 seconds
  uint16_t t0c; // clock epoch, stored UNSCALED, since it is not in the same place as GPS

  //      2^-34     2^-46
  int32_t af0{-1}   ,   af1{-1};
  //     2^-59
  int8_t af2{-1};
  
  uint8_t sisa;

  uint16_t iodnav;

  struct Almanac
  {
    int svid{-1};
    int af0, af1;
    int e1bhs, e5bhs;
  } alma1, alma2, alma3;
    
  
  // an ephemeris word
  void parse1(std::basic_string_view<uint8_t> page)
  {
    iodnav = getbitu(&page[0], 6, 10);
    t0e = getbitu(&page[0],   16, 14); 
    m0 = getbits(&page[0],    30, 32);
    e = getbitu(&page[0],     62, 32);
    sqrtA = getbitu(&page[0], 94, 32);
  }

  // another ephemeris word
  void parse2(std::basic_string_view<uint8_t> page)
  {
    iodnav = getbitu(&page[0], 6, 10);
    omega0 = getbits(&page[0], 16, 32);
    i0 = getbits(&page[0],     48, 32);
    omega = getbits(&page[0],  80, 32);
    idot = getbits(&page[0],   112, 14);
  }

  // yet another ephemeris word
  void parse3(std::basic_string_view<uint8_t> page)
  {
    iodnav = getbitu(&page[0], 6, 10);
    omegadot = getbits(&page[0], 16, 24);
    deltan = getbits(&page[0],   40, 16);
    cuc = getbits(&page[0],      56, 16);
    cus = getbits(&page[0],      72, 16);
    crc = getbits(&page[0],      88, 16);
    crs = getbits(&page[0],     104, 16);
    sisa = getbitu(&page[0],    120, 8);
  }

  // can't get enough of that ephemeris
  void parse4(std::basic_string_view<uint8_t> page)
  {
    iodnav = getbitu(&page[0], 6, 10);
    cic = getbits(&page[0], 22, 16);
    cis = getbits(&page[0], 38, 16);

    t0c = getbitu(&page[0], 54, 14);
    af0 = getbits(&page[0], 68, 31);
    af1 = getbits(&page[0], 99, 21);
    af2 = getbits(&page[0], 120, 6);
  }

  // ionospheric disturbance, health, group delay, time
  void parse5(std::basic_string_view<uint8_t> page)
  {
    ai0 = getbitu(&page[0], 6, 11);
    ai1 = getbits(&page[0], 17, 11); // ai1 & 2 are signed, 0 not
    ai2 = getbits(&page[0], 28, 14);
    
    sf1 = getbitu(&page[0], 42, 1);
    sf2 = getbitu(&page[0], 43, 1);
    sf3 = getbitu(&page[0], 44, 1);
    sf4 = getbitu(&page[0], 45, 1);
    sf5 = getbitu(&page[0], 46, 1);
    BGDE1E5a = getbits(&page[0], 47, 10);
    BGDE1E5b = getbits(&page[0], 57, 10);
    
    e5bhs = getbitu(&page[0], 67, 2);
    e1bhs = getbitu(&page[0], 69, 2);
    e5bdvs = getbitu(&page[0], 71, 1);
    e1bdvs = getbitu(&page[0], 72, 1);
    wn = getbitu(&page[0], 73, 12);
    tow = getbitu(&page[0], 85, 20);
  }

  // time stuff
  void parse6(std::basic_string_view<uint8_t> page)
  {
    a0 = getbits(&page[0], 6, 32);
    a1 = getbits(&page[0], 38, 24);
    dtLS = getbits(&page[0], 62, 8);
    t0t = getbitu(&page[0], 70, 8);
    wn0t = getbitu(&page[0], 78, 8);
    wnLSF = getbitu(&page[0], 86, 8);
    dn = getbitu(&page[0], 94, 3);
    dtLSF = getbits(&page[0], 97, 8);

    tow = getbitu(&page[0], 105, 20);
  }

  // almanac
  void parse7(std::basic_string_view<uint8_t> page)
  {
    iodalmanac = getbitu(&page[0], 6, 4);
    wnalmanac = getbitu(&page[0], 10, 2);
    t0almanac = getbitu(&page[0], 12, 10);
    alma1.svid     = getbitu(&page[0], 22, 6);
    
  }
  // almanac
  void parse8(std::basic_string_view<uint8_t> page)
  {
    alma1.af0 = getbits(&page[0], 10, 16);
    alma1.af1 = getbits(&page[0], 26, 13);
    alma1.e5bhs = getbitu(&page[0], 39, 2);
    alma1.e1bhs = getbitu(&page[0], 41, 2);
    alma2.svid     = getbitu(&page[0], 43, 6);
  }

  // almanac
  void parse9(std::basic_string_view<uint8_t> page)
  {
    alma2.af0 = getbits(&page[0], 38, 16);
    alma2.af1 = getbits(&page[0], 54, 13);
    alma2.e5bhs = getbitu(&page[0], 67, 2);
    alma2.e1bhs = getbitu(&page[0], 69, 2);
    
    alma3.svid = getbitu(&page[0], 71, 6);
    
    
  }

  // almanac + more time stuff (GPS)
  void parse10(std::basic_string_view<uint8_t> page)
  {

    alma3.af0 = getbits(&page[0], 53, 16);
    alma3.af1 = getbits(&page[0], 69, 13);
    alma3.e5bhs = getbitu(&page[0], 82, 2);
    alma3.e1bhs = getbitu(&page[0], 84, 2);

    
    a0g = getbits(&page[0], 86, 16);
    a1g = getbits(&page[0], 102, 12);
    t0g = getbitu(&page[0], 114, 8);
    wn0g = getbitu(&page[0], 122, 6);
  }

};
