#include <stdio.h>
#include <string>
#include <iostream>
#include <arpa/inet.h>
#include "fmt/format.h"
#include "fmt/printf.h"
#include <fstream>

using namespace std;

struct EofException{};

uint8_t getUint8()
{
  int c;
  c = fgetc(stdin);
  if(c == -1)
    throw EofException();
  return (uint8_t) c;
}

uint16_t getUint16()
{
  uint16_t ret;
  int res = fread(&ret, 1, sizeof(ret), stdin);
  if(res != sizeof(ret))
    throw EofException();
  //  ret = ntohs(ret);
  return ret;
}

/* lovingly lifted from RTKLIB */
unsigned int getbitu(const unsigned char *buff, int pos, int len)
{
  unsigned int bits=0;
  int i;
  for (i=pos;i<pos+len;i++) bits=(bits<<1)+((buff[i/8]>>(7-i%8))&1u);
  return bits;
}


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

int main()
try
{
  unsigned int tow=0, wn=0;
  ofstream csv("iod.csv");
  ofstream csv2("toe.csv");
  csv<<"timestamp sv iod sisa"<<endl;
  csv2<<"timestamp sv tow toe"<<endl;
  for(;;) {
    auto c = getUint8();
    if(c != 0xb5) {
      cout << (char)c;
      continue;
    }
    c = getUint8();
    if(c != 0x62) {
      ungetc(c, stdin); // might be 0xb5
      continue;
    }

    // if we are here, just had ubx header

    uint8_t ubxClass = getUint8();
    uint8_t ubxType = getUint8();
    uint16_t msgLen = getUint16();
    
    cout <<"Had an ubx message of class "<<(int) ubxClass <<" and type "<< (int) ubxType << " of " << msgLen <<" bytes"<<endl;

    std::basic_string<uint8_t> msg;
    msg.reserve(msgLen);
    for(int n=0; n < msgLen; ++n)
      msg.append(1, getUint8());

    uint16_t ubxChecksum = getUint16();
    if(ubxChecksum != calcUbxChecksum(ubxClass, ubxType, msg)) {
      cout<<"Checksum: "<<ubxChecksum<< ", calculated: "<<
        calcUbxChecksum(ubxClass, ubxType, msg)<<endl;
    }

    if(ubxClass == 2 && ubxType == 89) { // SAR
      cout<<"SAR: sv = "<< (int)msg[2] <<" ";
      for(int n=4; n < 12; ++n)
        cout << (int)msg[n]<<" ";
      cout << "Type: "<< msg[12] <<endl;
      cout<<endl;
    }
    if(ubxClass == 2 && ubxType == 19) { //UBX-RXM-SFRBX
      cout<<"SFRBX GNSSID "<< (int)msg[0]<<", SV "<< (int)msg[1];
      cout<<" words "<< (int)msg[4]<<", version "<< (int)msg[6];
      cout<<endl;
      if(msg[0] != 2)
        continue;
      // 7 is reserved

      //      cout << ((msg[8]&128) ? "Even " : "Odd ");
      //      cout << ((msg[8]&64) ? "Alert " : "Nominal ");
      unsigned int wtype = (int)(msg[11] & (~(64+128)));
      unsigned int sv = (int)msg[1];
      cout << "Word type "<< (int)(msg[11] & (~(64+128))) <<" SV " << (int)msg[1]<<endl;
      for(unsigned int n = 8; n < msg.size() ; ++n) {
        fmt::printf("%02x ", msg[n]);
      }
      cout<<"\n";
      std::basic_string<uint8_t> payload;
      for(unsigned int i = 0 ; i < (msg.size() - 8) / 4; ++i)
        for(int j=1; j <= 4; ++j)
          payload.append(1, msg[8 + (i+1) * 4 -j]);
      
      for(auto& c : payload)
        fmt::printf("%02x ", c);
            
      cout<<endl;

      std::basic_string<uint8_t> inav;
      unsigned int i,j;
      for (i=0,j=2; i<14; i++, j+=8)
        inav.append(1, (unsigned char)getbitu(payload.c_str()   ,j,8));
      for (i=0,j=2; i< 2; i++, j+=8)
        inav.append(1, (unsigned char)getbitu(payload.c_str()+16,j,8));

      cout<<"inav for "<<wtype<<" for sv "<<sv<<": ";
      for(auto& c : inav)
        fmt::printf("%02x ", c);

      cout<<endl;
      if(wtype == 1 && tow) {
        time_t t = 935280000 + wn * 7*86400 + tow;
        csv2<<t<<" "<<sv<<" " << tow<<" "<<60*getbitu(inav.c_str(), 16, 14)<<"\n";
      }
      if(wtype == 3 && tow) {
        time_t t = 935280000 + wn * 7*86400 + tow;
        csv<<t<<" "<<sv<<" " << getbitu(inav.c_str(), 6, 10)<<" " << getbitu(inav.c_str(), 120, 8) << endl;
      }
      if(wtype==5) {
        tow = getbitu(inav.c_str(), 85, 20);
        wn = getbitu(inav.c_str(), 73, 12);
        cout<<"gst wn " << wn << " tow "<<tow<<endl;
        unsigned int e5bhs = getbitu(inav.c_str(), 67, 2);
        unsigned int e1bhs = getbitu(inav.c_str(), 69, 2);
        unsigned int e5bdvs = getbitu(inav.c_str(), 71, 1);
        unsigned int e1bdvs = getbitu(inav.c_str(), 72, 1);
        cout<<"health: sv "<< sv<<" " << e5bhs << " " << e1bhs <<" " << e5bdvs<<" " << e1bdvs <<endl;
      }
      

      
      
    }
  }
}
 catch(EofException& e)
   {}
