
#include <string>
#include <stdio.h>
#include <iostream>
#include <arpa/inet.h>
#include "fmt/format.h"
#include "fmt/printf.h"
#include <fstream>
#include <map>
#include <bitset>
#include <vector>
#include <thread>
#include <signal.h>
#include <time.h>
#include "ubx.hh"
#include "bits.hh"
#include "minivec.hh"
#include "navmon.pb.h"
#include "ephemeris.hh"
#include "gps.hh"
#include "glonass.hh"
#include "beidou.hh"
#include "galileo.hh"

#include <unistd.h>
using namespace std;

static std::string humanTime(time_t t)
{
  struct tm tm={0};
  gmtime_r(&t, &tm);

  char buffer[80];
  strftime(buffer, sizeof(buffer), "%a, %d %b %Y %T %z", &tm);
  return buffer;
}


string beidouHealth(int in)
{
  string ret;
  if(in == 256) {
    return "NO CLOCK";
  }
  if(in==511) {
    return "NO SAT";
  }

  if(in & (1<<7))
    ret += "B1I abnormal ";
  if(in & (1<<6))
    ret += "B2I abnormal ";
  if(in & (1<<5))
    ret += "B3I abnormal ";
  if(in & (1<<1))
    ret += "navigation abnormal ";

  if(ret.empty())
    return "OK";
  return ret;
}

