#include "beidou.hh"
#include "bits.hh"
#include <iostream>

using namespace std;

// with immense gratitude to https://stackoverflow.com/questions/24612436/how-to-check-a-bch15-11-1-code-checksum-for-bds-beidou-satellite-system

static int checkbds(int bits)
{
  static int const at[15] = {1, 2, 4, 8, 3, 6, 12, 11, 5, 10, 7, 14, 15, 13, 9};
  int s, i, j;

  for(i = 1; i <= 2; i++)
  {
    s = 0;
    for(j = 0; j < 15; j++)
    {
      if(bits & (1<<j))
      {
        s ^= at[(i * j) % 15];
      }
    }
    if(s != 0)
    {
      return 0;
    }
  }
  return 1;
}

std::basic_string<uint8_t> getCondensedBeidouMessage(std::basic_string_view<uint8_t> payload)
{

  // payload consists of 32 bit words where we have to ignore the first 2 bits of every word

  int chunk = getbitu(&payload[0], 17, 15);
  //  cout <<"w0 checkbds(chunk0): "<<checkbds(chunk) << endl;
  if(!checkbds(chunk))
    throw std::runtime_error("Beidou checksum error chunk0");
  
  uint8_t buffer[(26+9*22)/8];

  setbitu(buffer, 0, 26, getbitu(&payload[0], 2, 26));
  
  for(int w = 1 ; w < 10; ++w) {
    int chunk1 = getbitu(&payload[0], 2+w*32, 11);
    int chunk2 = getbitu(&payload[0], 2+w*32+11, 11);
    int parity1 = getbitu(&payload[0], 2+w*32+22, 4);
    int parity2 = getbitu(&payload[0], 2+w*32+22+4, 4);
    chunk1 = (chunk1<<4) | parity1;
    chunk2 = (chunk2<<4) | parity2;
    //cout <<"w"<<w<<" checkbds(chunk1): "<<checkbds(chunk1) << ", checkbds(chunk2): "<<checkbds(chunk2)<<endl;
    if(!checkbds(chunk1) || !checkbds(chunk2))
      throw std::runtime_error("Beidou checksum error");
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


bool processBeidouAlmanac(const BeidouMessage& bm, struct BeidouAlmanacEntry& bae)
{
  bae.alma = bm.alma;
  int pageno = bm.alma.pageno;

  if(bm.fraid == 5 && pageno >= 11 && pageno <= 23) {
    /*
    cout<<" AmEpID "<< bm.alma.AmEpID;
    cout << " AmID "<<bm.alma.AmID; */
    if(bm.alma.AmEpID != 3) {
      //      cout<<" skipping page because AmEpID tells us to "<<endl;
      return false;
    }
  }
  if(ephAge(bm.sow, bm.alma.getT0e()) < 0) {
    //    cout <<" almanac too old t0a "<<bm.alma.getT0e()<<" sow "<<bm.sow << " days " << 1.0*(bm.alma.getT0e()-bm.sow)/86400<< endl;
    return false;
  }
  if(bm.alma.sqrtA == 0) {
    //    cout<<"sqrtA==0 - likely not a present satellite "<<endl;
    return false;
  }
        
  Point sat;
  if(bm.fraid ==4 && pageno <= 5)
    bae.alma.geo=true;
  else
    bae.alma.geo=false;
  int offset = 0;
  if(bm.fraid == 4)
    offset = 0;
  else if(bm.fraid == 5 && pageno <=6)
    offset = 24;
  else if(bm.fraid == 5 && bm.alma.AmID ==1)
    offset = 20;
  else if(bm.fraid == 5 && bm.alma.AmID ==2)
    offset = 34;
  else if(bm.fraid == 5 && bm.alma.AmID ==3)
    offset = 46;

  bae.sv = pageno+offset;
  //  cout<<" eff-id "<<bae.sv;
  if(bae.sv == 59)
    bae.alma.geo = true;
  return true;
}
      


