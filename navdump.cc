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

Point g_ourpos;

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

double utcFromGPS(int wn, double tow)
{
  return (315964800 + wn * 7*86400 + tow - 18); 
}


int main(int argc, char** argv)
try
{
  TLERepo tles;
  //  tles.parseFile("active.txt");
  tles.parseFile("galileo.txt");
  tles.parseFile("glo-ops.txt");
  tles.parseFile("gps-ops.txt");
  tles.parseFile("beidou.txt");

  bool skipGPS{false};
  bool skipBeidou{false};
  bool skipGalileo{false};
  bool skipGlonass{false};
  
  ofstream almanac("almanac.txt");
  ofstream iodstream("iodstream.csv");
  iodstream << "timestamp gnssid sv iodnav t0e age" << endl;

  ofstream csv("delta.csv");
  csv <<"timestamp gnssid sv tow tle_distance alma_distance utc_dist x y z vx vy vz rad inclination e iod"<<endl;
  
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
      cout<<"receptiondata for "<<nmm.rd().gnssid()<<","<<nmm.rd().gnsssv()<<","<< (nmm.rd().has_sigid() ? nmm.rd().sigid() : 0) <<" db "<<nmm.rd().db()<<" ele "<<nmm.rd().el() <<" azi "<<nmm.rd().azi()<<" prRes "<<nmm.rd().prres() << endl;
    }
    else if(nmm.type() == NavMonMessage::GalileoInavType) {
      if(skipGalileo)
        continue;
      basic_string<uint8_t> inav((uint8_t*)nmm.gi().contents().c_str(), nmm.gi().contents().size());
      static map<int, GalileoMessage> gms;
      static map<pair<int, int>, GalileoMessage> gmwtypes;
      static map<int, GalileoMessage> oldgm4s;
      int sv = nmm.gi().gnsssv();
      GalileoMessage& gm = gms[sv];
      int wtype = gm.parse(inav);
      
      gm.tow = nmm.gi().gnsstow();
      gmwtypes[{nmm.gi().gnsssv(), wtype}] = gm;
      static map<int,GalileoMessage> oldEph;
      cout << "gal inav for "<<nmm.gi().gnssid()<<","<<nmm.gi().gnsssv()<<","<<nmm.gi().sigid()<<" tow "<< nmm.gi().gnsstow()<<" wtype "<< wtype<<" ";
      static uint32_t tow;
      if(wtype == 4) {
        //              2^-34       2^-46
        cout <<" iodnav "<<gm.iodnav <<" af0 "<<gm.af0 <<" af1 "<<gm.af1 <<", scaled: "<<ldexp(1.0*gm.af0, 19-34)<<", "<<ldexp(1.0*gm.af1, 38-46);
        if(tow && oldgm4s.count(nmm.gi().gnsssv()) && oldgm4s[nmm.gi().gnsssv()].iodnav != gm.iodnav) {
          auto& oldgm4 = oldgm4s[nmm.gi().gnsssv()];
          auto oldOffset = oldgm4.getAtomicOffset(tow);
          auto newOffset = gm.getAtomicOffset(tow);
          cout<<"  Timejump: "<<oldOffset.first - newOffset.first<<" after "<<(gm.getT0c() - oldgm4.getT0c() )<<" seconds";
        }

        oldgm4s[nmm.gi().gnsssv()] = gm;
      }
      if(wtype == 1) {
        cout << " iodnav " << gm.iodnav <<" t0e "<< gm.t0e*60 <<" " << ephAge(gm.t0e*60, gm.tow);
      }
      if(wtype == 2 || wtype == 3) {
        cout << " iodnav " << gm.iodnav;
      }
      if(wtype == 1 || wtype == 2 || wtype == 3) {
        iodstream << nmm.localutcseconds()<<" " << nmm.gi().gnssid() <<" "<< nmm.gi().gnsssv() << " " << gm.iodnav << " " << gm.t0e*60 <<" " << ephAge(gm.t0e*60, gm.tow);
        if(wtype == 3)
          iodstream<<endl;
        else
          iodstream<<"\n";
      }
      if(wtype == 0 || wtype == 5 || wtype == 6)
        tow = gm.tow;
      
      //      if(wtype < 7)
      //        gm = GalileoMessage{};

      if(wtype >=7 && wtype<=10)
        cout<<" ioda "<<gm.iodalmanac;
      // af0 af1 scaling in almanac: 2^-19, 2^2^-38 plus "truncated"
      if(wtype == 7) {
        // this is possible because all the ephemeris stuff is in 7
        cout<<"  t0a "<<gm.t0almanac<<", alma sv1 "<<gm.alma1.svid<<", t0a age: "<< ephAge(gm.t0almanac *600, tow) << " ";
        if(gm.alma1.svid) {
          Point satpos;
          getCoordinates(gm.tow, gm.alma1, &satpos);
          cout<< "("<<satpos.x/1000<<", "<<satpos.y/1000<<", "<<satpos.z/1000<<")";
          
          auto match = tles.getBestMatch(nmm.localutcseconds(), satpos.x, satpos.y, satpos.z);
          cout<<" best-tle-match "<<match.name <<" distance "<<match.distance /1000<<" km ";
          cout <<" tle-e "<<match.e <<" eph-e " <<gm.alma1.getE() <<" tle-ran "<<match.ran;
          cout<<" norad " <<match.norad <<" int-desig " << match.internat;
          cout<<" ele " << getElevationDeg(satpos, g_ourpos) << " azi " << getAzimuthDeg(satpos, g_ourpos);
        }
      }
      else if(wtype == 8 && gm.tow - gmwtypes[{sv,7}].tow < 5 && gmwtypes[{sv,7}].alma1.svid && gm.iodalmanac == gmwtypes[{sv,7}].iodalmanac) {
        // 8 finishes the rest
        cout<<"  alma1.sv "<<gmwtypes[{sv,7}].alma1.svid<<" af0 "<<gm.alma1.af0<<" af1 "<< gm.alma1.af1 <<" e5bhs "<< gm.alma1.e5bhs<<" e1bhs "<< gm.alma1.e1bhs <<" sv9 "<<gm.alma2.svid;
      }
      else if(wtype == 9 && gm.tow - gmwtypes[{sv,8}].tow < 30 && gm.iodalmanac == gmwtypes[{sv,8}].iodalmanac) {
        if(gmwtypes[{sv,8}].alma2.svid)
          cout<<" alma2.sv "<<gmwtypes[{sv,8}].alma2.svid<<" af0 "<<gm.alma2.af0<<" af1 "<< gm.alma2.af1 <<" e5bhs "<< gm.alma2.e5bhs<<" e1bhs "<< gm.alma2.e1bhs;
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
      if(skipGPS) {
        cout<<endl;
        continue;
      }
      
      int sv = nmm.gpsi().gnsssv();
      auto cond = getCondensedGPSMessage(std::basic_string<uint8_t>((uint8_t*)nmm.gpsi().contents().c_str(), nmm.gpsi().contents().size()));
      struct GPSState gs;
      static map<int, GPSState> eph;
      static map<int, GPSAlmanac> almas;
      uint8_t page;
      static int gpswn;
      int frame=parseGPSMessage(cond, gs, &page);
      cout<<"GPS "<<sv<<"@"<<nmm.gpsi().sigid()<<": "<<gs.tow<<" frame "<<frame<<" ";
      if(frame == 1) {
        static map<int, GPSState> oldgs1s;
        gpswn = gs.wn;
        cout << "gpshealth = "<<(int)gs.gpshealth<<", wn "<<gs.wn << " t0c "<<gs.t0c;
        if(auto iter = oldgs1s.find(sv); iter != oldgs1s.end() && iter->second.t0c != gs.t0c) {
          auto oldOffset = getGPSAtomicOffset(gs.tow, iter->second);
          auto newOffset = getGPSAtomicOffset(gs.tow, gs);
          cout<<"  Timejump: "<<oldOffset.first - newOffset.first<<" after "<<(getT0c(gs) - getT0c(iter->second) )<<" seconds, old t0c "<<iter->second.t0c;
        }
        oldgs1s[sv] = gs;
      }
      else if(frame == 2) {
        parseGPSMessage(cond, eph[sv], &page);
        cout << "t0e = "<<gs.iods.begin()->second.t0e << " " <<ephAge(gs.tow, gs.iods.begin()->second.t0e) << " iod "<<gs.gpsiod;
      }
      else if(frame == 3) {
        parseGPSMessage(cond, eph[sv], &page);
        cout <<"iod "<<gs.gpsiod;
        if(eph[sv].isComplete(gs.gpsiod)) {
          Point sat;
          getCoordinates(gs.tow, eph[sv].iods[gs.gpsiod], &sat);
          TLERepo::Match second;
          auto match = tles.getBestMatch(utcFromGPS(gpswn, gs.tow), sat.x, sat.y, sat.z, &second);
          cout<<" best-tle-match "<<match.name <<" dist "<<match.distance /1000<<" km";
          cout<<" norad " <<match.norad <<" int-desig " << match.internat;
          cout<<" 2nd-match "<<second.name << " dist "<<second.distance/1000<<" km t0e "<<gs.gpsalma.getT0e() << " t " <<nmm.localutcseconds();
          cout<<" ele " << getElevationDeg(sat, g_ourpos) << " azi " << getAzimuthDeg(sat, g_ourpos);

          if(almas.count(sv)) {
            Point almapoint;
            getCoordinates(gs.tow, almas[sv], &almapoint);
            cout<<" alma-dist " << Vector(sat, almapoint).length();

            Vector speed;
            getSpeed(gs.tow, eph[sv].iods[gs.gpsiod], &speed);
            Point core;
            csv << nmm.localutcseconds() << " 0 "<< sv <<" " << gs.tow << " " << match.distance <<" " << Vector(sat, almapoint).length() << " " << utcFromGPS(gpswn, gs.tow) - nmm.localutcseconds() << " " << sat.x <<" " << sat.y <<" " << sat.z <<" " <<speed.x <<" " <<speed.y<<" " <<speed.z<< " " << Vector(core, sat).length() << " " << eph[sv].iods[gs.gpsiod].getI0()<<" " << eph[sv].iods[gs.gpsiod].getE() << " " <<gs.gpsiod<<endl;
          }
        }
      }

      else if(frame == 4) {
        cout<<" page/svid " <<gs.gpsalma.sv;
        if((gs.gpsalma.sv >= 25 && gs.gpsalma.sv <= 32) || gs.gpsalma.sv==57 ) { // see table 20-V of the GPS ICD
          cout << " data-id "<<gs.gpsalma.dataid <<" alma-sv "<<gs.gpsalma.sv<<" t0a = "<<gs.gpsalma.getT0e() <<" health " <<gs.gpsalma.health;
          Point sat;
          getCoordinates(gs.tow, gs.gpsalma, &sat);
          TLERepo::Match second;
          auto match = tles.getBestMatch(nmm.localutcseconds(), sat.x, sat.y, sat.z, &second);
          cout<<" best-tle-match "<<match.name <<" dist "<<match.distance /1000<<" km";
          cout<<" norad " <<match.norad <<" int-desig " << match.internat;
          cout<<" 2nd-match "<<second.name << " dist "<<second.distance/1000<<" km t0e "<<gs.gpsalma.getT0e() << " t " <<nmm.localutcseconds();
        }

      }
      else if(frame == 5) {
        if(gs.gpsalma.sv <= 24) {
          cout << " alma-sv "<<gs.gpsalma.sv<<" t0a = "<<gs.gpsalma.getT0e() <<" health " <<gs.gpsalma.health;
          Point sat;
          getCoordinates(gs.tow, gs.gpsalma, &sat);
          TLERepo::Match second;
          auto match = tles.getBestMatch(nmm.localutcseconds(), sat.x, sat.y, sat.z, &second);
          cout<<" best-tle-match "<<match.name <<" dist "<<match.distance /1000<<" km";
          cout<<" norad " <<match.norad <<" int-desig " << match.internat;
          cout<<" 2nd-match "<<second.name << " dist "<<second.distance/1000<<" km t0e "<<gs.gpsalma.getT0e() << " t " <<nmm.localutcseconds();
          almas[gs.gpsalma.sv] = gs.gpsalma;
        }

      }

      cout<<"\n";
    }
    else if(nmm.type() == NavMonMessage::BeidouInavTypeD1) {
      if(skipBeidou) {
        cout<<endl;
        continue;
      }

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
        cout<< ", "<<offset.first<<"ns " << (offset.second * 3600) <<" ns/hour "<< ephAge(bm.sow, bm.t0c*8);
      }
      else if(fraid == 3 && bm.sow) {
        Point sat;
        getCoordinates(bm.sow, bm, &sat);
        TLERepo::Match second;
        auto match = tles.getBestMatch(nmm.localutcseconds(), sat.x, sat.y, sat.z, &second);
        cout<<" best-tle-match "<<match.name <<" dist "<<match.distance /1000<<" km";
        cout<<" norad " <<match.norad <<" int-desig " << match.internat;
        cout<<" 2nd-match "<<second.name << " dist "<<second.distance/1000<<" km";


      }
      else if((fraid == 4 && 1<= pageno && pageno <= 24) ||
              (fraid == 5 && 1<= pageno && pageno <= 6) ||
              (fraid == 5 && 11<= pageno && pageno <= 23) ) {

        struct BeidouAlmanacEntry bae;
        if(processBeidouAlmanac(bm, bae)) {
          cout<<" alma-sv "<<bae.sv;
          Point sat;
          getCoordinates(bm.sow, bae.alma, &sat);
          TLERepo::Match second;
          auto match = tles.getBestMatch(nmm.localutcseconds(), sat.x, sat.y, sat.z, &second);
          cout<<" best-tle-match "<<match.name <<" dist "<<match.distance /1000<<" km";
          cout<<" norad " <<match.norad <<" int-desig " << match.internat;
          cout<<" 2nd-match "<<second.name << " dist "<<second.distance/1000<<" km t0e "<<bm.alma.getT0e() << " t " <<nmm.localutcseconds();
        }
        else
          cout <<" no valid alma";
      }
      else if(bm.fraid == 5 && pageno==7) {
        for(int n=0; n<19; ++n)
          cout<<" hea"<<(1+n)<<" " << getbitu(&cond[0], beidouBitconv(51+n*9), 9) << " ("<<beidouHealth(getbitu(&cond[0], beidouBitconv(51+n*9), 9)) <<")";
      }
      
      else if(bm.fraid == 5 && pageno==8) {
        for(int n=0; n<10; ++n)
          cout<<" hea"<<(20+n)<<" " << getbitu(&cond[0], beidouBitconv(51+n*9), 9) << " ("<<beidouHealth(getbitu(&cond[0], beidouBitconv(51+n*9), 9))<<")";
        cout<<" WNa "<<getbitu(&cond[0], beidouBitconv(190), 8)<<" t0a "<<getbitu(&cond[0], beidouBitconv(198), 8);
      }
      else if(bm.fraid == 5 && pageno==24) {
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
      if(skipGlonass) {
        cout<<endl;
        continue;
      }

      static map<int, GlonassMessage> gms;
      auto& gm = gms[nmm.gloi().gnsssv()];
      
      int strno = gm.parse(std::basic_string<uint8_t>((uint8_t*)nmm.gloi().contents().c_str(), nmm.gloi().contents().size()));

      cout<<"Glonass R"<<nmm.gloi().gnsssv()<<" @ "<< ((int)nmm.gloi().freq()-7) <<" strno "<<strno;
      if(strno == 1) {
        cout << ", hour "<<(int)gm.hour <<" minute " <<(int)gm.minute <<" seconds "<<(int)gm.seconds;
        // start of period is 1st of January 1996 + (n4-1)*4, 03:00 UTC
        time_t glotime = gm.getGloTime();
        cout<<" 'wn' " << glotime / (7*86400)<<" 'tow' "<< (glotime % (7*86400)) << " x " <<gm.getX()/1000.0;
      }
      else if(strno == 2)
        cout<<" Tb "<<(int)gm.Tb <<" Bn "<<(int)gm.Bn << " y " <<gm.getY()/1000.0;
      else if(strno == 3)
        cout<<" l_n " << (int)gm.l_n  << " z " <<gm.getZ()/1000.0;
      else if(strno == 4) {
        cout<<", taun "<<gm.taun <<" NT "<<gm.NT <<" FT " << (int) gm.FT <<" En " << (int)gm.En;
        if(gm.x && gm.y && gm.z) {
          auto longlat = getLongLat(gm.getX(), gm.getY(), gm.getZ());
          cout<<" long "<< 180* longlat.first/M_PI <<" lat " << 180*longlat.second/M_PI<<" rad "<<gm.getRadius();
          cout << " Tb "<<(int)gm.Tb <<" H"<<((gm.Tb/4.0) -3) << " UTC ("<<gm.getX()/1000<<", "<<gm.getY()/1000<<", "<<gm.getZ()/1000<<") -> ";
          cout << "("<<gm.getdX()/1000<<", "<<gm.getdY()/1000<<", "<<gm.getdZ()/1000<<")";
          auto match = tles.getBestMatch(getGlonassT0e(nmm.localutcseconds(), gm.Tb), gm.getX(), gm.getY(), gm.getZ());
          cout<<" best-tle-match "<<match.name <<" distance "<<match.distance /1000<<" km";
          cout<<" norad " <<match.norad <<" int-desig " << match.internat;
        }
      }
      else if(strno == 5)
        cout<<", n4 "<< (int)gm.n4 << " l_n " << gm.l_n;
      else if(strno == 6 || strno ==8 || strno == 10 || strno ==12 ||strno ==14) {
        cout<<" nA "<< gm.nA <<" CnA " << gm.CnA <<" LambdaNaDeg "<< gm.getLambdaNaDeg() << " e " <<gm.getE() << " i0 "<< 180.0*gm.getI0()/M_PI;
      }
      else if(strno == 7 || strno == 9 || strno == 11 || strno ==13 ||strno ==15) {
        cout << " l_n "<< gm.l_n << " Tlambdana " <<gm.gettLambdaNa() <<" Hna " << (int)gm.hna;
      }
      cout<<endl;
    }
    else if(nmm.type() == NavMonMessage::ObserverPositionType) {
      auto lonlat = getLongLat(nmm.op().x(), nmm.op().y(), nmm.op().z());
      cout<<std::fixed<<"ECEF "<<nmm.op().x()<<", "<<nmm.op().y()<<", "<<nmm.op().z()<< " lon "<< 180*lonlat.first/M_PI << " lat "<<
        180*lonlat.second/M_PI<< " acc "<<nmm.op().acc()<<" m "<<endl;
      g_ourpos = Point(nmm.op().x(), nmm.op().y(), nmm.op().z());
    }
    else if(nmm.type() == NavMonMessage::RFDataType) {
      cout<<"RFdata for "<<nmm.rfd().gnssid()<<","<<nmm.rfd().gnsssv()<<","<<(nmm.rfd().has_sigid() ? nmm.rfd().sigid() : 0) <<endl;

    }
    else {
      cout<<"Unknown type "<< (int)nmm.type()<<endl;
    }
  }
}
 catch(EofException& ee)
   {}
