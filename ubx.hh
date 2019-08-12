#include <string>
#include <vector>
uint16_t calcUbxChecksum(uint8_t ubxClass, uint8_t ubxType, std::basic_string_view<uint8_t> str);
std::basic_string<uint8_t> buildUbxMessage(uint8_t ubxClass, uint8_t ubxType, std::basic_string_view<uint8_t> str);

std::basic_string<uint8_t> buildUbxMessage(uint8_t ubxClass, uint8_t ubxType, const std::initializer_list<uint8_t>& str);

std::basic_string<uint8_t> getInavFromSFRBXMsg(std::basic_string_view<uint8_t> msg);
std::basic_string<uint8_t> getGPSFromSFRBXMsg(int sv, std::basic_string_view<uint8_t> msg);
struct CRCMismatch{};
