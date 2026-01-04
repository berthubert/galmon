#include <iostream>
#include "ubx.hh"
#include "fmt/format.h"
#include "fmt/printf.h"
#include "bits.hh"
#include "navmon.hh"

using namespace std;
uint16_t calcUbxChecksum(uint8_t ubxClass, uint8_t ubxType, const std::vector<uint8_t>& str)
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

std::vector<uint8_t> buildUbxMessage(uint8_t ubxClass, uint8_t ubxType, const std::initializer_list<uint8_t>& lst)
{
  std::vector<uint8_t> str;
  for(const auto& a : lst)
    str.push_back(a);
  return buildUbxMessage(ubxClass, ubxType, str);
}

std::vector<uint8_t> buildUbxMessage(uint8_t ubxClass, uint8_t ubxType, const std::vector<uint8_t>& str)
{
  // 0xb5 0x62 class id len1 len2 payload cka ckb

  std::vector<uint8_t> msg;
  msg.push_back(0xb5);
  msg.push_back(0x62);
  msg.push_back(ubxClass); // CFG
  msg.push_back(ubxType); // MSG
  msg.push_back(str.size()); // len1
  msg.push_back(str.size()/256); // len2
  
  for(unsigned int n= 0 ; n < str.size(); ++n)
    msg.push_back(str[n]); 

  uint16_t csum = calcUbxChecksum(ubxClass, ubxType, {msg.cbegin() + 6, msg.cend()});

  msg.push_back(csum&0xff);
  msg.push_back(csum>>8);
  /*
  for(const auto& c : msg) {
    fmt::fprintf(stderr, "%02x ", (int)c);
  }
  fmt::fprintf(stderr,"\n");
  */
  return msg;
}

vector<uint8_t> getInavFromSFRBXMsg(const std::vector<uint8_t>& msg,
                                          vector<uint8_t>& reserved1,
                                          vector<uint8_t>& reserved2,
                                          vector<uint8_t>& sar,
                                          vector<uint8_t>& spare,
                                          vector<uint8_t>& crc, uint8_t* ssp)
{
  // byte order adjustment
  std::vector<uint8_t> payload;
  for(unsigned int i = 0 ; i < (msg.size() - 8) / 4; ++i)
    for(int j=1; j <= 4; ++j)
      payload.push_back(msg[8 + (i+1) * 4 -j]);

  /* test crc (4(pad) + 114 + 82 bits) */
  unsigned char crc_buff[26]={0};
  unsigned int i,j;
  for (i=0,j=  4;i<15;i++,j+=8) setbitu(crc_buff,j,8,getbitu(&*payload.cbegin()   ,i*8,8));
  for (i=0,j=118;i<11;i++,j+=8) setbitu(crc_buff,j,8,getbitu(&payload[0] + 16,i*8,8));
  if (rtk_crc24q(crc_buff,25) != getbitu(&payload[0] +16,82,24)) {
    cerr << "CRC mismatch, " << rtk_crc24q(crc_buff, 25) << " != " << getbitu(&payload[0]+16,82,24) <<endl;
    throw CRCMismatch();
  }

  crc.clear();
  for(i=0; i < 3; ++i)
    crc.push_back(getbitu(&payload[0] +16,82+i*8,8));

  if(ssp) {
    *ssp=getbitu(&payload[0]+16,82+24,8);
  }

  
  std::vector<uint8_t> inav;
  
  for (i=0,j=2; i<14; i++, j+=8)
    inav.push_back((unsigned char)getbitu(&payload[0]   ,j,8));
  for (i=0,j=2; i< 2; i++, j+=8)
    inav.push_back((unsigned char)getbitu(&payload[0]+16,j,8));

  reserved1.clear();
  for(i=0, j=18; i < 5 ; i++, j+=8)
    reserved1.push_back((unsigned char)getbitu(&payload[0] +16, j, 8));
  //  cerr<<"reserved1: "<<makeHexDump(reserved1)<<endl;

  sar.clear();
  for(i=0, j=58; i < 3 ; i++, j+=8) // you get 24 bits
    sar.push_back((unsigned char)getbitu(&payload[0] +16, j, 8));

  spare.clear();
  spare.push_back((unsigned char)getbitu(&payload[0]+16, 80, 2));

  reserved2.clear();
  reserved2.push_back((unsigned char)getbitu(&payload[0] +16, 106, 8));
  
  return inav;
}

