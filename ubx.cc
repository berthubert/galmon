#include <iostream>
#include "ubx.hh"
#include "fmt/format.h"
#include "fmt/printf.h"
#include "bits.hh"
#include "navmon.hh"

using namespace std;
uint16_t calcUbxChecksum(uint8_t ubxClass, uint8_t ubxType, std::basic_string_view<uint8_t> str)
{
  uint8_t CK_A = 0, CK_B = 0;

  auto update = [&CK_A, &CK_B](uint8_t c) {
    CK_A = CK_A + c;
    CK_B = CK_B + CK_A;
  };
  update(ubxClass);
  update(ubxType);
  uint16_t len = str.size();
  update(((uint8_t*)&len)[0]);
  update(((uint8_t*)&len)[1]);
  for(unsigned int I=0; I < str.size(); I++) {
    update(str[I]);
  }
  return (CK_B << 8) + CK_A;
}

std::basic_string<uint8_t> buildUbxMessage(uint8_t ubxClass, uint8_t ubxType, const std::initializer_list<uint8_t>& lst)
{
  std::basic_string<uint8_t> str;
  for(const auto& a : lst)
    str.append(1, a);
  return buildUbxMessage(ubxClass, ubxType, str);
}

std::basic_string<uint8_t> buildUbxMessage(uint8_t ubxClass, uint8_t ubxType, std::basic_string_view<uint8_t> str)
{
  // 0xb5 0x62 class id len1 len2 payload cka ckb

  std::basic_string<uint8_t> msg;
  msg.append(1, 0xb5);
  msg.append(1, 0x62);
  msg.append(1, ubxClass); // CFG
  msg.append(1, ubxType); // MSG
  msg.append(1, str.size()); // len1
  msg.append(1, str.size()/256); // len2
  
  for(unsigned int n= 0 ; n < str.size(); ++n)
    msg.append(1, str[n]); 

  uint16_t csum = calcUbxChecksum(ubxClass, ubxType, msg.substr(6));

  msg.append(1, csum&0xff);
  msg.append(1, csum>>8);
  /*
  for(const auto& c : msg) {
    fmt::fprintf(stderr, "%02x ", (int)c);
  }
  fmt::fprintf(stderr,"\n");
  */
  return msg;
}

basic_string<uint8_t> getInavFromSFRBXMsg(std::basic_string_view<uint8_t> msg,
                                          basic_string<uint8_t>& reserved1,
                                          basic_string<uint8_t>& reserved2,
                                          basic_string<uint8_t>& sar,
                                          basic_string<uint8_t>& spare,
                                          basic_string<uint8_t>& crc)
{
  // byte order adjustment
  std::basic_string<uint8_t> payload;
  for(unsigned int i = 0 ; i < (msg.size() - 8) / 4; ++i)
    for(int j=1; j <= 4; ++j)
      payload.append(1, msg[8 + (i+1) * 4 -j]);

  /* test crc (4(pad) + 114 + 82 bits) */
  unsigned char crc_buff[26]={0};
  unsigned int i,j;
  for (i=0,j=  4;i<15;i++,j+=8) setbitu(crc_buff,j,8,getbitu(payload.c_str()   ,i*8,8));
  for (i=0,j=118;i<11;i++,j+=8) setbitu(crc_buff,j,8,getbitu(payload.c_str()+16,i*8,8));
  if (rtk_crc24q(crc_buff,25) != getbitu(payload.c_str()+16,82,24)) {
    cerr << "CRC mismatch, " << rtk_crc24q(crc_buff, 25) << " != " << getbitu(payload.c_str()+16,82,24) <<endl;
    throw CRCMismatch();
  }

  crc.clear();
  for(i=0; i < 3; ++i)
    crc.append(1, getbitu(payload.c_str()+16,82+i*8,8));
  
  std::basic_string<uint8_t> inav;
  
  for (i=0,j=2; i<14; i++, j+=8)
    inav.append(1, (unsigned char)getbitu(payload.c_str()   ,j,8));
  for (i=0,j=2; i< 2; i++, j+=8)
    inav.append(1, (unsigned char)getbitu(payload.c_str()+16,j,8));

  reserved1.clear();
  for(i=0, j=18; i < 5 ; i++, j+=8)
    reserved1.append(1, (unsigned char)getbitu(payload.c_str()+16, j, 8));
  //  cerr<<"reserved1: "<<makeHexDump(reserved1)<<endl;

  sar.clear();
  for(i=0, j=58; i < 3 ; i++, j+=8) // you get 24 bits
    sar.append(1, (unsigned char)getbitu(payload.c_str()+16, j, 8));

  spare.clear();
  spare.append(1, (unsigned char)getbitu(payload.c_str()+16, 80, 2));

  reserved2.clear();
  reserved2.append(1, (unsigned char)getbitu(payload.c_str()+16, 106, 8));
  
  return inav;
}

// XXX this should do the parity check
basic_string<uint8_t> getGPSFromSFRBXMsg(std::basic_string_view<uint8_t> msg)
{
  // byte order adjustment
  std::basic_string<uint8_t> payload;
  for(unsigned int i = 0 ; i < (msg.size() - 8) / 4; ++i)
    for(int j=1; j <= 4; ++j)
      payload.append(1, msg[8 + (i+1) * 4 -j]);


  return payload;
}

// note, this returns the fourth UBX specific word with derived data, feel free to ignore!
basic_string<uint8_t> getGlonassFromSFRBXMsg(std::basic_string_view<uint8_t> msg)
{
  // byte order adjustment
  std::basic_string<uint8_t> payload;
  for(unsigned int i = 0 ; i < (msg.size() - 8) / 4; ++i)
    for(int j=1; j <= 4; ++j)
      payload.append(1, msg[8 + (i+1) * 4 -j]);


  return payload;
}

// note, this returns the fourth UBX specific word with derived data, feel free to ignore!
basic_string<uint8_t> getBeidouFromSFRBXMsg(std::basic_string_view<uint8_t> msg)
{
  // byte order adjustment
  std::basic_string<uint8_t> payload;
  for(unsigned int i = 0 ; i < (msg.size() - 8) / 4; ++i)
    for(int j=1; j <= 4; ++j)
      payload.append(1, msg[8 + (i+1) * 4 -j]);


  return payload;
}

basic_string<uint8_t> getSBASFromSFRBXMsg(std::basic_string_view<uint8_t> msg)
{
  // byte order adjustment
  std::basic_string<uint8_t> payload;
  for(unsigned int i = 0 ; i < (msg.size() - 8) / 4; ++i)
    for(int j=1; j <= 4; ++j)
      payload.append(1, msg[8 + (i+1) * 4 -j]);

  
  return payload;
}
