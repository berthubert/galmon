#include "beidou.hh"
#include "bits.hh"

std::basic_string<uint8_t> getCondensedBeidouMessage(std::basic_string_view<uint8_t> payload)
{
  uint8_t buffer[(26+9*22)/8];

  setbitu(buffer, 0, 26, getbitu(&payload[0], 2, 26));
  
  for(int w = 1 ; w < 10; ++w) {
    setbitu(buffer, 26+22*(w-1), 22, getbitu(&payload[0], 2 + w*32, 22));
  }

  return std::basic_string<uint8_t>(buffer, 28);
}


// the BeiDou ICD lists bits from bit 1
// In addition, they count the parity bits which we strip out
// this function converts their bit numbers to ours
// the first word has 4 parity bits, the rest has 8
int beidouBitconv(int their)
{
  int wordcount = their/30;
  
  if(!wordcount)
    return their - 1;
  
  return their - (1 + 4 +  (wordcount -1)*8);
}
