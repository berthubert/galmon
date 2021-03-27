#include <string>
#include <stdio.h>
#include <iostream>
#include <arpa/inet.h>
#include "fmt/format.h"
#include "fmt/printf.h"
#include "CLI/CLI.hpp"
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
#include "sp3.hh"
#include "ubx.hh"
#include <unistd.h>
#include "sbas.hh"
#include "version.hh"
#include "gpscnav.hh"
#include "rinex.hh"
#include "rtcm.hh"

static char program[]="navdump";

using namespace std;

extern const char* g_gitHash;

map<int, Point> g_srcpos;


vector<SP3Entry> g_sp3s;

bool sp3Order(const SP3Entry& a, const SP3Entry& b)
{
  return tie(a.gnss, a.sv, a.t) < tie(b.gnss, b.sv, b.t);
}

void readSP3s(string_view fname)
{
  SP3Reader sp3(fname);
  SP3Entry e;
  while(sp3.get(e)) {
    g_sp3s.push_back(e);
  }

  sort(g_sp3s.begin(), g_sp3s.end(), sp3Order);
  
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


// GALILEO ONLY!!
template<typename T>
void doOrbitDump(int gnss, int sv, int wn, const T& oldEph, const T& newEph, int time_start, int time_end)
{
  ofstream orbitcsv("orbit."+to_string(gnss)+"."+to_string(sv)+"."+to_string(oldEph.iodnav)+"-"+to_string(newEph.iodnav)+".csv");
                
  orbitcsv << "timestamp x y z oldx oldy oldz\n";
  orbitcsv << fixed;
  for(int t = time_start; t < time_end; t += 30) {
    Point p, oldp;
    getCoordinates(t, newEph, &p);
    getCoordinates(t, oldEph, &oldp);
    time_t posix = utcFromGST(wn, t);
    orbitcsv << posix <<" "
                           <<p.x<<" " <<p.y<<" "<<p.z<<" "
                           <<oldp.x<<" " <<oldp.y<<" "<<oldp.z<<"\n";
  }
}


struct SVFilter
{
  struct SatID
  {
    int gnss, sv, sigid;
    bool operator<(const SatID& rhs) const
    {
      return std::tie(gnss, sv, sigid) < std::tie(rhs.gnss, rhs.sv, rhs.sigid);
    }
  };
  
  void addFilter(string_view str)
  {
    SatID satid{0,0,-1};
    satid.gnss = atoi(&str[0]);
    auto pos=  str.find(',');
    if( pos != string::npos)
      satid.sv = atoi(&str[pos+1]);

    pos = str.find(',', pos+1);
    if(pos != string::npos)
      satid.sigid = atoi(&str[pos+1]);
    //    cout<<"Add to filter "<<satid.gnss<<", "<< satid.sv <<", "<<satid.sigid;
    d_filter.insert(satid);
  }
  bool check(int gnss, int sv, int sigid=-1)
  {
    //    cout<<"Check filter "<<gnss<<", "<< sv <<", "<<sigid<<endl;
    if(d_filter.empty())
      return true;
    if(d_filter.count({gnss,0,-1})) { // gnss match
      //      cout<<"gnss match"<<endl;
      return true;
    }
    if(d_filter.count({gnss,sv,-1})) {  // gnss, sv match
      //      cout<<"gnss, sv match"<<endl;
      return true;
    }
    if(d_filter.count({gnss,sv,sigid})) { // gnss, sv match, sigid
      //      cout<<"gnss, sv, sigid,  match"<<endl;
      return true;
    }
    //    cout<<"Returning false"<<endl;
    return false;
  }
  set<SatID> d_filter;
  
};

struct FixStat
{
  double iTow{-1};
  
  struct SatStat
  {
    double bestrange1{-1}; // corrected for clock
    double bestrange5{-1}; // corrected for clock
    double doppler1{-1}; // corrected for clock
    double doppler5{-1}; // corrected for clock
    double radvel{-1};
    double ephrange{-1};
    GalileoMessage ephemeris;
  };

  map<pair<int,int>, SatStat> sats;
};

double g_rcvoffset;
void emitFixState(int src, double iTow, FixStat& fs, int n)
{
  cout<<"\nFix state for source "<<src<<", have "<<fs.sats.size()<<" satellites, n="<<n<<endl;
  //  for(double dt = -0.2; dt < 0.2; dt+=0.0001)
  double dt=0;
  double offset=0;
  {

    int count=0;
    for(const auto& s : fs.sats) {
      if(s.first.second==14 || s.first.second==18)
        continue;

      Point sat;
      getCoordinates(iTow, s.second.ephemeris, &sat);
      if(getElevationDeg(sat, g_srcpos[src]) < 20)
        continue;
      /*
      Point sat;
      auto [toffset, trend] = s.second.ephemeris.getAtomicOffset(iTow +dt);
      (void)trend;

      getCoordinates(iTow + toffset/1000000000.0 + dt, s.second.ephemeris, &sat);
      double range = Vector(g_srcpos[nmm.sourceid()], sat).length();

      getCoordinates(iTow + range/299792458.0 + toffset/1000000000.0 + dt, s.second.ephemeris, &sat);
      range = Vector(g_srcpos[nmm.sourceid()], sat).length();
      */
      double range = s.second.ephrange;
      if(s.second.bestrange1 != -1) {
        offset += s.second.bestrange1 - range;
        count++;
      }
      if(s.second.bestrange5 != -1) {
        offset += s.second.bestrange5 - range;
        count++;
      }
    }
    if(!count) {
      fs.sats.clear();
      return;
    }
    
    cout<< " dt "<<dt<<" err "<<offset << " " << count << " avg " << offset/count<< " "<<offset/count/3e5<<"ms"<<endl;
    offset/=count; 
  }
  
  for(const auto& s : fs.sats) {
    Point sat;
    double E=getCoordinates(iTow, s.second.ephemeris, &sat);
    cout<<""<<s.first.first<<","<<s.first.second<<": "<<s.second.bestrange1-offset<<" "<<s.second.bestrange5-offset<<" " << s.second.ephrange<<", delta1 " << (s.second.bestrange1-offset-s.second.ephrange)<<", delta5 "<< (s.second.bestrange5-offset-s.second.ephrange)<<" dd "<< s.second.bestrange1 - s.second.bestrange5 <<" t0e " << s.second.ephemeris.getT0e()<< " elev " << getElevationDeg(sat, g_srcpos[src]) << " E " << E<< " clock " << s.second.ephemeris.getAtomicOffset(iTow).first/1000000<<"ms doppler1 "<<s.second.doppler1 << " doppler5 " <<s.second.doppler5<<" radvel " <<s.second.radvel<< " frac " << (s.second.bestrange1-offset-s.second.ephrange)/s.second.radvel;
    cout<< " fixed "<<((s.second.bestrange1 - offset-s.second.ephrange) + (s.second.ephrange/299792458.0) * s.second.radvel)<< " BGD-ns "<<ldexp(s.second.ephemeris.BGDE1E5b,-32)*1000000000<< endl;
  }

  fs.sats.clear();
}


int main(int argc, char** argv)
try
{
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  CLI::App app(program);

  
  TLERepo tles;
  //  tles.parseFile("active.txt");
  tles.parseFile("galileo.txt");
  tles.parseFile("glo-ops.txt");
  tles.parseFile("gps-ops.txt");
  tles.parseFile("beidou.txt");

  //  readSP3s("all.sp3");
  if(!g_sp3s.empty()) {
    //    sort(g_sp3s.begin(), g_sp3s.end(), [](const auto& a, const auto&b) { return a.t < b.t; });
    cout<<"Have "<<g_sp3s.size()<<" sp3 entries"<<endl; //, from "<<humanTime(g_sp3s.begin()->t) <<" to "<< humanTime(g_sp3s.rbegin()->t)<<endl;
  }

  vector<string> svpairs;
  vector<int> stations;
  bool doReceptionData{false};
  bool doRFData{true};
  bool doObserverPosition{false};
  bool doVERSION{false};
  string rinexfname;
  string osnmafname;
  app.add_option("--svs", svpairs, "Listen to specified svs. '0' = gps, '2' = Galileo, '2,1' is E01");
  app.add_option("--stations", stations, "Listen to specified stations.");
  app.add_option("--positions,-p", doObserverPosition, "Print out observer positions (or not)");
  app.add_option("--rfdata,-r", doRFData, "Print out RF data (or not)");
  app.add_option("--recdata", doReceptionData, "Print out reception data (or not)");
  app.add_option("--rinex", rinexfname, "If set, emit ephemerides to this filename");
  app.add_option("--osnma", osnmafname, "If set, emit OSNMA CSV to this filename");
  app.add_flag("--version", doVERSION, "show program version and copyright");
    
  try {
    app.parse(argc, argv);
  } catch(const CLI::Error &e) {
    return app.exit(e);
  }
  if(doVERSION) {
    showVersion(program, g_gitHash);
    exit(0);
  }
  SVFilter svfilter;
  for(const auto& svp : svpairs) {
    svfilter.addFilter(svp);
  }

  set<int> statset;
  for(const auto& i : stations)
    statset.insert(i);
  
  ofstream almanac("almanac.txt");
  ofstream iodstream("iodstream.csv");
  iodstream << "timestamp gnssid sv iodnav t0e age" << endl;

  ofstream ephcsv("eph.csv");
  ephcsv<<"timestamp gnssid sv old_iod new_iod age insta_age x y z lat lon h"<<endl;
  
  ofstream csv("delta.csv");
  csv <<"timestamp gnssid sv tow tle_distance alma_distance utc_dist x y z vx vy vz rad inclination e iod"<<endl;

  ofstream sp3csv;

  sp3csv.open ("sp3.csv", std::ofstream::out | std::ofstream::app);

  sp3csv<<"timestamp gnssid sv ephAge sp3X sp3Y sp3Z ephX ephY ephZ  sp3Clock ephClock distance along clockDelta E speed"<<endl;

  std::optional<RINEXNavWriter> rnw;

  if(!rinexfname.empty())
    rnw = RINEXNavWriter(rinexfname);

  std::optional<ofstream> osnmacsv;
  if(!osnmafname.empty()) {
    osnmacsv = ofstream(osnmafname);
    (*osnmacsv)<<"wn,tow,wtype,sv,osnma\n";

  }
  
  for(;;) {
    char bert[4];
    int res = readn2(0, bert, 4);
    if( res != 4) {
      cerr<<"EOF, res = "<<res<<endl;
      break;
    }
    
    // I am so sorry
    if(bert[0]!='b' || bert[1]!='e' || bert[2] !='r' || bert[3]!='t') {
      cerr<<"Bad magic: "<<makeHexDump(string(bert, 4))<<endl;
      int res;
      for(int s=0;;++s) {
        cerr<<"Skipping character hunting for good magic.. "<<s<<endl;
        bert[0] = bert[1];
        bert[1] = bert[2];
        bert[2] = bert[3];
        res = readn2(0, bert + 3, 1);
        if(res != 1)
          break;
        if(bert[0]=='b' && bert[1]=='e' && bert[2] =='r' && bert[3]=='t')
          break;
      }
      if(res != 1) {
        cerr<<"EOF2, res = "<<res<<endl;
        break;
      }
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

    if(!statset.empty() && !statset.count(nmm.sourceid()))
      continue;
    
    //    if(nmm.type() == NavMonMessage::ReceptionDataType)
    //      continue;

    auto etstamp = [&nmm]()  {
      cout<<humanTime(nmm.localutcseconds(), nmm.localutcnanoseconds())<<" src "<< nmm.sourceid()<<" ";
      uint32_t imptow = ((uint32_t)round(nmm.localutcseconds() + nmm.localutcnanoseconds()/1000000000.0) - 935280000 + 18) % (7*86400);
      if(!(imptow % 2))
        imptow--;
      
      cout<< "imptow "<<imptow-2<<" ";
    };
    
    static map<int,GalileoMessage> galEphemeris;
    if(nmm.type() == NavMonMessage::ReceptionDataType) {
      if(doReceptionData) {
        etstamp();
        cout<<"receptiondata for "<<nmm.rd().gnssid()<<","<<nmm.rd().gnsssv()<<","<< (nmm.rd().has_sigid() ? nmm.rd().sigid() : 0) <<" db "<<nmm.rd().db()<<" ele "<<nmm.rd().el() <<" azi "<<nmm.rd().azi()<<" prRes "<<nmm.rd().prres() << " qi " << (nmm.rd().has_qi() ? nmm.rd().qi() : -1) << " used " << (nmm.rd().has_used() ? nmm.rd().used() : -1) << endl;
      }
    }
    else if(nmm.type() == NavMonMessage::GalileoInavType) {
      basic_string<uint8_t> inav((uint8_t*)nmm.gi().contents().c_str(), nmm.gi().contents().size());
      static map<int, GalileoMessage> gms;
      static map<pair<int, int>, GalileoMessage> gmwtypes;
      static map<int, GalileoMessage> oldgm4s;
      int sv = nmm.gi().gnsssv();
      if(!svfilter.check(2, sv, nmm.gi().sigid()))
        continue;
      etstamp();

      int64_t imptow = (((uint32_t)round(nmm.localutcseconds() + nmm.localutcnanoseconds()/1000000000.0) - 935280000 + 18) % (7*86400)) - 2;
      if(nmm.gi().sigid() != 5) {
        if(!(imptow % 2))
          imptow--;
      }
      else if((imptow % 2))
          imptow--;

      if(imptow != nmm.gi().gnsstow())
        cout<< " !!"<< (imptow - nmm.gi().gnsstow()) <<"!! ";

      
      GalileoMessage& gm = gms[sv];
      int wtype = gm.parse(inav);
      
      gm.tow = nmm.gi().gnsstow();
      bool isnew = gmwtypes[{nmm.gi().gnsssv(), wtype}].tow != gm.tow;
      gmwtypes[{nmm.gi().gnsssv(), wtype}] = gm;
      static map<int,GalileoMessage> oldEph;
      cout << "gal inav wtype "<<wtype<<" for "<<nmm.gi().gnssid()<<","<<nmm.gi().gnsssv()<<","<<nmm.gi().sigid()<<" pbwn "<<nmm.gi().gnsswn()<<" pbtow "<< nmm.gi().gnsstow();
      static uint32_t tow;

      if(osnmacsv && isnew)
	(*osnmacsv)<<nmm.gi().gnsswn()<<","<<gm.tow<<","<<wtype<<","<<nmm.gi().gnsssv()<<","<<makeHexDump(nmm.gi().reserved1())<<endl;

      
      if(wtype >=1 && wtype <= 5) {
        if(nmm.gi().has_reserved1()) {
          cout<<" res1 "<<makeHexDump(nmm.gi().reserved1());
	}
      }
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
        int sv = nmm.gi().gnsssv();
        if(gmwtypes[{sv,1}].iodnav == gmwtypes[{sv,2}].iodnav &&
           gmwtypes[{sv,2}].iodnav == gmwtypes[{sv,3}].iodnav &&
           gmwtypes[{sv,3}].iodnav == gmwtypes[{sv,4}].iodnav) {
          cout <<" have complete ephemeris at " << gm.iodnav;

          galEphemeris[sv] = gm;
          SatID sid;
          sid.gnss=2;
          sid.sv = sv;
          sid.sigid=1;
          
          
          int start = utcFromGST(gm.wn, (int)gm.tow);
          
          SP3Entry e{2, sv, start};
          auto bestSP3 = lower_bound(g_sp3s.begin(), g_sp3s.end(), e, sp3Order);
          if(bestSP3 != g_sp3s.end() && bestSP3->gnss == e.gnss && bestSP3->sv == sv) {
            static set<pair<int,int>> haveSeen;
            if(!haveSeen.count({sv, bestSP3->t})) {
              haveSeen.insert({sv, bestSP3->t});
              Point newPoint;
              double E=getCoordinates(gm.tow + (bestSP3->t - start), gm, &newPoint, false);
              Point sp3Point(bestSP3->x, bestSP3->y, bestSP3->z);
              Vector dist(newPoint, sp3Point);

              Vector nspeed;
              getSpeed(gm.tow + (bestSP3->t - start), gm, &nspeed);
              Vector speed = nspeed;
              nspeed.norm();
              double along = nspeed.inner(dist);
              
              cout<<"\nsp3 "<<(bestSP3->t - start)<<" E"<<sv<<" "<<humanTime(bestSP3->t)<<" (" << newPoint.x/1000.0 <<", "<<newPoint.y/1000.0<<", "<<newPoint.z/1000.0<< ") (" <<
                (bestSP3->x/1000.0) <<", " << (bestSP3->y/1000.0) <<", " << (bestSP3->z/1000.0) << ") "<<bestSP3->clockBias << " " << gm.getAtomicOffset(gm.tow + (bestSP3->t-start)).first<< " " << dist.length()<< " ";
              cout << (bestSP3->clockBias - gm.getAtomicOffset(gm.tow + (bestSP3->t-start)).first);
              cout << " " << gm.af0 <<" " << gm.af1;
              cout << endl;
              
              sp3csv <<std::fixed<< bestSP3->t << " 2 "<< sv <<" " << ephAge(gm.tow+(bestSP3->t - start), gm.getT0e()) <<" "<<bestSP3->x<<" " << bestSP3->y<<" " <<bestSP3->z <<" " << newPoint.x<<" " <<newPoint.y <<" " <<newPoint.z << " " <<bestSP3->clockBias <<" ";
              sp3csv << gm.getAtomicOffset(gm.tow + (bestSP3->t-start)).first<<" " << dist.length() <<" " << along <<" ";
              sp3csv << (bestSP3->clockBias - gm.getAtomicOffset(gm.tow + (bestSP3->t-start)).first) << " " << E << " " << speed.length()<<endl;
            }
            
          }

          
          if(!oldEph[sv].sqrtA)
            oldEph[sv] = gm;
          else if(oldEph[sv].iodnav != gm.iodnav) {
	    if(rnw)
	      rnw->emitEphemeris(sid, gm);
	    //	    gm.toJSON();

            cout<<" disco! "<< oldEph[sv].iodnav << " - > "<<gm.iodnav <<", "<< (gm.getT0e() - oldEph[sv].getT0e())/3600.0 <<" hours-jump insta-age "<<ephAge(gm.tow, gm.getT0e())/3600.0<<" hours";
            Point oldPoint, newPoint;
            getCoordinates(gm.tow, oldEph[sv], &oldPoint);
            getCoordinates(gm.tow, gm, &newPoint);
            Vector jump(oldPoint, newPoint);
            cout<<" distance "<< jump.length() << " ("<<jump.x<<", "<<jump.y <<", "<<jump.z<<")";
            auto oldAtomic = oldEph[sv].getAtomicOffset(gm.tow);
            auto newAtomic = gm.getAtomicOffset(gm.tow);
            cout<<" clock-jump "<<oldAtomic.first - newAtomic.first<<" ns ";
            //            doOrbitDump(2, sv, gm.wn, oldEph[sv], gm, gm.tow - 3*3600, gm.tow + 3*3600);
            auto latlonh = ecefToWGS84(newPoint.x, newPoint.y, newPoint.z);
            ephcsv<<nmm.localutcseconds()<<" "<< 2 <<" " << sv <<" " <<oldEph[sv].iodnav << " "<<gm.iodnav <<" "<< (gm.getT0e() - oldEph[sv].getT0e()) <<" "<<ephAge(gm.tow, gm.getT0e())/3600 << " " <<newPoint.x<<" " << newPoint.y <<" " <<newPoint.z<<" " << 180*get<0>(latlonh)/M_PI<<" " << 180*get<1>(latlonh)/M_PI <<" " <<get<2>(latlonh) << "\n";
            oldEph[sv]=gm;
          }
        }
      }
      if(wtype == 1) {
        cout << " iodnav " << gm.iodnav <<" t0e "<< gm.t0e*60 <<" " << ephAge(gm.t0e*60, gm.tow);
      }
      if(wtype == 2 || wtype == 3) {
        cout << " iodnav " << gm.iodnav;
        if(wtype == 3)
          cout<<" sisa "<<(int)gm.sisa;
      }
      if(wtype == 1 || wtype == 2 || wtype == 3) {
        iodstream << nmm.localutcseconds()<<" " << nmm.gi().gnssid() <<" "<< nmm.gi().gnsssv() << " " << gm.iodnav << " " << gm.t0e*60 <<" " << ephAge(gm.t0e*60, gm.tow);
        if(wtype == 3)
          iodstream<<endl;
        else
          iodstream<<"\n";
      }
      if(wtype == 0 || wtype == 5 || wtype == 6) {
        if(wtype != 0 || gm.sparetime == 2) {
          cout << " tow "<< gm.tow;
          tow = gm.tow;
        }
      }
      if(wtype == 5) {
        cout <<" e1bhs "<< (int) gm.e1bhs << " e5bhs "<< (int) gm.e5bhs <<  " e1bdvs "<< (int) gm.e1bdvs << " e5bdvs "<< (int) gm.e5bdvs<< " wn "<<gm.wn <<" ai0 "<< gm.ai0 <<" ai1 " << gm.ai1 <<" ai2 " <<gm.ai2 << " BGDE1E5a " << gm.BGDE1E5a <<" BGDE1E5b " <<gm.BGDE1E5b;
        //        wn = gm.wn;
      }
      if(wtype == 6) {
        cout<<" a0 " << gm.a0 <<" a1 " << gm.a1 <<" t0t "<<gm.t0t << " dtLS "<<(int)gm.dtLS;
      }
      
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
          cout<<" ele " << getElevationDeg(satpos, g_srcpos[nmm.sourceid()]) << " azi " << getAzimuthDeg(satpos, g_srcpos[nmm.sourceid()]);
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
        cout<<"  alma3.sv "<<gmwtypes[{sv,9}].alma3.svid <<" af0 "<<gm.alma3.af0<<" af1 "<< gm.alma3.af1 <<" e5bhs "<< gm.alma3.e5bhs<<" e1bhs "<< gm.alma3.e1bhs <<" a0g " << gm.a0g <<" a1g "<< gm.a1g <<" t0g "<<gm.t0g <<" wn0g "<<gm.wn0g <<" delta-gps "<< gm.getGPSOffset(gm.tow, gm.wn).first<<"ns";

        int dw = (int)(gm.wn%64) - (int)(gm.wn0g);
        if(dw > 31)
          dw = 31- dw;
        int delta = dw*7*86400  + gm.tow - gm.getT0g(); // NOT ephemeris age tricks
        cout<<" wn%64 "<< (gm.wn%64) << " dw " << dw <<" delta " << delta;
      }
      
      cout<<endl;      
    }
    else if(nmm.type() == NavMonMessage::GalileoCnavType) {
      basic_string<uint8_t> cnav((uint8_t*)nmm.gc().contents().c_str(), nmm.gc().contents().size());
      int sv = nmm.gc().gnsssv();
      if(!svfilter.check(2, sv, nmm.gc().sigid()))
        continue;
      etstamp();
      cout << "C/NAV for " << nmm.gc().gnssid()<<","<<nmm.gc().gnsssv()<<","<<nmm.gc().sigid() <<": "<< makeHexDump(cnav)<<endl;

    }
    else if(nmm.type() == NavMonMessage::GalileoFnavType) {
      basic_string<uint8_t> fnav((uint8_t*)nmm.gf().contents().c_str(), nmm.gf().contents().size());
      int sv = nmm.gf().gnsssv();
      if(!svfilter.check(2, sv, nmm.gf().sigid()))
        continue;
      etstamp();
      GalileoMessage gm;
      gm.parseFnav(fnav);
      cout<<"gal F/NAV wtype "<< (int)gm.wtype << " for "<<nmm.gf().gnssid()<<","<<nmm.gf().gnsssv()<<","<<nmm.gf().sigid();
      if(gm.wtype ==1 || gm.wtype == 2 || gm.wtype == 3 || gm.wtype == 4)
        cout<<" iodnav " <<gm.iodnav <<" tow " << gm.tow;
      if(gm.wtype == 1) {
        cout <<" af0 "<<gm.af0 << " af1 "<<gm.af1 <<" af2 "<< (int)gm.af2;
      }
      if(gm.wtype == 2) {
        cout <<" sqrtA "<<gm.sqrtA;
      }
      if(gm.wtype == 3) {
        cout <<" t0e "<<gm.t0e;
      }
      if(gm.wtype == 4) {
        cout <<" dtLS "<<(int)gm.dtLS;
      }

      cout<<endl;
    }
    else if(nmm.type() == NavMonMessage::GPSInavType) {
      int sv = nmm.gpsi().gnsssv();

      if(!svfilter.check(0, sv))
        continue;
      etstamp();
      
      auto cond = getCondensedGPSMessage(std::basic_string<uint8_t>((uint8_t*)nmm.gpsi().contents().c_str(), nmm.gpsi().contents().size()));
      struct GPSState gs;
      static map<int, GPSState> eph;

      static map<int, GPSAlmanac> almas;
      uint8_t page;
      static int gpswn;
      int frame=gs.parseGPSMessage(cond, &page);
      cout<<"GPS "<<sv<<"@"<<nmm.gpsi().sigid()<<": "<<gs.tow<<" frame "<<frame<<" ";
      static map<int, GPSState> oldgs1s;
      static map<int, GPSState> oldgs2s;
      if(frame == 1) {

        gpswn = gs.wn;
        cout << "gpshealth = "<<(int)gs.gpshealth<<", wn "<<gs.wn << " t0c "<<gs.t0c << " af0 " << gs.af0 << " af1 " << gs.af1 <<" af2 " << (int)gs.af2;
        if(auto iter = oldgs1s.find(sv); iter != oldgs1s.end() && iter->second.t0c != gs.t0c) {
          auto oldOffset = getGPSAtomicOffset(gs.tow, iter->second);
          auto newOffset = getGPSAtomicOffset(gs.tow, gs);
          cout<<"  Timejump: "<<oldOffset.first - newOffset.first<<" after "<< ephAge(getT0c(gs), getT0c(iter->second) )<<" seconds, old t0c "<<iter->second.t0c;
        }
        oldgs1s[sv] = gs;
      }
      else if(frame == 2) {
        eph[sv].parseGPSMessage(cond, &page);
        // gs in frame 2 contains t0e, so legit
        cout << "t0e = "<<gs.getT0e() << " " <<ephAge(gs.tow, gs.getT0e()) << " iod "<<gs.gpsiod;
        oldgs2s[sv] = gs;
      }
      else if(frame == 3) {
        eph[sv].parseGPSMessage(cond, &page);
        cout <<"iod "<<gs.gpsiod;
        if(eph[sv].gpsiod == oldgs2s[sv].gpsiod) {
          Point sat;
          getCoordinates(gs.tow, eph[sv], &sat);
          TLERepo::Match second;
          auto match = tles.getBestMatch(utcFromGPS(gpswn, gs.tow), sat.x, sat.y, sat.z, &second);
          cout<<" best-tle-match "<<match.name <<" dist "<<match.distance /1000<<" km";
          cout<<" norad " <<match.norad <<" int-desig " << match.internat;
          cout<<" 2nd-match "<<second.name << " dist "<<second.distance/1000<<" km t0e "<<gs.gpsalma.getT0e() << " t " <<nmm.localutcseconds();
          cout<<" ele " << getElevationDeg(sat, g_srcpos[nmm.sourceid()]) << " azi " << getAzimuthDeg(sat, g_srcpos[nmm.sourceid()]);

          if(almas.count(sv)) {
            Point almapoint;
            getCoordinates(gs.tow, almas[sv], &almapoint);
            cout<<" alma-dist " << Vector(sat, almapoint).length();

            Vector speed;
            getSpeed(gs.tow, eph[sv], &speed);
            Point core;
            csv << nmm.localutcseconds() << " 0 "<< sv <<" " << gs.tow << " " << match.distance <<" " << Vector(sat, almapoint).length() << " " << utcFromGPS(gpswn, gs.tow) - nmm.localutcseconds() << " " << sat.x <<" " << sat.y <<" " << sat.z <<" " <<speed.x <<" " <<speed.y<<" " <<speed.z<< " " << Vector(core, sat).length() << " " << eph[sv].getI0()<<" " << eph[sv].getE() << " " <<gs.gpsiod<<endl;
          }

          int start = utcFromGPS(gpswn, (int)gs.tow);
          cout<<"sp3 start: "<<start<<" wn " << gpswn<<" tow " << gs.tow << endl;
            
          SP3Entry e{0, sv, start};
          auto bestSP3 = lower_bound(g_sp3s.begin(), g_sp3s.end(), e, sp3Order);
          if(bestSP3 != g_sp3s.end() && bestSP3->gnss == e.gnss && bestSP3->sv == sv) {
            static set<pair<int,int>> haveSeen;
            if(!haveSeen.count({sv, bestSP3->t})) {
              haveSeen.insert({sv, bestSP3->t});
              Point newPoint;
              double E=getCoordinates(gs.tow + (bestSP3->t - start), eph[sv], &newPoint, false);
              Point sp3Point(bestSP3->x, bestSP3->y, bestSP3->z);
              Vector dist(newPoint, sp3Point);

              Vector nspeed;
              getSpeed(gs.tow + (bestSP3->t - start), eph[sv], &nspeed);
              Vector speed = nspeed;
              nspeed.norm();
              double along = nspeed.inner(dist);
              
              cout<<"\nsp3 "<<(bestSP3->t - start)<<" G"<<sv<<" "<<humanTime(bestSP3->t)<<" (" << newPoint.x/1000.0 <<", "<<newPoint.y/1000.0<<", "<<newPoint.z/1000.0<< ") (" <<
                (bestSP3->x/1000.0) <<", " << (bestSP3->y/1000.0) <<", " << (bestSP3->z/1000.0) << ") "<<bestSP3->clockBias << " " << getGPSAtomicOffset(gs.tow + (bestSP3->t-start), oldgs1s[sv]).first<< " " << dist.length()<< " ";
              cout << (bestSP3->clockBias - getGPSAtomicOffset(gs.tow + (bestSP3->t-start), oldgs1s[sv]).first);
              cout << " " << gs.af0 <<" " << gs.af1;
              cout << endl;
              
              sp3csv <<std::fixed<< bestSP3->t << " 0 "<< sv <<" " << ephAge(gs.tow+(bestSP3->t - start), eph[sv].getT0e()) <<" "<<bestSP3->x<<" " << bestSP3->y<<" " <<bestSP3->z <<" " << newPoint.x<<" " <<newPoint.y <<" " <<newPoint.z << " " <<bestSP3->clockBias <<" ";
              sp3csv << getGPSAtomicOffset(gs.tow + (bestSP3->t-start), oldgs1s[sv]).first<<" " << dist.length() <<" " << along <<" ";
              sp3csv << (bestSP3->clockBias - getGPSAtomicOffset(gs.tow + (bestSP3->t-start), oldgs1s[sv]).first) << " " << E << " " << speed.length()<<endl;
            }
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
        if(page == 18)
          cout << " dtLS " << (int)gs.dtLS <<" dtLSF "<< (int)gs.dtLSF;
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
    else if(nmm.type() == NavMonMessage::RTCMMessageType) {
      etstamp();
      RTCMMessage rm;
      rm.parse(nmm.rm().contents());
      cout<<" rtcm-msg "<<rm.type<<" ";
      if(rm.type == 1057 || rm.type == 1240) {
        cout<<"iod-ssr "<<rm.ssrIOD<<" ";
        for(const auto& ed : rm.d_ephs) {
          cout<<makeSatIDName(ed.id)<<":  iode "<< ed.iod<<" ("<< ed.radial<<", "<<ed.along<<", "<<ed.cross<<") mm -> (";
          cout<< ed.dradial<<", "<<ed.dalong<<", "<<ed.dcross<< ") mm/s\n";
        }
      }
      else if(rm.type == 1058 || rm.type == 1241) {
        cout<<"iod-ssr "<<rm.ssrIOD<<" ";
        for(const auto& cd : rm.d_clocks) {
          cout<<makeSatIDName(cd.id)<<":  dclock0 "<< cd.dclock0 <<" dclock1 " << cd.dclock1 <<" dclock2 "<< cd.dclock2 << endl;
        }
      }
      else if (rm.type == 1060 || rm.type == 1243) {
        for(const auto& ed : rm.d_ephs) {
          cout<<makeSatIDName(ed.id)<<":  iode "<< ed.iod<<" ("<< ed.radial<<", "<<ed.along<<", "<<ed.cross<<") mm -> (";
          cout<< ed.dradial<<", "<<ed.dalong<<", "<<ed.dcross<< ") mm/s\n";
        }

        for(const auto& cd : rm.d_clocks) {
          cout<<makeSatIDName(cd.id)<<":  dclock0 "<< cd.dclock0 <<" dclock1 " << cd.dclock1 <<" dclock2 "<< cd.dclock2 << endl;
        }
      }
      else if(rm.type == 1045 || rm.type == 1046) {
        static ofstream af0inavstr("af0inav.csv"), af0fnavstr("af0fnav.csv"), bgdstr("bgdstr.csv");
        static bool first{true};

        if(first) {
          af0inavstr<<"timestamp sv wn t0c af0 af1\n";
          af0fnavstr<<"timestamp sv wn t0c af0 af1\n";
          first=false;
        }
        SatID sid;
        sid.gnss = 2;
        sid.sv = rm.d_sv;
        sid.sigid = (rm.type == 1045) ? 5 : 1;
        
        cout<< makeSatIDName(sid)<<" ";
        if(rm.type == 1045) {
          af0fnavstr << nmm.localutcseconds()<<" " << rm.d_sv <<" " << rm.d_gm.wn<<" "<< rm.d_gm.t0c << " " << rm.d_gm.af0 << " " << rm.d_gm.af1<<"\n";
          cout<<"F/NAV";
        }
        else {
          af0inavstr << nmm.localutcseconds() <<" " << rm.d_sv <<" " << rm.d_gm.wn<<" "<<rm.d_gm.t0c<<" "<< rm.d_gm.af0 << " " << rm.d_gm.af1<<"\n";
          bgdstr << nmm.localutcseconds() <<" " << rm.d_sv<<" " <<rm.d_gm.BGDE1E5a <<" " << rm.d_gm.BGDE1E5b << "\n";
          cout <<"I/NAV";
        }

        cout <<" iode " << rm.d_gm.iodnav << " sisa " << (unsigned int) rm.d_gm.sisa << " t0c " << rm.d_gm.t0c << " af0 "<<rm.d_gm.af0 <<" af1 " << rm.d_gm.af1 <<" af2 " << (int) rm.d_gm.af2 << " BGDE1E5a " << rm.d_gm.BGDE1E5a;
        if(rm.type == 1046) // I/NAV
          cout <<" BGDE1E5b "<< rm.d_gm.BGDE1E5b;
        cout<<endl;
      }
      else if(rm.type == 1059 || rm.type==1242) {
        cout<<"\n";
        for(const auto& dcb : rm.d_dcbs) {
          cout<<"   "<<makeSatIDName(dcb.first)<<":  "<<dcb.second<<" meters\n";
        }

        cout<<endl;
      }
      else {
        cout<<" len " << nmm.rm().contents().size() << endl;
        
      }

    }
    else if(nmm.type() == NavMonMessage::GPSCnavType) {
      int sv = nmm.gpsc().gnsssv();
      int sigid = nmm.gpsc().sigid();
      if(!svfilter.check(0, sv, sigid))
        continue;
      etstamp();
      static map<int, GPSCNavState> states;
      auto& state = states[sv];
      int type = parseGPSCNavMessage(
                                     std::basic_string<uint8_t>((uint8_t*)nmm.gpsc().contents().c_str(),
                                                                nmm.gpsc().contents().size()),
        state);
    
      SatID sid{0, (uint32_t)sv, (uint32_t)sigid};
      cout << "GPS CNAV " << makeSatIDName(sid) <<" tow "<<state.tow<<" type " << type;
      if(type == 32) {
        cout <<" delta-ut1 "<< state.getUT1OffsetMS(state.tow).first<<"ms";
      }
      cout<<endl;
    }
    else if(nmm.type() == NavMonMessage::BeidouInavTypeD1) {
      int sv = nmm.bid1().gnsssv();

      if(!svfilter.check(3, sv))
        continue;
      etstamp();

      std::basic_string<uint8_t> cond;
      try {
        cond = getCondensedBeidouMessage(std::basic_string<uint8_t>((uint8_t*)nmm.bid1().contents().c_str(), nmm.bid1().contents().size()));
      }
      catch(std::exception& e) {
        cout<<"Parsing error"<<endl;
        continue;
      }
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
        cout<<" wn "<<bm.wn<<" t0c "<<(int)bm.t0c<<" aodc "<< (int)bm.aodc <<" aode "<< (int)bm.aode <<" sath1 "<< (int)bm.sath1 << " urai "<<(int)bm.urai << " af0 "<<bm.a0 <<" af1 " <<bm.a1 <<" af2 "<< (int)bm.a2;
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
      else if(bm.fraid == 5 && pageno==10) {
        cout <<" dTLS "<< (int)bm.deltaTLS;
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

      if(!svfilter.check(3, sv))
        continue;
      etstamp();
      
      auto cond = getCondensedBeidouMessage(std::basic_string<uint8_t>((uint8_t*)nmm.bid2().contents().c_str(), nmm.bid2().contents().size()));
      BeidouMessage bm;
      uint8_t pageno;
      int fraid = bm.parse(cond, &pageno);

      cout<<"BeiDou "<<sv<<" D2: "<<bm.sow<<", FraID "<<fraid << endl;
            
    }
    else if(nmm.type() == NavMonMessage::GlonassInavType) {
      if(!svfilter.check(6, nmm.gloi().gnsssv()))
        continue;
      etstamp();
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
        cout<<", taun "<<gm.taun <<" NT "<<gm.NT <<" FT " << (int) gm.FT <<" En " << (int)gm.En <<" M " << (int)gm.M ;
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
        cout<<", n4 "<< (int)gm.n4 << " l_n " << gm.l_n <<" tauc "<< gm.tauc <<  " taugps "<<gm.taugps;
      else if(strno == 6 || strno ==8 || strno == 10 || strno ==12 ||strno ==14) {
        cout<<" nA "<< gm.nA <<" CnA " << gm.CnA <<" LambdaNaDeg "<< gm.getLambdaNaDeg() << " e " <<gm.getE() << " i0 "<< 180.0*gm.getI0()/M_PI;
      }
      else if(strno == 7 || strno == 9 || strno == 11 || strno ==13 ||strno ==15) {
        cout << " l_n "<< gm.l_n << " Tlambdana " <<gm.gettLambdaNa() <<" Hna " << (int)gm.hna;
      }
      cout<<endl;
    }
    else if(nmm.type() == NavMonMessage::ObserverPositionType) {
      g_srcpos[nmm.sourceid()] = Point(nmm.op().x(), nmm.op().y(), nmm.op().z());
      
      if(!doObserverPosition)
        continue;
      etstamp();
      
      auto latlonh = ecefToWGS84(nmm.op().x(), nmm.op().y(), nmm.op().z());
      cout<<std::fixed<<"ECEF "<<nmm.op().x()<<", "<<nmm.op().y()<<", "<<nmm.op().z();
      cout<<", WGS84 lon "<< 180*std::get<1>(latlonh)/M_PI
	  <<" lat "<< 180*std::get<0>(latlonh)/M_PI
	  <<" elev "<< std::get<2>(latlonh) << " acc "<<nmm.op().acc()<<" m "<<endl;

      //loccsv<<std::fixed<<nmm.localutcseconds()+nmm.localutcnanoseconds()/1000000000.0<<" "<<180*std::get<1>(latlonh)/M_PI<<" "<<
      //        180*std::get<0>(latlonh)/M_PI<<" "<<std::get<2>(latlonh)<<" "<<nmm.op().acc()<<"\n";

    }
    else if(nmm.type() == NavMonMessage::RFDataType) {
      if(!doRFData)
        continue;
      
      if(!svfilter.check(nmm.rfd().gnssid(), nmm.rfd().gnsssv(), nmm.gi().sigid()))
        continue;

      
      etstamp();
      cout<<std::fixed<<"RFdata for "<<nmm.rfd().gnssid()<<","<<nmm.rfd().gnsssv()<<","<<(nmm.rfd().has_sigid() ? nmm.rfd().sigid() : 0) <<": ";
      cout<<" doppler-hz " << nmm.rfd().doppler();
      cout<<" pseudo-range " << nmm.rfd().pseudorange();
      cout<<" carrier-phase " << nmm.rfd().carrierphase();
      cout<<" rcv-tow "<<nmm.rfd().rcvtow();
      cout<<" pr-std " << nmm.rfd().prstd();
      cout<<" dop-std " << nmm.rfd().dostd();
      cout<<" cp-std " << nmm.rfd().cpstd();
      cout<<" locktime-ms " <<nmm.rfd().locktimems();
      if(nmm.rfd().has_cno()) {
        cout<<" cno-db " <<nmm.rfd().cno();
      }
      if(nmm.rfd().has_prvalid()) {
        cout<<" prvalid " <<nmm.rfd().prvalid();
      }
      if(nmm.rfd().has_cpvalid()) {
        cout<<" cpvalid " <<nmm.rfd().cpvalid();
      }
      if(nmm.rfd().has_clkreset()) {
        cout<<" clkreset " <<nmm.rfd().clkreset();
      }

        
      static map<int,FixStat> fixes;
      
      if(nmm.rfd().gnssid()==2 && galEphemeris.count(nmm.rfd().gnsssv())) { // galileo
        const auto& eph = galEphemeris[nmm.rfd().gnsssv()];
        Point sat;
        auto [offset, trend] = eph.getAtomicOffset(round(nmm.rfd().rcvtow()));
        (void)trend;

        static int n;
        double E=getCoordinates(nmm.rfd().rcvtow(), eph, &sat);

        double range = Vector(g_srcpos[nmm.sourceid()], sat).length();

        double origrange = range;
        E=getCoordinates(nmm.rfd().rcvtow() - range/299792458.0, eph, &sat);
        range = Vector(g_srcpos[nmm.sourceid()], sat).length();
        cout << " d "<<origrange-range;
        origrange=range;
        /*
        E=getCoordinates(nmm.rfd().rcvtow() + range/299792458.0 + offset/1000000000.0 - 0.018, eph, &sat);
        range = Vector(g_srcpos[nmm.sourceid()], sat).length();
        cout << " d "<< 10000.0*(origrange-range);
        origrange=range;
        */

        constexpr double omegaE = 2*M_PI /86164.091 ;
        /*
        double theta = -(2*M_PI*range / 299792458.0) /86164.091; // sidereal day
        
        Point rot;
        rot.x = sat.x * cos(theta) - sat.y * sin(theta);
        rot.y = sat.x * sin(theta) + sat.y * cos(theta);
        rot.z = sat.z;
        double oldrange=range;
        range = Vector(g_srcpos[nmm.sourceid()], rot).length(); // second try
        cout<<" rot-shift "<<oldrange-range <<" abs-move "<<Vector(rot, sat).length();
        */
        double rotcor = omegaE * (sat.x*g_srcpos[nmm.sourceid()].y - sat.y * g_srcpos[nmm.sourceid()].x) / 299792458.0;
        cout<<" rot-shift "<<rotcor;
        range += rotcor;
        
        double bestrange = nmm.rfd().pseudorange();
        double gap = range - bestrange;
        cout <<" pseudo-gap " << gap/1000.0;

        constexpr double speedOfLightPerNS = 299792458.0 / 1000000000.0;
        bestrange += speedOfLightPerNS * offset;

        // Δtr=F e A1/2 sin(E)
        constexpr double F = 1000000000.0*-4.442807309e-10; // "in ns"
        double dtr = F * eph.getE() * eph.getSqrtA() * sin(E);
        
        bestrange -= speedOfLightPerNS * dtr;
        
        cout<<" relcor "<<speedOfLightPerNS * dtr;

        // multi-freq adjustment is done in emitFixState
        
        cout<<" clockcor "<< offset/1000000.0 << "ms gap " << gap/1000.0;

        if(fixes[nmm.sourceid()].iTow != nmm.rfd().rcvtow()) {
          emitFixState(nmm.sourceid(), nmm.rfd().rcvtow(), fixes[nmm.sourceid()], n);
          fixes[nmm.sourceid()].iTow = nmm.rfd().rcvtow();
          n++;
        }
        auto& satstat=fixes[nmm.sourceid()].sats[{(int)nmm.rfd().gnssid(), (int)nmm.rfd().gnsssv()}];
        satstat.ephrange = range;
        auto dop = doDoppler(nmm.rfd().rcvtow(), g_srcpos[nmm.sourceid()], eph, 1575420000);
        satstat.radvel = dop.radvel;
        if(nmm.rfd().sigid()==1) {
          satstat.bestrange1 = bestrange;
          satstat.doppler1 = nmm.rfd().doppler();
        }
        else if(nmm.rfd().sigid()==5) {
          satstat.bestrange5 = bestrange;
          satstat.doppler5 = nmm.rfd().doppler();
        }
        satstat.ephemeris = eph;

      }
      
      cout<<endl;
      
    }
    else if(nmm.type() == NavMonMessage::ObserverDetailsType) {
      etstamp();
      cout<<"vendor "<<nmm.od().vendor()<<" hwversion " <<nmm.od().hwversion()<<" modules "<<nmm.od().modules()<<" swversion "<<nmm.od().swversion();
      cout<<" serial "<<nmm.od().serialno();
      if(nmm.od().has_owner())
        cout<<" owner "<<nmm.od().owner();
      if(nmm.od().has_clockoffsetdriftns())
        cout<<" drift "<<nmm.od().clockoffsetdriftns();
      if(nmm.od().has_clockaccuracyns())
        cout<<" clock-accuracy "<<nmm.od().clockaccuracyns();
      
      cout<<endl;
    }
    else if(nmm.type() == NavMonMessage::UbloxJammingStatsType) {
      etstamp();
      cout<<"noisePerMS "<<nmm.ujs().noiseperms() << " agcCnt "<<
        nmm.ujs().agccnt()<<" flags "<<nmm.ujs().flags()<<" jamind "<<
        nmm.ujs().jamind()<<endl;
    }
    else if(nmm.type() == NavMonMessage::SBASMessageType) {
      if(!svfilter.check(1, nmm.sbm().gnsssv(), 0))
        continue;

      etstamp();
      basic_string<uint8_t> sbas((uint8_t*)nmm.sbm().contents().c_str(), nmm.sbm().contents().size());
      cout<<" PRN "<<nmm.sbm().gnsssv()<<" SBAS message type ";

      // Preamble sequence: 
      // 0x53, 0x9a, 0xc6
      // 83, 154, 198
      
      // preamble(8), msgtype(6), payload212-95(18)
      // 5 * payload194-3(32)
      // payload2-1 parity(24) pad(6)
      
      
      //   cout<< makeHexDump(string((char*)sbas.c_str(), sbas.size())) << endl;
      int type = getbitu(&sbas[0], 8, 6);
      cout <<type<<" ";
      static map<int, SBASState> sbstate;

      if(type == 0) {
        sbstate[nmm.sbm().gnsssv()].parse0(sbas, nmm.localutcseconds());
      }
      else if(type == 1) {
        sbstate[nmm.sbm().gnsssv()].parse1(sbas, nmm.localutcseconds());
      }
      else if(type == 6) {
        auto integ = sbstate[nmm.sbm().gnsssv()].parse6(sbas, nmm.localutcseconds());
        cout<<"integrity updated: ";
        for(const auto& i : integ) {
          cout<<makeSatPartialName(i.id)<<" corr "<<i.correction <<" udrei "<< i.udrei <<" ";
        }
      }
      else if(type ==7) {
        sbstate[nmm.sbm().gnsssv()].parse7(sbas, nmm.localutcseconds());
        cout<<" latency " <<sbstate[nmm.sbm().gnsssv()].d_latency;
      }
      else if(type == 24) {
        auto ret=sbstate[nmm.sbm().gnsssv()].parse24(sbas, nmm.localutcseconds());
        cout<< " fast";
        for(const auto& i : ret.first)
          cout<< " "<<makeSatPartialName(i.id)<<" corr "<< i.correction <<" udrei "<<i.udrei;
        for(const auto& i : ret.second)
          cout<< " "<<makeSatPartialName(i.id)<<" dx "<< i.dx <<" dy "<<i.dy<<" dz "<<i.dz<<" dai " <<i.dai;
                
      }
      else if(type == 25) {
        auto ret = sbstate[nmm.sbm().gnsssv()].parse25(sbas, nmm.localutcseconds());
        for(const auto& i : ret)
          cout<< " "<<makeSatPartialName(i.id)<<" dx "<< i.dx <<" dy "<<i.dy<<" dz "<<i.dz<<" dai " <<i.dai;

      }
        
      cout<<endl;
    }
    else if(nmm.type() == NavMonMessage::DebuggingType) {

      auto res = parseTrkMeas(basic_string<uint8_t>((const uint8_t*)nmm.dm().payload().c_str(), nmm.dm().payload().size()));
      if(res.empty())
        continue;
      etstamp();
      uint64_t maxt=0;
      for(const auto& sv : res) {
        if(sv.gnss != 2) continue;
        if(sv.tr > maxt)
          maxt = sv.tr;
      }

      double ttag = round( (ldexp(1.0*maxt, -32)/1000.0 + 0.08) / 0.1) * 0.1;
      
      double rtow = round(ldexp(1.0*maxt, -32) /1000.0); // this was the rounded tow of transmission
      sort(res.begin(), res.end(), [&](const auto& a, const auto& b) {
          double elevA=0, elevB=0;
          if(galEphemeris.count(a.sv)) {
            const auto& eph = galEphemeris[a.sv];
            Point sat;
            getCoordinates(rtow, eph, &sat);
            elevA=getElevationDeg(sat, g_srcpos[nmm.sourceid()]);
          }
          if(galEphemeris.count(b.sv)) {
            const auto& eph = galEphemeris[b.sv];
            Point sat;
            getCoordinates(rtow, eph, &sat);
            elevB=getElevationDeg(sat, g_srcpos[nmm.sourceid()]);
          }
          return elevB < elevA;

        });
      

      
      double toffsetms=0;
      bool first = true;
      for(const auto sv : res) {
        if(sv.gnss != 2) continue;
        if(!galEphemeris.count(sv.sv)) 
          continue;

        const auto& eph = galEphemeris[sv.sv];

        double clockoffms =  eph.getAtomicOffset(rtow).first/1000000.0;
        
        Point sat;
        
        double E=getCoordinates(rtow - clockoffms/1000.0, eph, &sat);
        double range = Vector(g_srcpos[nmm.sourceid()], sat).length();
        getCoordinates(rtow - clockoffms/1000.0 - range/299792458.0, eph, &sat);
        range = Vector(g_srcpos[nmm.sourceid()], sat).length();
        
        double trmsec = ldexp(maxt - sv.tr, -32) + clockoffms;

        constexpr double omegaE = 2*M_PI /86164.091 ;
        double rotcor = omegaE * (sat.x*g_srcpos[nmm.sourceid()].y - sat.y * g_srcpos[nmm.sourceid()].x) / 299792458.0;
        range += rotcor;

        double bgdcor = 299792458.0 *ldexp(eph.BGDE1E5b,-32);
        range -= bgdcor;


        constexpr double speedOfLightPerNS = 299792458.0 / 1000000000.0;
        // Δtr=F e A1/2 sin(E)
        constexpr double F = 1000000000.0*-4.442807309e-10; // "in ns"
        double dtr = F * eph.getE() * eph.getSqrtA() * sin(E);
        double relcor = speedOfLightPerNS * dtr;
        range -= relcor; 

        
        double predTrmsec =  range / 299792.4580;
        
        if(first) {
          toffsetms = trmsec - predTrmsec;
          first = false;
          cout<<"Set toffsetms to "<< toffsetms << " for tow "<<rtow<<" ttag " << ttag << " raw " << (ldexp(1.0*maxt, -32) /1000.0) << endl;
        }

        trmsec -= toffsetms;
        cout<<std::fixed<<"gnssid "<< sv.gnss<<" sv "<<sv.sv <<": doppler "<<sv.dopplerHz <<" range-ms " << range / 299792.4580;
        cout << " actual-ms " <<  trmsec << " delta " << ((range / 299792.4580 - trmsec))<< " delta-m " << 299792.4580*((range / 299792.4580 - trmsec));

        cout<<" rotcor "<< rotcor;
        cout<<" relcor "<<relcor;
        cout<<" elev " << getElevationDeg(sat, g_srcpos[nmm.sourceid()]);
        cout<<" bgd-m " << bgdcor;
        cout<<" clockoff-ms " << clockoffms << endl;
        
      }
      cout<<endl;
    }
    else if(nmm.type() == NavMonMessage::SARResponseType) {    
      etstamp();

      string hexstring;
      string id = nmm.sr().identifier();
      for(int n = 0; n < 15; ++n)
        hexstring+=fmt::sprintf("%x", (int)getbitu((unsigned char*)id.c_str(), 4 + 4*n, 4));

      
      cout<<" SAR RLM type "<< nmm.sr().type() <<" from gal sv ";
      cout<< nmm.sr().gnsssv() << " beacon "<<hexstring <<" code "<<(int)nmm.sr().code()<<" params "<< makeHexDump(nmm.sr().params()) <<endl;
    }
    else if(nmm.type() == NavMonMessage::TimeOffsetType) {
      etstamp();
      cout<<" got a time-offset message with "<< nmm.to().offsets().size()<<" offsets: ";
      for(const auto& o : nmm.to().offsets()) {
        cout << "gnssid "<<o.gnssid()<<" offset " << o.offsetns() << " +- "<<o.tacc()<<" ("<<o.valid()<<") , ";
      }
      cout<<endl;
            
    }
    else {
      etstamp();
      cout<<"Unknown type "<< (int)nmm.type()<<endl;
    }
  }
}
 catch(EofException& ee)
   {}
