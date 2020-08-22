#pragma once
#include <string>
#include <vector>
uint16_t calcUbxChecksum(uint8_t ubxClass, uint8_t ubxType, std::basic_string_view<uint8_t> str);
std::basic_string<uint8_t> buildUbxMessage(uint8_t ubxClass, uint8_t ubxType, std::basic_string_view<uint8_t> str);

std::basic_string<uint8_t> buildUbxMessage(uint8_t ubxClass, uint8_t ubxType, const std::initializer_list<uint8_t>& str);

std::basic_string<uint8_t> getInavFromSFRBXMsg(std::basic_string_view<uint8_t> msg,
                                               std::basic_string<uint8_t>& reserved1,
                                               std::basic_string<uint8_t>& reserved2,
                                               std::basic_string<uint8_t>& sar,
                                               std::basic_string<uint8_t>& spare,
                                               std::basic_string<uint8_t>& crc);

std::basic_string<uint8_t> getGPSFromSFRBXMsg(std::basic_string_view<uint8_t> msg);
std::basic_string<uint8_t> getGlonassFromSFRBXMsg(std::basic_string_view<uint8_t> msg);
std::basic_string<uint8_t> getBeidouFromSFRBXMsg(std::basic_string_view<uint8_t> msg);
std::basic_string<uint8_t> getSBASFromSFRBXMsg(std::basic_string_view<uint8_t> msg);
struct CRCMismatch{};

struct TrkSatStat
{
  int gnss;
  int sv;
  double dopplerHz;
  uint64_t tr;
};

std::vector<TrkSatStat> parseTrkMeas(std::basic_string_view<uint8_t> payload);
