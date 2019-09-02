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
#include "navmon.hh"
#include "tle.hh"

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
try
{
  TLERepo tles;
  tles.parseFile("active.txt");
  /*  tles.parseFile("galileo.txt");
  tles.parseFile("glo-ops.txt");
  tles.parseFile("gps-ops.txt");
  tles.parseFile("beidou.txt");*/
  
  for(;;) {
    char bert[4];
    int res = readn2(0, bert, 4);
    if( res != 4) {
      cerr<<"EOF, res = "<<res<<endl;
      break;
    }
    if(bert[0]!='b' || bert[1]!='e' || bert[2] !='r' || bert[3]!='t') {
      cerr<<"Bad magic"<<endl;
    }
  
    
    uint16_t len;
    if(readn2(0, &len, 2) != 2)
      break;
    len = htons(len);
    char buffer[len];
    if(readn2(0, buffer, len) != len)
      break;
    
    NavMonMessage nmm;
    nmm.ParseFromString(string(buffer, len));

    //    if(nmm.type() == NavMonMessage::ReceptionDataType)
    //      continue;

    
    cout<<humanTime(nmm.localutcseconds())<<" "<<nmm.localutcnanoseconds()<<" ";
    cout<<"src "<<nmm.sourceid()<< " ";
    if(nmm.type() == NavMonMessage::ReceptionDataType) {
      cout<<"receptiondata for "<<nmm.rd().gnssid()<<","<<nmm.rd().gnsssv()<<", db "<<nmm.rd().db()<<" ele "<<nmm.rd().el() <<" azi "<<nmm.rd().azi()<<" prRes "<<nmm.rd().prres() << endl;
    }
    else if(nmm.type() == NavMonMessage::GalileoInavType) {
      basic_string<uint8_t> inav((uint8_t*)nmm.gi().contents().c_str(), nmm.gi().contents().size());
      static map<int, GalileoMessage> gms;
      static map<pair<int, int>, GalileoMessage> gmwtypes;
      static map<int, GalileoMessage> oldgm4s;
      int sv = nmm.gi().gnsssv();
      GalileoMessage& gm = gms[sv];
      int wtype = gm.parse(inav);
      gm.tow = nmm.gi().gnsstow();
      gmwtypes[{nmm.gi().gnsssv(), wtype}] = gm;
      cout << "gal inav for "<<nmm.gi().gnssid()<<","<<nmm.gi().gnsssv()<<" tow "<< nmm.gi().gnsstow()<<" wtype "<< wtype<<" ";
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

        oldgm4s[nmm.gi().gnsssv()] = gm;
      }

      if(wtype == 0 || wtype == 5 || wtype == 6)
        tow = gm.tow;
      
      if(wtype < 7)
        gm = GalileoMessage{};

      if(wtype >=7 && wtype<=10)
        cout<<" ioda "<<gm.iodalmanac;
      // af0 af1 scaling in almanac: 2^-19, 2^2^-38 plus "truncated"
      if(wtype == 7) {
        cout<<"  t0a "<<gm.t0almanac<<", alma sv1 "<<gm.alma1.svid<<", t0a age: "<< ephAge(gm.t0almanac *600, tow) << " ";
        if(gm.alma1.svid) {
          Point satpos;
          getCoordinates(0, gm.tow, gm.alma1, &satpos);
          cout<< "("<<satpos.x/1000<<", "<<satpos.y/1000<<", "<<satpos.z/1000<<")";
          
          auto match = tles.getBestMatch(nmm.localutcseconds(), satpos.x, satpos.y, satpos.z);
          cout<<" best-tle-match "<<match.name <<" distance "<<match.distance /1000<<" km ";
          cout <<" tle-e "<<match.e <<" eph-e " <<gm.alma1.getE() <<" tle-ran "<<match.ran;
          cout<<" norad " <<match.norad <<" int-desig " << match.internat;
        }
      }
      else if(wtype == 8 && gm.tow - gmwtypes[{sv,7}].tow < 5 && gmwtypes[{sv,7}].alma1.svid && gm.iodalmanac == gmwtypes[{sv,7}].iodalmanac) {
        cout<<"  "<<gmwtypes[{sv,7}].alma1.svid<<" af0 "<<gm.alma1.af0<<" af1 "<< gm.alma1.af1 <<" e5bhs "<< gm.alma1.e5bhs<<" e1bhs "<< gm.alma1.e1bhs <<" sv9 "<<gm.alma2.svid;
      }
      else if(wtype == 9 && gm.tow - gmwtypes[{sv,8}].tow < 30 && gm.iodalmanac == gmwtypes[{sv,8}].iodalmanac) {
        if(gmwtypes[{sv,8}].alma2.svid)
          cout<<"  "<<gmwtypes[{sv,8}].alma2.svid<<" af0 "<<gm.alma2.af0<<" af1 "<< gm.alma2.af1 <<" e5bhs "<< gm.alma2.e5bhs<<" e1bhs "<< gm.alma2.e1bhs;
        else
          cout<<"  empty almanac slot";
      }
      else if(wtype == 10 && gm.tow - gmwtypes[{sv,9}].tow < 5 && gmwtypes[{sv,9}].alma3.svid && gm.iodalmanac == gmwtypes[{sv,9}].iodalmanac) {
        if(gm.alma3.e1bhs != 0) {
          cout <<" gm.tow "<<gm.tow<<" gmwtypes.tow "<< gmwtypes[{sv,9}].tow <<" ";
        }
        cout<<"  "<<gmwtypes[{sv,9}].alma3.svid <<" af0 "<<gm.alma3.af0<<" af1 "<< gm.alma3.af1 <<" e5bhs "<< gm.alma3.e5bhs<<" e1bhs "<< gm.alma3.e1bhs <<" a0g " << gm.a0g <<" a1g "<< gm.a1g <<" t0g "<<gm.t0g <<" wn0g "<<gm.wn0g;
      }
      cout<<endl;      
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
      uint8_t pageno;
      static map<int, BeidouMessage> bms;
      auto& bm = bms[sv];
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
      else if(fraid == 3 && bm.sow) {
        Point sat;
        getCoordinates(0, bm.sow, bm, &sat);
        TLERepo::Match second;
        auto match = tles.getBestMatch(nmm.localutcseconds(), sat.x, sat.y, sat.z, &second);
        cout<<" best-tle-match "<<match.name <<" dist "<<match.distance /1000<<" km";
        cout<<" norad " <<match.norad <<" int-desig " << match.internat;
        cout<<" 2nd-match "<<second.name << " dist "<<second.distance/1000<<" km";


      }
      else if((fraid == 4 && 1<= pageno && pageno <= 24) ||
              (fraid == 5 && 1<= pageno && pageno <= 6) ||
              (fraid == 5 && 11<= pageno && pageno <= 23) ) {
        cout <<" pageno "<< (int) pageno<<" AmEpID "<< getbitu(&cond[0], beidouBitconv(291), 2);
        Point sat;
        if(fraid ==4 && pageno <= 5)
          bm.alma.geo=true;
        else
          bm.alma.geo=false;
        getCoordinates(0, bm.sow, bm.alma, &sat);
        TLERepo::Match second;
        auto match = tles.getBestMatch(nmm.localutcseconds(), sat.x, sat.y, sat.z, &second);
        cout<<" alma best-tle-match "<<match.name <<" dist "<<match.distance /1000<<" km";
        cout<<" norad " <<match.norad <<" int-desig " << match.internat;
        cout<<" 2nd-match "<<second.name << " dist "<<second.distance/1000<<" km";
        Point core{0,0,0};
        cout<<" rad "<<Vector(core,sat).length() << " t0a " << bm.alma.getT0e() << " eph-age " <<ephAge(bm.sow, bm.alma.getT0e())<<endl;
        int offset = 0;
        if(fraid == 4)
          offset = 0;
        else if(fraid == 5 && pageno <=6)
          offset = 24;
        else if(fraid == 5 && bm.alma.AmEpID ==1)
          offset = 20;
        else if(fraid == 5 && bm.alma.AmEpID ==2)
          offset = 34;
        else if(fraid == 5 && bm.alma.AmEpID ==3)
          offset = 46;
        
        almanac << (int)pageno+offset <<" " << match.norad <<" " <<match.name<<" " << (int)(match.distance/1000)<<" " << (int)(Vector(core,sat).length()/1000000)<<" " << (int) fraid<<" " << (int) pageno <<" " <<(int)bm.alma.AmEpID<<" " <<(int)offset<<"\n";
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
      static map<int, GlonassMessage> gms;
      auto& gm = gms[nmm.gloi().gnsssv()];
      
      int strno = gm.parse(std::basic_string<uint8_t>((uint8_t*)nmm.gloi().contents().c_str(), nmm.gloi().contents().size()));

      cout<<"Glonass R"<<nmm.gloi().gnsssv()<<" @ "<< ((int)nmm.gloi().freq()-7) <<" strno "<<strno;
      if(strno == 1) {
        cout << ", hour "<<(int)gm.hour <<" minute " <<(int)gm.minute <<" seconds "<<(int)gm.seconds;
        // start of period is 1st of January 1996 + (n4-1)*4, 03:00 UTC
        time_t glotime = gm.getGloTime();
        cout<<" 'wn' " << glotime / (7*86400)<<" 'tow' "<< (glotime % (7*86400));
      }
      if(strno == 2)
        cout<<" Tb "<<(int)gm.Tb <<" Bn "<<(int)gm.Bn;
      else if(strno == 4) {
        cout<<", taun "<<gm.taun <<" NT "<<gm.NT <<" FT " << (int) gm.FT <<" En " << (int)gm.En;
        if(gm.x && gm.y && gm.z) {
          auto longlat = getLongLat(gm.getX(), gm.getY(), gm.getZ());
          cout<<" long "<< 180* longlat.first/M_PI <<" lat " << 180*longlat.second/M_PI<<" rad "<<gm.getRadius();
          cout << " Tb "<<(int)gm.Tb <<" H"<<((gm.Tb/4.0) -3) << " UTC ("<<gm.getX()/1000<<", "<<gm.getY()/1000<<", "<<gm.getZ()/1000<<") -> ";
          cout << "("<<gm.getdX()/1000<<", "<<gm.getdY()/1000<<", "<<gm.getdZ()/1000<<")";
          time_t now = nmm.localutcseconds();
          struct tm tm;
          memset(&tm, 0, sizeof(tm));
          gmtime_r(&now, &tm);
          tm.tm_hour = (gm.Tb/4.0) - 3;
          tm.tm_min = (gm.Tb % 4)*15;
          tm.tm_sec = 0;

          auto match = tles.getBestMatch(timegm(&tm), gm.getX(), gm.getY(), gm.getZ());
          cout<<" best-tle-match "<<match.name <<" distance "<<match.distance /1000<<" km";
          cout<<" norad " <<match.norad <<" int-desig " << match.internat;
        }
      }
      else if(strno == 5)
        cout<<", n4 "<< (int)gm.n4 << " l_n " << gm.l_n;
      else if(strno == 7 || strno == 9 || strno == 11 || strno ==13 ||strno ==15) {
        cout << " l_n "<< gm.l_n;
      }
      else if(strno == 6 || strno ==8 || strno == 10 || strno ==12 ||strno ==14) {
        cout<<" nA "<< gm.nA <<" CnA " << gm.CnA <<" LambdaNaDeg "<< gm.getLambdaNaDeg();
      }
      cout<<endl;
    }
    else if(nmm.type() == NavMonMessage::ObserverPositionType) {
      auto lonlat = getLongLat(nmm.op().x(), nmm.op().y(), nmm.op().z());
      cout<<"ECEF "<<nmm.op().x()<<", "<<nmm.op().y()<<", "<<nmm.op().z()<< " lon "<< 180*lonlat.first/M_PI << " lat "<<
        180*lonlat.second/M_PI<<endl;
    }
    else if(nmm.type() == NavMonMessage::RFDataType) {
      cout<<"RFdata for "<<nmm.rfd().gnssid()<<","<<nmm.rfd().gnsssv()<<endl;
    }
    else {
      cout<<"Unknown type "<< (int)nmm.type()<<endl;
    }
  }
}
 catch(EofException& ee)
   {}