vector<uint8_t> getFnavFromSFRBXMsg(const std::vector<uint8_t>& msg,
                                          vector<uint8_t>& crc)
{
  // byte order adjustment
  std::vector<uint8_t> payload;
  for(unsigned int i = 0 ; i < (msg.size() - 8) / 4; ++i)
    for(int j=1; j <= 4; ++j)
      payload.push_back(msg[8 + (i+1) * 4 -j]);

  // 
  
  // 214 bitsof payload
  // 2 bits padding, 214 bits payload, 24 bits ctc
  // 216 bits -> 27 bytes
  unsigned char crc_buff[27]={0};
  unsigned int i,j;
  for (i=0,j=  2;i<27;i++,j+=8) setbitu(crc_buff,j,8,getbitu(&payload[0]   ,i*8,8));

  if (rtk_crc24q(crc_buff,27) != getbitu(&payload[0], 214,24)) {
    cerr << "CRC mismatch, " << rtk_crc24q(crc_buff, 27) << " != " << getbitu(&payload[0], 214,24) <<endl;
    cerr << makeHexDump(payload) << " " << (int) getbitu(&payload[0], 0, 6) << endl;
    throw CRCMismatch();
  }
  //  cerr << "F/NAV CRC MATCHED!!"<<endl;
  
  crc.clear();
  for(i=0; i < 3; ++i)
    crc.push_back(getbitu(&payload[0], 214+i*8,8));

  
  std::vector<uint8_t> fnav;

  for (i=0,j=0; i<27; i++, j+=8)
    fnav.push_back((unsigned char)getbitu(&payload[0]   ,j,8));
  
  return fnav;
}



// XXX this should do the parity check
vector<uint8_t> getGPSFromSFRBXMsg(const std::vector<uint8_t>& msg)
{
  // byte order adjustment
  std::vector<uint8_t> payload;
  for(unsigned int i = 0 ; i < (msg.size() - 8) / 4; ++i)
    for(int j=1; j <= 4; ++j)
      payload.push_back( msg[8 + (i+1) * 4 -j]);


  return payload;
}

// note, this returns the fourth UBX specific word with derived data, feel free to ignore!
vector<uint8_t> getGlonassFromSFRBXMsg(const std::vector<uint8_t>& msg)
{
  // byte order adjustment
  std::vector<uint8_t> payload;
  for(unsigned int i = 0 ; i < (msg.size() - 8) / 4; ++i)
    for(int j=1; j <= 4; ++j)
      payload.push_back( msg[8 + (i+1) * 4 -j]);


  return payload;
}

// note, this returns the fourth UBX specific word with derived data, feel free to ignore!
vector<uint8_t> getBeidouFromSFRBXMsg(const std::vector<uint8_t>& msg)
{
  // byte order adjustment
  std::vector<uint8_t> payload;
  for(unsigned int i = 0 ; i < (msg.size() - 8) / 4; ++i)
    for(int j=1; j <= 4; ++j)
      payload.push_back( msg[8 + (i+1) * 4 -j]);


  return payload;
}

vector<uint8_t> getSBASFromSFRBXMsg(const std::vector<uint8_t>& msg)
{
  // byte order adjustment
  std::vector<uint8_t> payload;
  for(unsigned int i = 0 ; i < (msg.size() - 8) / 4; ++i)
    for(int j=1; j <= 4; ++j)
      payload.push_back( msg[8 + (i+1) * 4 -j]);

  
  return payload;
}
