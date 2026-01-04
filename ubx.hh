#pragma once
#include <string>
#include <vector>
#include <cstdint>

uint16_t calcUbxChecksum(uint8_t ubxClass, uint8_t ubxType, const std::vector<uint8_t>& str);
std::vector<uint8_t> buildUbxMessage(uint8_t ubxClass, uint8_t ubxType, const std::vector<uint8_t>& str);

std::vector<uint8_t> buildUbxMessage(uint8_t ubxClass, uint8_t ubxType, const std::initializer_list<uint8_t>& str);

std::vector<uint8_t> getInavFromSFRBXMsg(const std::vector<uint8_t>& msg,
                                               std::vector<uint8_t>& reserved1,
                                               std::vector<uint8_t>& reserved2,
                                               std::vector<uint8_t>& sar,
                                               std::vector<uint8_t>& spare,
                                               std::vector<uint8_t>& crc, uint8_t* ssp=0);

std::vector<uint8_t> getFnavFromSFRBXMsg(const std::vector<uint8_t>& msg,
					 std::vector<uint8_t>& crc);

std::vector<uint8_t> getGPSFromSFRBXMsg(const std::vector<uint8_t>& msg);
std::vector<uint8_t> getGlonassFromSFRBXMsg(const std::vector<uint8_t>& msg);
std::vector<uint8_t> getBeidouFromSFRBXMsg(const std::vector<uint8_t>& msg);
std::vector<uint8_t> getSBASFromSFRBXMsg(const std::vector<uint8_t>& msg);
struct CRCMismatch{};

struct TrkSatStat
{
  int gnss;
  int sv;
  double dopplerHz;
  uint64_t tr;
};

std::vector<TrkSatStat> parseTrkMeas(const std::vector<uint8_t>& payload);
