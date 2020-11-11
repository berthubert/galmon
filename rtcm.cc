#include "rtcm.hh"
#include "bits.hh"
#include <iostream>
#include <string.h>
using namespace std;

void RTCMMessage::parse(const std::string& str)
{
  memset(&d_gm, 0, sizeof(d_gm));
  auto gbu=[&str](int offset, int bits) {
    return getbitu((const unsigned char*)str.c_str(), offset, bits);
  };
  auto gbum=[&str](int& offset, int bits) {
    unsigned int ret = getbitu((const unsigned char*)str.c_str(), offset, bits);
    offset += bits;
    return ret;
  };
  
  auto gbs=[&str](int offset, int bits) {
    return getbits((const unsigned char*)str.c_str(), offset, bits);
  };
  
  auto gbsm=[&str](int& offset, int bits) {
    int ret = getbits((const unsigned char*)str.c_str(), offset, bits);
    offset += bits;
    return ret;
  };
  
  
  type = gbu(0, 12);
  //  cout<<"Message number: "<<type << " of size "<<str.size()<<"\n";
  if(type == 1057 || type == 1240) {
    d_ephs.clear();
    int stride;
    int iodlen;
    if(type == 1057) { // GPS
      stride = 135;
      iodlen=8;
    }
    else { // Galileo
      stride=137;
      iodlen=10;
    }
    
    int sats = gbu(62, 6);
    sow = gbu(12, 20);  // this is DF385
    udi = gbu(32, 4);
    mmi = gbu(36, 1);
    reference = gbu(37,1);
    ssrIOD = gbu(38,4);
    ssrProvider = gbu(42, 16);
    ssrSolution = gbu(58, 4);
    
    //    cout <<" sow "<< sow <<" sats "<<sats<<" update interval " << udi <<" mmi " << mmi;
    //    cout <<" reference "<< reference << " iod-ssr "<< ssrIOD << " ssr-provider " << ssrProvider << " ssr-solution ";
    //    cout<< ssrSolution <<":\n";
    
    for(int n = 0; n < sats; ++n) {
      EphemerisDelta ed;
      
      int off = 68+stride*n;
      ed.radial = gbs(off+     iodlen + 6, 22) * 0.1;   // we store this in millimeters
      ed.along = gbs(off+ iodlen+ 28, 20) * 0.4;
      ed.cross = gbs(off+ iodlen+48, 20) * 0.4;
      
      ed.dradial = gbs(off +   iodlen+  68, 21) * 0.001;   // we store this in mm/s
      ed.dalong = gbs(off + iodlen + 89, 19) * 0.004;
      ed.dcross = gbs(off + iodlen +108, 19) * 0.004;
      ed.iod = gbu(off +6, iodlen);
      ed.sow = sow;
      ed.udi = udi;
      if(type == 1057) {
        ed.id.gnss = 0;
        ed.id.sigid = 0;
      }
      else if(type == 1240) {
        ed.id.gnss = 2;
        ed.id.sigid = 1;
      }

      ed.id.sv = gbu(off + 0, 6);
      //      cout<<"  "<<makeSatIDName(ed.id)<<" iode "<< ed.iod<<" ("<< ed.radial<<", "<<ed.along<<", "<<ed.cross<<") mm -> (";
      //      cout<< ed.dradial<<", "<<ed.dalong<<", "<<ed.dcross<< ") mm/s\n";
      d_ephs.push_back(ed);
    }
  }
  else if(type == 1058 || type == 1241) {
    d_clocks.clear();
    int sats = gbu(61, 6);
    sow = gbu(12, 20);
    udi = gbu(32, 4);
    mmi = gbu(36, 1);
    ssrIOD = gbu(37, 4);
    ssrProvider = gbu(41, 16);
    ssrSolution=gbu(57, 4);
    
    //    cout <<" sow "<< sow <<" sats "<<sats<<" update interval " << udi <<" mmi " << mmi;
    //    cout << " iod-ssr "<< ssrIOD << " ssr-provider " << ssrProvider << " ssr-solution ";
    //    cout<< ssrSolution <<":\n";

    for(int n = 0; n < sats; ++n) {
      ClockDelta cd;
      cd.sow = sow;
      cd.udi = udi;
      if(type == 1058) {
        cd.id.gnss = 0;
        cd.id.sigid = 0;
      }
      else if(type == 1241) {
        cd.id.gnss = 2;
        cd.id.sigid = 1;
      }

      int off = 67+76*n;
      cd.id.sv = gbu(off +0, 6);

      /*
C0 polynomial coefficient for correction of broadcast satellite clock.
The reference time t0 is Epoch Time (DF385, DF386) plus 1⁄2 SSR
Update Interval. The reference time t0 for SSR Update Interval “0” is
Epoch Time

DF 385: Full seconds since the beginning of the GPS week

      */
      
      cd.dclock0 = gbs(off + 6, 22)*1e-4;     // in 0.1 mm, this converts to meters
      cd.dclock1 = gbs(off + 28, 21)*1e-6;    // meter/s
      cd.dclock2 = gbs(off + 49, 27)*2e-8;    // meter/s^2
      d_clocks.push_back(cd);
      //      cout<<"  "<< makeSatIDName(cd.id)<<" ";
      //      cout<< cd.dclock0 <<" ";
      //      cout<< cd.dclock1 <<" ";
      //      cout<< cd.dclock2 << endl;
    }
  }
  else if(type == 1060 || type == 1243) { // combined
    int sow = gbu(12, 20);
    int udi = gbu(32, 4);
    //    int mmi = gbu(36, 1);
    //    int srd = gbu(37, 1);
    ssrIOD = gbu(38, 4);
    ssrProvider = gbu(42, 16);
    ssrSolution=gbu(58, 4);
    unsigned int numsats=gbu(62, 6);

    int offset=68;
    d_ephs.clear();
    d_clocks.clear();
    int iodlen = type == 1060 ? 8 : 10; 
    for(unsigned int n=0; n < numsats; ++n) {
      ClockDelta cd;
      EphemerisDelta ed;

      int off = offset + n*(197 + iodlen);
      cd.sow = ed.sow = sow;
      cd.udi = ed.udi = udi;
      cd.id.gnss = (type == 1060) ? 0 : 2; // GPS or Galileo
      cd.id.sv = gbu(off + 0, 6);
      cd.id.sigid = (type == 1060) ? 0 : 1;

      ed.id = cd.id;
      ed.iod =    gbu(off +  6,  iodlen);
      int shift = iodlen - 8;
      ed.radial = gbs(off + 14 + shift,  22 ) * 0.1;   // we store this in millimeters
      ed.along =  gbs(off + 36 + shift,  20 ) * 0.4;
      ed.cross =  gbs(off + 56 + shift,  20 ) * 0.4;
      
      ed.dradial= gbs(off + 76 + shift,  21) * 0.001;   // we store this in mm/s
      ed.dalong = gbs(off + 97 + shift,  19) * 0.004;
      ed.dcross = gbs(off +116 + shift,  19) * 0.004;

      d_ephs.push_back(ed);
      cd.iod = ed.iod;
      cd.dclock0 = gbs(off + 135 + shift,  22)*1e-4;     // in 0.1 mm, this converts to meters
      cd.dclock1 = gbs(off + 157 + shift,  21)*1e-6;    // meter/s
      cd.dclock2 = gbs(off + 178 + shift,  27)*2e-8;    // meter/s^2
      // 205 / 207
      d_clocks.push_back(cd);
    }
  }
  else if(type == 1045 || type == 1046) { // F/NAV or I/NAV respectively ephemeris
    int off=12;
    d_sv = gbum(off, 6);
    d_gm.wn = gbum(off, 12);
    d_gm.iodnav = gbum(off, 10);
    d_gm.sisa = gbum(off, 8);
    d_gm.idot = gbsm(off, 14);
    d_gm.t0c = gbum(off, 14);
    d_gm.af2 = gbsm(off, 6);
    d_gm.af1 = gbsm(off, 21);
    d_gm.af0 = gbsm(off, 31);
    //
    d_gm.crs = gbsm(off, 16);
    d_gm.deltan = gbsm(off, 16);
    d_gm.m0 = gbsm(off, 32);
    d_gm.cuc = gbsm(off, 16);
    d_gm.e = gbum(off, 32);
    d_gm.cus = gbsm(off, 16);
    d_gm.sqrtA = gbum(off, 32);
    d_gm.t0e = gbum(off, 14); 
    //

    d_gm.cic = gbsm(off, 16);
    d_gm.omega0 = gbsm(off, 32);
    d_gm.cis = gbsm(off, 16);
    d_gm.i0 = gbsm(off, 32);
    d_gm.crc = gbsm(off, 16);
    d_gm.omega = gbsm(off, 32);
    d_gm.omegadot = gbsm(off, 24);

    //     16 + 16 + 32 + 16 + 32 + 16 + 32 + 14 +
    //     crs deln  M0  cuc   e   cus  sqrA toe  cic OM0 cis i0   crc  omeg omegdot  
    //    off +=                                         16+ 32 +16 + 32 + 16 + 32 +24;
    d_gm.BGDE1E5a = gbsm(off, 10);
    if(type == 1046) { // I/NAV
      d_gm.BGDE1E5b = gbsm(off, 10);
    }
    else {
      d_gm.BGDE1E5b = 9999999;
    }

    // thank you RTKLIB:
#if 0
    setbitu(rtcm->buff,i,12,1045     ); i+=12;
    setbitu(rtcm->buff,i, 6,prn      ); i+= 6;
    setbitu(rtcm->buff,i,12,week     ); i+=12;
    setbitu(rtcm->buff,i,10,eph->iode); i+=10;
    setbitu(rtcm->buff,i, 8,eph->sva ); i+= 8;
    setbits(rtcm->buff,i,14,idot     ); i+=14;
    setbitu(rtcm->buff,i,14,toc      ); i+=14;
    setbits(rtcm->buff,i, 6,af2      ); i+= 6;
    setbits(rtcm->buff,i,21,af1      ); i+=21;
    setbits(rtcm->buff,i,31,af0      ); i+=31;

    setbits(rtcm->buff,i,16,crs      ); i+=16;
    setbits(rtcm->buff,i,16,deln     ); i+=16;
    setbits(rtcm->buff,i,32,M0       ); i+=32;
    setbits(rtcm->buff,i,16,cuc      ); i+=16;
    setbitu(rtcm->buff,i,32,e        ); i+=32;
    setbits(rtcm->buff,i,16,cus      ); i+=16;
    setbitu(rtcm->buff,i,32,sqrtA    ); i+=32;
    setbitu(rtcm->buff,i,14,toe      ); i+=14;
    setbits(rtcm->buff,i,16,cic      ); i+=16;
    setbits(rtcm->buff,i,32,OMG0     ); i+=32;
    setbits(rtcm->buff,i,16,cis      ); i+=16;
    setbits(rtcm->buff,i,32,i0       ); i+=32;
    setbits(rtcm->buff,i,16,crc      ); i+=16;
    setbits(rtcm->buff,i,32,omg      ); i+=32;
    setbits(rtcm->buff,i,24,OMGd     ); i+=24;
    setbits(rtcm->buff,i,10,bgd1     ); i+=10;

1045:  F/NAV
    setbitu(rtcm->buff,i, 2,oshs     ); i+= 2; /* E5a SVH */
    setbitu(rtcm->buff,i, 1,osdvs    ); i+= 1; /* E5a DVS */
    setbitu(rtcm->buff,i, 7,0        ); i+= 7; /* reserved */
1046:  I/NAV
    setbits(rtcm->buff,i,10,bgd2     ); i+=10;
    setbitu(rtcm->buff,i, 2,oshs1    ); i+= 2; /* E5b SVH */
    setbitu(rtcm->buff,i, 1,osdvs1   ); i+= 1; /* E5b DVS */
    setbitu(rtcm->buff,i, 2,oshs2    ); i+= 2; /* E1 SVH */
    setbitu(rtcm->buff,i, 1,osdvs2   ); i+= 1; /* E1 DVS */

#endif
  }
  else if(type == 1059 || type == 1242) { // GPS/Galileo bias
    int off = 0;
    int msgnum = gbum(off, 12);

    int gpstime = gbum(off, 20);
    int uinterval = gbum(off, 4);
    int mmi = gbum(off, 1);
    int iodssr = gbum(off, 4);
    int ssrprov = gbum(off, 16);
    int ssrsol = gbum(off, 4);
    int numsats = gbum(off, 6);

    //    cout <<"msgnum "<<msgnum<<" gpstime " << gpstime<<" numsats "<< numsats<<endl;
    d_dcbs.clear();
    for(int n=0; n < numsats; ++n) {
      int gpsid = gbum(off, 6);
      int numdcbs = gbum(off, 5);
      //      cout<<"  "<< (type==1059 ? "G" : "E") <<gpsid<<" has "<<numdcbs <<" DCBs\n";
      SatID id;
      id.gnss = (type==1059 ? 0 : 2); // GPS or Galileo
      id.sv = gpsid;
      for(int m = 0 ; m < numdcbs; ++m) {
        int sig = gbum(off, 5);
        id.sigid = sig;
        int dcb = gbsm(off, 14); // 0.01 meter
        d_dcbs[id] = 0.01*dcb;
        //        cout<<"     sig "<<sig <<" dcb " << dcb*0.01 << "\n";

        /*
Indicator to specify the GPS signal and tracking mode:
0 - L1 C/A
1- L1 P
2- L1 Z-tracking and similar (AS on)
3 - Reserved
4 - Reserved
5 - L2 C/A
6 - L2 L1(C/A)+(P2-P1) (semi-codeless)
7 - L2 L2C (M)
8 - L2 L2C (L)
9 - L2 L2C (M+L)
10 - L2 P
11 - L2 Z-tracking and similar (AS on)
12 - Reserved
13 - Reserved
14 - L5 I
15 - L5 Q
>15 - Reserved.
        */
      }
    }
  }
}
