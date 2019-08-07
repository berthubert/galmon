#include <iostream>
#include "ubx.hh"
#include "fmt/format.h"
#include "fmt/printf.h"

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
