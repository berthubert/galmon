#include <string>
#include "fmt/format.h"
#include "fmt/printf.h"
#include <vector>
#include <map>
#include <iostream>
#include "ubx.hh"
using namespace std;
/* TRK-MEAS

   2                U1    Number of channels tracked
              ?
   8                U4    Poweron counter milliseconds
   12               U4    "session time counter" milliseconds

   104 + n*56 + 0   U1    Channel (?)
   104 + n*56 + 1   U1    Quality indicator 0 idle, 1 search, 2 acquired, 3 unsusable, 4 code lock, 5/6/7 carrier lock)
              + 2   ?
              + 3   ?
   104 + n*56 + 4   U1    GNSS (?)
   104 + n*56 + 5   U1    Satellite ID
              ?
   104 + n*56 + 7   U1    GLONASS Frequency
   104 + n*56 + 8   U1    Tracking status
              ?
              + 16  U1    Code lock "count"
              + 17  U1    Phase lock "count"
              ?
              + 20  U2    256*dB
              ?
   104 + n*56 + 24  I8    Transmission time, in units of 2^-32 milliseconds (?)  // ts=I8(p+24)*P2_32/1000.0;

              + 32  I8    "ADR" in units of 2^-32, plus 0.5 if 'bit' 0x40 is set in tracking status
              + 40  I4    Doppler in units of 2^-10 deca-hertz (?)  // I4(p+40)*P2_10*10.0;
              + 44        ? 
              ..
              + 56  
*/

extern "C" {
  int ubxdecrypt(const unsigned char crypto[16], unsigned char plaintext[16]) __attribute__((weak));
  int ubxdecrypt(const unsigned char crypto[16], unsigned char plaintext[16])
  {
    return -1;
  }
}

vector<TrkSatStat> parseTrkMeas(std::basic_string_view<uint8_t> payload)
{
  uint8_t plainchunk[16];
  std::basic_string<uint8_t> plaintext;
  /*
  cerr<<"payload.size(): "<<payload.size()<<", mod "<<(payload.size()-4)%16<<", " << payload[2]+256*payload[3]<<": ";
  for(int n=0; n < 4; ++n)
    cerr<<fmt::sprintf("%02x ", (unsigned int) payload[n]);
  cerr<<endl;
  */

  vector<TrkSatStat> ret;
  for(unsigned int n=4 ; n < payload.size(); n+=16) {
    if(ubxdecrypt(&payload[n], plainchunk) < 0)
      return ret;
    plaintext.append(plainchunk, plainchunk+16);
  }
  /*
  int msgsize = (payload[2]+256*payload[3]) - 6;
  cerr<<"msgsize: "<<msgsize<<", assumed SVs: "<<(int)plaintext[8]<<", per "<<(msgsize-104.0)/(int)plaintext[8]<<endl;
  */

  int64_t maxtr=0;
  for(int n = 0 ; n < plaintext[8]; ++n) {
    int offset = 110+n*56;
    int64_t tr;
    memcpy(&tr, &plaintext[offset+24], 8);
    if(tr > maxtr)
      maxtr = tr;
  }


  
  for(int n = 0 ; n < plaintext[8]; ++n) {
    int offset = 110+n*56;
    int32_t rdoppler;
    int64_t tr;
    memcpy(&rdoppler, &plaintext[offset+40], 4);
    
    memcpy(&tr, &plaintext[offset+24], 8);

    int gnssid = plaintext[offset+4];
    int sv = plaintext[offset+5];
    double doppler = ldexp(1.0*rdoppler,-10)*10;
    int plcount = (unsigned int)plaintext[offset+17];
    /*    
    int trkstat = plaintext[offset+8];
    cerr<<" gnssid " << gnssid <<" sv "<< sv;
    cerr<<" qi "<<(unsigned int)plaintext[offset+1] <<" c "<<(unsigned int)plaintext[offset];
    cerr<<" ? "<<(unsigned int)plaintext[offset+2] << " ? " <<(unsigned int)plaintext[offset+3];
    cerr<<" trkstat " << trkstat<<" db " << (unsigned int) plaintext[offset+21] << " doppler "<<
      doppler << "Hz tr " << tr<< " cl-count " << (unsigned int)plaintext[offset+16]<< " pl-count " << plcount;
    cerr<<" tau " << ldexp((tr - maxtr), -32)/1000.0;
    cerr<<" pr " << 3e5*ldexp((tr - maxtr), -32)/1000.0;
    cerr<<endl;
    */
    if((gnssid== 0 || gnssid == 2) && plcount > 2) {
      struct TrkSatStat tss;
      tss.sv = sv;
      tss.gnss = gnssid;
      tss.dopplerHz = doppler;
      tss.tr = tr;
      ret.push_back(tss);
    }
   
  }
  /*
  for(int n = 0 ; n < plaintext[8]; ++n) {
    int offset = 110+n*56;
    cerr<<" gnssid " << (unsigned int)plaintext[offset+4]<<" sv "<<(unsigned int) plaintext[offset+5]<<endl;
    for(int j = 0; j < 56; ++j) {
      cerr<< fmt::sprintf("%02x ", (unsigned int)plaintext[offset+j]);
      if((j % 16)==15)
        cerr<<endl;
    }
    cerr<<endl;
  }
  */
  return ret;
}
