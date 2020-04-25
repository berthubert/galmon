#include "rtcm.hh"
#include "bits.hh"
#include <iostream>

using namespace std;

void RTCMMessage::parse(const std::string& str)
{
  auto gbu=[&str](int offset, int bits) {
    return getbitu((const unsigned char*)str.c_str(), offset, bits);
  };
  auto gbs=[&str](int offset, int bits) {
    return getbits((const unsigned char*)str.c_str(), offset, bits);
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
    sow = gbu(12, 20);
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
      ed.radial = gbs(off+     iodlen + 6, 22) * 0.1;
      ed.along = gbs(off+ iodlen+ 28, 20) * 0.4;
      ed.cross = gbs(off+ iodlen+48, 20) * 0.4;
      
      
      ed.dradial = gbs(off +   iodlen+  68, 21) * 0.001; 
      ed.dalong = gbs(off + iodlen + 89, 19) * 0.004;
      ed.dcross = gbs(off + iodlen +108, 19) * 0.004;
      ed.iod = gbu(off +6, iodlen);
      ed.sow = sow;
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
      cd.dclock0 = gbs(off + 6, 22)*1e-4;
      cd.dclock1 = gbs(off + 28, 21)*1e-6;
      cd.dclock2 = gbs(off + 49, 27)*2e-8;
      d_clocks.push_back(cd);
      //      cout<<"  "<< makeSatIDName(cd.id)<<" ";
      //      cout<< cd.dclock0 <<" ";
      //      cout<< cd.dclock1 <<" ";
      //      cout<< cd.dclock2 << endl;
    }
  }
    

}
