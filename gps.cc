#include "gps.hh"

// this strips out spare bits + parity, and leaves 10 clean 24 bit words
std::basic_string<uint8_t> getCondensedGPSMessage(std::basic_string_view<uint8_t> payload)
{
  uint8_t buffer[10*24/8];

  for(int w = 0 ; w < 10; ++w) {
    setbitu(buffer, 24*w, 24, getbitu(&payload[0], 2 + w*32, 24));
  }

  return std::basic_string<uint8_t>(buffer, 30);
  
}

// expects input as 24 bit read to to use messages, returns frame number
int GPSState::parseGPSMessage(std::basic_string_view<uint8_t> cond, uint8_t* pageptr)
{
  using namespace std;
  int frame = getbitu(&cond[0], 24+19, 3);
  // 10 * 4 bytes in payload now
  tow = 1.5*(getbitu(&cond[0], 24, 17)*4);
  //  cerr << "Preamble: "<<getbitu(&cond[0], 0, 8) <<", frame: "<< frame<<", truncated TOW: "<<tow<<endl;
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

    wn = 2048 + getbitu(&cond[0], 2*24, 10);
    ura = getbitu(&cond[0], 2*24+12, 4);
    gpshealth = getbitu(&cond[0], 2*24+16, 6);
    
    //    cerr<<"GPS Week Number: "<< wn <<", URA: "<< (int)ura<<", health: "<<
    //      (int)gpshealth <<endl;

    af2 = getbits(&cond[0], 8*24, 8);           // * 2^-55
    af1 = getbits(&cond[0], 8*24 + 8, 16);      // * 2^-43
    af0 = getbits(&cond[0], 9*24, 22);          // * 2^-31
    t0c = getbitu(&cond[0], 7*24 + 8, 16);      // * 16
    //    cerr<<"t0c*16: "<<t0c*16<<", af2: "<< (int)af2 <<", af1: "<< af1 <<", af0: "<<
    //      af0 <<endl;
  }
  else if(frame == 2) {
    gpsiod = getbitu(&cond[0], 2*24, 8);
    t0e = getbitu(&cond[0], 9*24, 16) * 16.0;  // WE SCALE THIS FOR THE USER!!
    //    cerr<<"IODe "<<(int)iod<<", t0e "<< t0e << " = "<<  16* t0e <<"s"<<endl;

    e= getbitu(&cond[0], 5*24+16, 32);
    //    cerr<<"e: "<<ldexp(e, -33)<<", ";

    
    // sqrt(A), 32 bits, 2^-19
    sqrtA= getbitu(&cond[0], 7*24+ 16, 32);
    //    double sqrtA=ldexp(sqrtA, -19);           // 2^-19
    //    cerr<<"Radius: "<<sqrtA*sqrtA<<endl;

    crs = getbits(&cond[0], 2*24 + 8, 16);  // 2^-5 meters
    deltan = getbits(&cond[0], 3*24, 16);  // 2^-43 semi-circles/s
    m0 = getbits(&cond[0], 3*24+16, 32);  // 2^-31 semi-circles

    cuc = getbits(&cond[0], 5*24, 16);    // 2^-29 RADIANS
    cus = getbits(&cond[0], 7*24, 16);    // 2^-29 RADIANS
  }
  else if(frame == 3) {
    gpsiod = getbitu(&cond[0], 9*24, 8);
    cic = getbits(&cond[0], 2*24, 16);   // 2^-29  RADIANS
    omega0 = getbits(&cond[0], 2*24 + 16, 32);   // 2^-31 semi-circles
    cis = getbits(&cond[0], 4*24, 16);        // 2^-29  radians
    i0 = getbits(&cond[0], 4*24 + 16, 32);    // 2^-31, semicircles

    crc = getbits(&cond[0], 6*24, 16);            // 2^-5, meters
    omega = getbits(&cond[0], 6*24+16, 32);       // 2^-31, semi-circles

    omegadot = getbits(&cond[0], 8*24, 24);       // 2^-43, semi-circles/s
    idot = getbits(&cond[0], 9*24+8, 14);         // 2^-43, semi-cirlces/s
  }
  else if(frame == 4) { // this is a carousel frame
    int page = getbitu(&cond[0], 2*24 + 2, 6);
    if(pageptr)
      *pageptr=0;
    //    cerr<<"Frame 4, page "<<page;
    if(page == 56) { // 56 is the new 18 somehow? See table 20-V of the ICD
      if(pageptr)
        *pageptr=18;
      a0 = getbits(&cond[0], 6*24 , 32); // 2^-30
      a1 = getbits(&cond[0], 5*24 , 24); // 2^-50

      t0t = getbitu(&cond[0], 7*24 + 8, 8) * 4096; // WE SCALE THIS FOR THE USER!
      wn0t = getbitu(&cond[0], 7*24  + 16, 8);
      dtLS = getbits(&cond[0], 8*24, 8);
      dtLSF = getbits(&cond[0], 9*24, 8);
      
      //      cerr<<": a0: "<<a0<<", a1: "<<a1<<", t0t: "<< t0t * (1<<12) <<", wn0t: "<< wn0t<<", rough offset: "<<ldexp(a0, -30)<<endl;
      //      cerr<<"deltaTLS: "<< (int)dtLS<<", post "<< (int)dtLSF<<endl;
      return frame; // otherwise pageptr gets overwritten below
    }
    //else cerr<<endl;
    // page 18 contains UTC -> 56
    // page 25 -> 63
    // 2-10    -> 25 -> 32 ??
  }

  if(frame == 5 || frame==4) { // this is a caroussel frame
    gpsalma.dataid = getbitu(&cond[0], 2*24, 2);
    gpsalma.sv = getbitu(&cond[0], 2*24+2, 6);

    if(pageptr)
      *pageptr= gpsalma.sv;

    
    gpsalma.e = getbitu(&cond[0], 2*24 + 8, 16);
    gpsalma.t0a = getbitu(&cond[0], 3*24, 8);
    gpsalma.deltai = getbits(&cond[0], 3*24 +8 , 16);
    gpsalma.omegadot = getbits(&cond[0], 4*24, 16);
    gpsalma.health = getbitu(&cond[0], 4*24 +16, 8);
    gpsalma.sqrtA = getbitu(&cond[0], 5*24, 24);
    gpsalma.Omega0 = getbits(&cond[0], 6*24, 24);
    gpsalma.omega = getbits(&cond[0], 7*24, 24);
    gpsalma.M0 = getbits(&cond[0], 8*24, 24);
    gpsalma.af0 = (getbits(&cond[0], 9*24, 8) << 3) + getbits(&cond[0], 9*24 +19, 3);
    gpsalma.af1 = getbits(&cond[0], 9*24 + 8, 11);
    //    cerr<<"Frame 5, SV: "<<getbitu(&cond[0], 2*32 + 2 +2, 6)<<endl;
  }
  return frame;
}
