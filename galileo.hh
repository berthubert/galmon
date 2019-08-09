#pragma once
#include <stdint.h>
#include <string>

bool getTOWFromInav(std::basic_string_view<uint8_t> inav, uint32_t *satTOW, uint16_t *wn);