int main(int argc, char** argv)
{
  for(;;) {
    char bert[4];
    int res = read(0, bert, 4);
    if( res != 4) {
      cerr<<"EOF, res = "<<res<<endl;
      break;
    }
    if(bert[0]!='b' || bert[1]!='e' || bert[2] !='r' || bert[3]!='t') {
      cerr<<"Bad magic"<<endl;
    }
  
    
    uint16_t len;
    if(read(0, &len, 2) != 2)
      break;
    len = htons(len);
    char buffer[len];
    if(read(0, buffer, len) != len)
      break;
    
    NavMonMessage nmm;
    nmm.ParseFromString(string(buffer, len));

    if(nmm.type() == NavMonMessage::ReceptionDataType)
      continue;

    
    cout<<humanTime(nmm.localutcseconds())<<" "<<nmm.localutcnanoseconds()<<" ";
    cout<<"src "<<nmm.sourceid()<< " ";
    if(nmm.type() == NavMonMessage::ReceptionDataType) {
      cout<<"receptiondata for "<<nmm.rd().gnssid()<<","<<nmm.rd().gnsssv()<<", db "<<nmm.rd().db()<<" ele "<<nmm.rd().el() <<" azi "<<nmm.rd().azi()<<endl;
    }
    else if(nmm.type() == NavMonMessage::GalileoInavType) {
      basic_string<uint8_t> inav((uint8_t*)nmm.gi().contents().c_str(), nmm.gi().contents().size());
      static map<int, GalileoMessage> gms;
      static map<int, GalileoMessage> oldgm4s;
      GalileoMessage& gm = gms[nmm.gi().gnsssv()];
      int wtype = gm.parse(inav);

      cout << "gal inav for "<<nmm.gi().gnssid()<<","<<nmm.gi().gnsssv()<<" tow "<< nmm.gi().gnsstow()<<" wtype "<< wtype << endl;
      static uint32_t tow;
      if(wtype == 4) {
        //              2^-34       2^-46
        cout <<" af0 "<<gm.af0 <<" af1 "<<gm.af1 <<", scaled: "<<ldexp(1.0*gm.af0, 19-34)<<", "<<ldexp(1.0*gm.af1, 38-46);
        if(tow && oldgm4s.count(nmm.gi().gnsssv()) && oldgm4s[nmm.gi().gnsssv()].iodnav != gm.iodnav) {
          auto& oldgm4 = oldgm4s[nmm.gi().gnsssv()];
          auto oldOffset = oldgm4.getAtomicOffset(tow);
          auto newOffset = gm.getAtomicOffset(tow);
          cout<<"  Timejump: "<<oldOffset.first - newOffset.first<<" after "<<(gm.getT0c() - oldgm4.getT0c() )<<" seconds";
        }
        cout<<endl;
        oldgm4s[nmm.gi().gnsssv()] = gm;
      }

      if(wtype == 0 || wtype == 5 || wtype == 6)
        tow = gm.tow;
      
      if(wtype < 7)
        gm = GalileoMessage{};


      // af0 af1 scaling in almanac: 2^-19, 2^2^-38 plus "truncated"
      if(wtype == 7) {
        cout<<"  t0a "<<gm.t0almanac<<", alma sv1 "<<gm.alma1.svid<<", t0a age: "<< ephAge(gm.t0almanac *600, tow) << endl;
      }
      else if(wtype == 8 && gm.alma1.svid > 0) {
        cout<<"  "<<gm.alma1.svid<<" af0 "<<gm.alma1.af0<<" af1 "<< gm.alma1.af1 <<" e5bhs "<< gm.alma1.e5bhs<<" e1bhs "<< gm.alma1.e1bhs<<endl;
      }
      else if(wtype == 9 && gm.alma2.svid > 0) {
        cout<<"  "<<gm.alma2.svid<<" af0 "<<gm.alma2.af0<<" af1 "<< gm.alma2.af1 <<" e5bhs "<< gm.alma2.e5bhs<<" e1bhs "<< gm.alma2.e1bhs<<endl;
      }
      else if(wtype == 10 && gm.alma3.svid > 0){
        cout<<"  "<<gm.alma3.svid<<" af0 "<<gm.alma3.af0<<" af1 "<< gm.alma3.af1 <<" e5bhs "<< gm.alma3.e5bhs<<" e1bhs "<< gm.alma3.e1bhs<<endl;
      }
      
    }
    else if(nmm.type() == NavMonMessage::GPSInavType) {
      int sv = nmm.gpsi().gnsssv();
      auto cond = getCondensedGPSMessage(std::basic_string<uint8_t>((uint8_t*)nmm.gpsi().contents().c_str(), nmm.gpsi().contents().size()));
      struct GPSState gs;
      uint8_t page;
      int frame=parseGPSMessage(cond, gs, &page);
      cout<<"GPS "<<sv<<": "<<gs.tow<<" ";
      if(frame == 1) {
        static map<int, GPSState> oldgs1s;
        
        cout << "gpshealth = "<<(int)gs.gpshealth<<", wn "<<gs.wn << " t0c "<<gs.t0c;
        if(auto iter = oldgs1s.find(sv); iter != oldgs1s.end() && iter->second.t0c != gs.t0c) {
          auto oldOffset = getAtomicOffset(gs.tow, iter->second);
          auto newOffset = getAtomicOffset(gs.tow, gs);
          cout<<"  Timejump: "<<oldOffset.first - newOffset.first<<" after "<<(getT0c(gs) - getT0c(iter->second) )<<" seconds, old t0c "<<iter->second.t0c;
        }
        oldgs1s[sv] = gs;
      }
      else if(frame == 2) {
        cout << "t0e = "<<gs.iods.begin()->second.t0e << " " <<ephAge(gs.tow, gs.iods.begin()->second.t0e);
      }
      cout<<"\n";
    }
    else if(nmm.type() == NavMonMessage::BeidouInavTypeD1) {
      int sv = nmm.bid1().gnsssv();
      auto cond = getCondensedBeidouMessage(std::basic_string<uint8_t>((uint8_t*)nmm.bid1().contents().c_str(), nmm.bid1().contents().size()));
      BeidouMessage bm;
      uint8_t pageno;
      int fraid = bm.parse(cond, &pageno);
      cout<<"BeiDou "<<sv<<": "<<bm.sow<<", FraID "<<fraid;
      if(fraid == 1) {
        static map<int, BeidouMessage> msgs;
        if(msgs[sv].wn>= 0 && msgs[sv].t0c != bm.t0c) {
          auto oldOffset = msgs[sv].getAtomicOffset(bm.sow);
          auto newOffset = bm.getAtomicOffset(bm.sow);
          cout<<"  Timejump: "<<oldOffset.first - newOffset.first<<" after "<<(bm.getT0c() - msgs[sv].getT0c() )<<" seconds";
        }
        msgs[sv]=bm;
        cout<<" wn "<<bm.wn<<" t0c "<<(int)bm.t0c<<" aodc "<< (int)bm.aodc <<" aode "<< (int)bm.aode <<" sath1 "<< (int)bm.sath1 << " urai "<<(int)bm.urai << " af0 "<<bm.a0 <<" af1 " <<bm.a1 <<" af2 "<<bm.a2;
        auto offset = bm.getAtomicOffset();
        cout<< ", "<<offset.first<<"ns " << (offset.second * 3600) <<" ns/hour "<< ephAge(bm.sow, bm.t0c*8)<<endl;
      }
      else if(fraid == 4 && 1<= pageno && pageno <= 24) {
        cout <<" pageno "<< (int) pageno<<" AmEpID "<< getbitu(&cond[0], beidouBitconv(291), 2);
      }
      else if(fraid == 5 && 1<= pageno && pageno <= 6) {
        cout <<" pageno "<<(int)pageno<<" AmEpID "<< getbitu(&cond[0], beidouBitconv(291), 2);
      }

      else if(fraid == 5 && pageno==7) {
        for(int n=0; n<19; ++n)
          cout<<" hea"<<(1+n)<<" " << getbitu(&cond[0], beidouBitconv(51+n*9), 9) << " ("<<beidouHealth(getbitu(&cond[0], beidouBitconv(51+n*9), 9)) <<")";
      }
      
      else if(fraid == 5 && pageno==8) {
        for(int n=0; n<10; ++n)
          cout<<" hea"<<(20+n)<<" " << getbitu(&cond[0], beidouBitconv(51+n*9), 9) << " ("<<beidouHealth(getbitu(&cond[0], beidouBitconv(51+n*9), 9))<<")";
        cout<<" WNa "<<getbitu(&cond[0], beidouBitconv(190), 8)<<" t0a "<<getbitu(&cond[0], beidouBitconv(198), 8);
      }
      else if(fraid == 5 && pageno==24) {
        int AmID= getbitu(&cond[0], beidouBitconv(216), 2);
        cout<<" AmID "<< AmID;
        for(int n=0; n<14; ++n)
          cout<<" hea"<<(31+n)<<" (" << getbitu(&cond[0], beidouBitconv(51+n*9), 9) << " "<<beidouHealth(getbitu(&cond[0], beidouBitconv(51+n*9), 9))<<")";
      }
      cout<<endl;
      
    }
    else if(nmm.type() == NavMonMessage::BeidouInavTypeD2) {
      int sv = nmm.bid2().gnsssv();
      auto cond = getCondensedBeidouMessage(std::basic_string<uint8_t>((uint8_t*)nmm.bid2().contents().c_str(), nmm.bid2().contents().size()));
      BeidouMessage bm;
      uint8_t pageno;
      int fraid = bm.parse(cond, &pageno);

      cout<<"BeiDou "<<sv<<" D2: "<<bm.sow<<", FraID "<<fraid << endl;
            
    }
    else if(nmm.type() == NavMonMessage::GlonassInavType) {
      GlonassMessage gm;
      int strno = gm.parse(std::basic_string<uint8_t>((uint8_t*)nmm.gloi().contents().c_str(), nmm.gloi().contents().size()));

      cout<<"Glonass R"<<nmm.gloi().gnsssv()<<" @ "<<nmm.gloi().freq() <<" strno "<<strno;
      if(strno == 1)
        cout << ", hour "<<(int)gm.hour <<" minute " <<(int)gm.minute <<" seconds "<<(int)gm.seconds;
      if(strno == 2)
        cout<<" Tb "<<(int)gm.Tb <<" Bn "<<(int)gm.Bn;
      else if(strno == 4)
        cout<<", taun "<<gm.taun <<" NT "<<gm.NT <<" FT " << (int) gm.FT <<" En " << (int)gm.En;
      else if(strno == 6 || strno ==8 || strno == 10 || strno ==12 ||strno ==14)
        cout<<" nA "<< gm.nA <<" CnA "<<gm.CnA;
      cout<<endl;
    }
    else if(nmm.type() == NavMonMessage::ObserverPositionType) {
      cout<<"ECEF"<<endl;
    }
    else if(nmm.type() == NavMonMessage::RFDataType) {
      cout<<"RFdata for "<<nmm.rfd().gnssid()<<","<<nmm.rfd().gnsssv()<<endl;
    }
    else {
      cout<<"Unknown type "<< (int)nmm.type()<<endl;
    }
  }
}

