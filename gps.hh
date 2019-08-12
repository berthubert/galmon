#pragma once
#include <string>
#include "bits.hh"
#include <iostream>
std::basic_string<uint8_t> getCondensedGPSMessage(std::basic_string_view<uint8_t> payload);

// expects input as 24 bit read to to use messages, returns frame number
int parseGPSMessage(std::basic_string_view<uint8_t> cond, auto& out, uint8_t* pageptr=0)
{
  using namespace std;
  int frame = getbitu(&cond[0], 24+19, 3);
  // 10 * 4 bytes in payload now
  out.tow = 1.5*(getbitu(&cond[0], 24, 17)*4);
  //  cerr << "Preamble: "<<getbitu(&cond[0], 0, 8) <<", frame: "<< frame<<", truncated TOW: "<<out.tow<<endl;
  if(frame == 1) {
    // word 1, word 2 are TLM and HOW
    // 2 bits of padding on each word
    // word3:
    // 1-10:  WN
    // 11-12: Which codes 00 = invalid, 01 = P-code on, 10 = C/A code on, 11 invalid
    // 13-16: URA, 0-15 scale
    // 17-22: 0 is ok
    // 23-24: MSB of IODC

    // word 8:
    // 1-8: LSB of IODC
    // 9-24:

    out.wn = 2048 + getbitu(&cond[0], 2*24, 10);
    out.ura = getbitu(&cond[0], 2*24+12, 4);
    out.gpshealth = getbitu(&cond[0], 2*24+16, 6);
    
    //    cerr<<"GPS Week Number: "<< out.wn <<", URA: "<< (int)out.ura<<", health: "<<
    //      (int)out.gpshealth <<endl;

    out.af2 = getbits(&cond[0], 8*24, 8);           // * 2^-55
    out.af1 = getbits(&cond[0], 8*24 + 8, 16);      // * 2^-43
    out.af0 = getbits(&cond[0], 9*24, 22);          // * 2^-31
    out.t0c = getbits(&cond[0], 7*24 + 8, 16);      // * 16 
    //    cerr<<"t0c*16: "<<out.t0c*16<<", af2: "<< (int)out.af2 <<", af1: "<< out.af1 <<", af0: "<<
    //      out.af0 <<endl;
  }
  else if(frame == 2) {
    int iod = getbitu(&cond[0], 2*24, 8);
    auto& eph = out.getEph(iod);
    eph.words[2]=1;
    eph.t0e = getbits(&cond[0], 9*24, 16) * 16.0;  // WE SCALE THIS FOR THE USER!!
    //    cerr<<"IODe "<<(int)iod<<", t0e "<< eph.t0e << " = "<<  16* eph.t0e <<"s"<<endl;

    eph.e= getbitu(&cond[0], 5*24+16, 32);
    //    cerr<<"e: "<<ldexp(eph.e, -33)<<", ";

    
    // sqrt(A), 32 bits, 2^-19
    eph.sqrtA= getbitu(&cond[0], 7*24+ 16, 32);
    //    double sqrtA=ldexp(eph.sqrtA, -19);           // 2^-19
    //    cerr<<"Radius: "<<sqrtA*sqrtA<<endl;

    eph.crs = getbits(&cond[0], 2*24 + 8, 16);  // 2^-5 meters
    eph.deltan = getbits(&cond[0], 3*24, 16);  // 2^-43 semi-circles/s
    eph.m0 = getbits(&cond[0], 3*24+16, 32);  // 2^-31 semi-circles

    eph.cuc = getbits(&cond[0], 5*24, 16);    // 2^-29 RADIANS
    eph.cus = getbits(&cond[0], 7*24, 16);    // 2^-29 RADIANS
    out.checkCompleteAndClean(iod);
  }
  else if(frame == 3) {
    int iod = getbitu(&cond[0], 9*24, 8);
    auto& eph = out.getEph(iod);
    eph.words[3]=1;
    eph.cic = getbits(&cond[0], 2*24, 16);   // 2^-29  RADIANS
    eph.omega0 = getbits(&cond[0], 2*24 + 16, 32);   // 2^-31 semi-circles
    eph.cis = getbits(&cond[0], 4*24, 16);        // 2^-29  radians
    eph.i0 = getbits(&cond[0], 4*24 + 16, 32);    // 2^-31, semicircles

    eph.crc = getbits(&cond[0], 6*24, 16);            // 2^-5, meters
    eph.omega = getbits(&cond[0], 6*24+16, 32);       // 2^-31, semi-circles

    eph.omegadot = getbits(&cond[0], 8*24, 24);       // 2^-43, semi-circles/s
    eph.idot = getbits(&cond[0], 9*24+8, 14);         // 2^-43, semi-cirlces/s
    out.checkCompleteAndClean(iod);
  }
  else if(frame == 4) { // this is a carousel frame
    int page = getbitu(&cond[0], 2*24 + 2, 6);
    if(pageptr)
      *pageptr=0;
    //    cerr<<"Frame 4, page "<<page;
    if(page == 56) { // 56 is the new 18 somehow?
      if(pageptr)
        *pageptr=18;
      out.a0 = getbits(&cond[0], 6*24 , 32); // 2^-30
      out.a1 = getbits(&cond[0], 5*24 , 24); // 2^-50

      out.t0t = getbitu(&cond[0], 7*24 + 8, 8) * 4096; // WE SCALE THIS FOR THE USER!
      out.wn0t = getbitu(&cond[0], 7*24  + 16, 8);
      out.dtLS = getbits(&cond[0], 8*24, 8);
      out.dtLSF = getbits(&cond[0], 9*24, 8);
      
      //      cerr<<": a0: "<<out.a0<<", a1: "<<out.a1<<", t0t: "<< out.t0t * (1<<12) <<", wn0t: "<< out.wn0t<<", rough offset: "<<ldexp(out.a0, -30)<<endl;
      //      cerr<<"deltaTLS: "<< (int)out.dtLS<<", post "<< (int)out.dtLSF<<endl;
    }
    //else cerr<<endl;
    // page 18 contains UTC -> 56
    // page 25 -> 63
    // 2-10    -> 25 -> 32 ??
  }
  else if(frame == 5) { // this is a caroussel frame
    //    cerr<<"Frame 5, SV: "<<getbitu(&cond[0], 2*32 + 2 +2, 6)<<endl;
  }
  return frame;
}
