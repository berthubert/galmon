#include <stdio.h>
#include <string>
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
#include <mutex>
#include "ext/powerblog/h2o-pp.hh"
#include "minicurl.hh"
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
#include "tle.hh"
#include <optional>
#include "navmon.hh" 
#include <Tle.h>
#include "navparse.hh"
#include <fenv.h> 
#include "influxpush.hh"
#include "sbas.hh"

#include "CLI/CLI.hpp"
#include "gpscnav.hh"
#include "rtcm.hh"
#include "version.hh"


static char program[]="navparse";

using namespace std;

extern const char* g_gitHash;

struct ObserverPosition
{
  Point pos;
  double groundSpeed{-1};
  double accuracy{-1.0};
  string serialno;
  string hwversion;
  string swversion;
  string mods;
  string vendor;
  double clockOffsetNS{-1};
  double clockOffsetDriftNS{-1};
  double clockAccuracyNS{-1};
  double freqAccuracyPS{-1};
  time_t uptime;
  string githash;
  string owner;
  string remark;
  time_t lastSeen{0};
};
std::map<int, ObserverPosition> g_srcpos;

struct SBASAndReceiverStatus
{
  SBASState status;
  struct PerRecv
  {
    time_t last_seen{0};
  };
  map<int, PerRecv> perrecv;
};
typedef map<int, SBASAndReceiverStatus> sbas_t;
sbas_t g_sbas;
GetterSetter<sbas_t> g_sbaskeeper;

map<int, BeidouAlmanacEntry> g_beidoualma;
map<int, pair<GlonassMessage, GlonassMessage>> g_glonassalma;
map<int, GalileoMessage::Almanac> g_galileoalma;
map<int, GPSAlmanac> g_gpsalma;

GetterSetter<map<int, BeidouAlmanacEntry>> g_beidoualmakeeper;
GetterSetter<map<int, pair<GlonassMessage, GlonassMessage>>> g_glonassalmakeeper;
GetterSetter<map<int, GalileoMessage::Almanac>> g_galileoalmakeeper;
GetterSetter<map<int, GPSAlmanac>> g_gpsalmakeeper;

TLERepo g_tles;
struct GNSSReceiver
{
  Point position; 
};

double g_GSTUTCOffset, g_GSTGPSOffset, g_GPSUTCOffset, g_BeiDouUTCOffset, g_GlonassUTCOffset, g_GlonassGPSOffset;

/* 
The situation. We have a single ephemeris function that is able to operate on
Galileo, GPS1 and Beidou structs. It does so using functions like getT0e().
This ephemeris function also does speed and doppler. 

We have structs for all four GNSS.

All GNSS have ephemerides that are spread out over multiple messages. 
Because of that, we have to do some kind of atomic update.

We also have an SVStat that has some knowledge about IODs. 
It would be great is we could ask the svstat about "the latest ephemeris" 
and get a useful answer. 

More concretely, we could ask the svstat about the latest *position* or *speed*.
It would be easier to do that. There is no unified "ephemeris object" between all the GNSS.

We do need a unified "do we have a complete ephemeris method".

For Galileo this is messages 1, 2, 3 and 4
For GPS it is 2 and 3
For BeiDou it is message 2 and message 3 in succession (?)
   sow is in each message, forms a link
For GLONASS I don't really know

We also care about ephemeris discontinuities. 

Idea is that SVStat has four structs around, one for each GNSS, each containing "the lastest complete ephemeris".
   ephglomsg etc

   It also has infrastructure for storing the ephemerides as they are being assembled.

Open question: 

*/


// XXX conversion glonass??
int SVStat::wn() const
{
  if(gnss == 0)
    return gpsmsg.wn;
  else if(gnss == 2)
    return galmsg.wn;
  else if(gnss == 3)
    return beidoumsg.wn;
  else if(gnss == 6) {
    uint32_t glotime = glonassMessage.getGloTime(); // this starts GLONASS time at 31st of december 1995, 00:00 UTC
    return glotime / (7*86400);
  }
    
  return 0;
}

int SVStat::tow() const
{
  if(gnss == 0)
    return gpsmsg.tow;
  else if(gnss == 2)
    return galmsg.tow;
  else if(gnss == 3)
    return beidoumsg.sow;
  else if(gnss == 6) {
    uint32_t glotime = glonassMessage.getGloTime(); // this starts GLONASS time at 31st of december 1995, 00:00 UTC
    return glotime % (7*86400);
  }
  
  return 0;
}

const GPSLikeEphemeris& SVStat::liveIOD() const
{
  if(gnss == 0)
    return ephgpsmsg;
  else if(gnss == 2)
    return ephgalmsg;
  else if(gnss == 3)
    return ephBeidoumsg;

  throw std::runtime_error("Asked for GPS like ephemeris for gnss " + to_string(gnss));
}

const GPSLikeEphemeris& SVStat::prevIOD() const
{
  if(gnss == 0)
    return oldephgpsmsg;
  else if(gnss == 2)
    return oldephgalmsg;
  else if(gnss == 3)
    return oldephBeidoumsg;

  throw std::runtime_error("Asked for old-GPS like ephemeris for gnss " + to_string(gnss));
}


bool SVStat::completeIOD() const
{
  if(gnss == 6)
    return false;
  // yeah now what
  return true; 
}


double SVStat::getCoordinates(double tow, Point* p, bool quiet) const
{
  if(gnss == 6)
    return ::getCoordinates(tow, ephglomsg, p);

  // getCoordinates needs to be overloaded for GLONASS
  return ::getCoordinates(tow, liveIOD(), p, quiet);
  
}

double SVStat::getOldEphCoordinates(double tow, Point* p, bool quiet) const
{
  if(gnss == 6)
    return ::getCoordinates(tow, oldephglomsg, p);

  // getCoordinates needs to be overloaded for GLONASS
  return ::getCoordinates(tow, prevIOD(), p, quiet);
  
}


void SVStat::getSpeed(double tow, Vector* v) const
{
  return ::getSpeed(tow, liveIOD(), v);
}
DopplerData SVStat::doDoppler(double tow, const Point& us, double freq) const
{
  if(gnss == 6)
    return ::doDoppler(tow, us, ephglomsg, freq);
  else
    return ::doDoppler(tow, us, liveIOD(), freq);
}

double satUTCTime(const SatID& id);

void SVStat::reportNewEphemeris(const SatID& id, InfluxPusher& idb) 
{
  int ephage;
  if(gnss==6)
    ephage = ephAge(ephglomsg.getT0e(), oldephglomsg.getT0e());
  else
    ephage = ephAge(liveIOD().getT0e(), prevIOD().getT0e());
  
  Point p, oldp;
  getCoordinates(tow(), &p);
  getOldEphCoordinates(tow(), &oldp);

  double hours = ephage / 3600;
  double disco = Vector(p, oldp).length();
  //              cout<<id.first<<","<<id.second<<" discontinuity after "<< hours<<" hours: "<< disco <<endl;
  
  if(hours < 4) {
    latestDisco= disco;
    latestDiscoAge= ephage;
  }
  else
    latestDisco= -1;

  if(hours < 24) {
    idb.addValue(id,  "eph-disco",
                 {{"meters", disco},
                     {"seconds", hours*3600.0},
                       {"oldx", oldp.x},
                         {"oldy", oldp.y},
                           {"oldz", oldp.z},
                             {"x", p.x},
                               {"y", p.y},
                                 {"z", p.z},
                                   {"iod", gnss == 6 ? -1.0 : 1.0*liveIOD().getIOD()},
                                     {"oldiod", gnss== 6 ? -1.0 : 1.0*prevIOD().getIOD()}}, satUTCTime(id));
  }


}



svstats_t g_svstats;

GetterSetter<svstats_t> g_statskeeper;

int latestWN(int gnssid, const svstats_t& stats)
{
  map<int, SatID> ages;
  for(const auto& s: stats)
    if(s.first.gnss == (unsigned int)gnssid)
      ages[7*s.second.wn()*86400 + s.second.tow()]= s.first;
  if(ages.empty())
    throw runtime_error("Asked for latest WN for "+to_string(gnssid)+": we don't know it yet ("+to_string(stats.size())+")");
  
  return stats.find(ages.rbegin()->second)->second.wn();
}

int latestTow(int gnssid, const svstats_t& stats)
{
  map<int, SatID> ages;
  for(const auto& s: stats)
    if(s.first.gnss == (unsigned int) gnssid)
      ages[7*s.second.wn()*86400 + s.second.tow()]= s.first;
  if(ages.empty())
    throw runtime_error("Asked for latest TOW for "+to_string(gnssid)+": we don't know it yet ("+to_string(stats.size())+")");
  return stats.find(ages.rbegin()->second)->second.tow();
}


// nanoseconds posix time from that gnss WN and TOW
int64_t nanoTime(int gnssid, int wn, double tow)
{
  int offset;
  if(gnssid == 0) // GPS
    offset = 315964800;
  if(gnssid == 2) // Galileo, 22-08-1999
    offset = 935280000;
  if(gnssid == 3) {// Beidou, 01-01-2006 - I think leap seconds count differently in Beidou!! XXX
    offset = 1136073600;
    return 1000000000ULL*(offset + wn * 7*86400 + tow - g_dtLSBeidou); 
  }
  if(gnssid == 6) { // GLONASS
    offset = 820368000;
    return 1000000000ULL*(offset + wn * 7*86400 + tow);  // no leap seconds in glonass
  }
  
  return 1000000000ULL*(offset + wn * 7*86400 + tow - g_dtLS); 
}

// same in seconds 
double satUTCTime(const SatID& id)
{
  return nanoTime(id.gnss, g_svstats[id].wn(), g_svstats[id].tow())/1000000000.0;
}


/* The GST start epoch is defined as 13 seconds before midnight between 21st August and
22nd August 1999, i.e. GST was equal to 13 seconds at 22nd August 1999 00:00:00 UTC.  */

std::string humanTime(int gnssid, int wn, int tow)
{
  time_t t = nanoTime(gnssid, wn, tow)/1000000000;

  struct tm tm;
  gmtime_r(&t, &tm);

  char buffer[80];
  strftime(buffer, sizeof(buffer), "%a, %d %b %Y %T %z", &tm);
  return buffer;
}

std::optional<double> getHzCorrection(time_t now, int src, unsigned int gnssid, unsigned int sigid, const svstats_t& svstats)
{
  
  std::optional<double> allHzCorr;
  double alltot=0;
  int allcount=0;
  //  cout<<"getHzCorrection called for src "<<src<<" gnss "<<gnssid <<" sigid "<< sigid <<endl;
  for(const auto& s: svstats) {
    if(s.first.gnss != gnssid)
      continue;
    if(s.first.sigid != sigid)
      continue;
    if(auto iter = s.second.perrecv.find(src); iter != s.second.perrecv.end() && now - iter->second.deltaHzTime < 60) {
      //      cout<<"  Found entry for SV "<<s.first.gnss<<","<<s.first.sv<<","<<s.first.sigid<<" from src "<<iter->first<<", deltaHz: "<<iter->second.deltaHz<< " age " << now - iter->second.deltaHzTime<<" db "<<iter->second.db<<endl;
      alltot+=iter->second.deltaHz;
      allcount++;
    }
  }
  if(allcount > 3) {
    allHzCorr = alltot/allcount;
    //    cout<<"Returning "<<*allHzCorr<<endl;
  }
  else
    ; //    cout<<"Not enough data"<<endl;
  return allHzCorr;
}



std::string humanBhs(int bhs)
{
  static vector<string> options{"ok", "out of service", "will be out of service", "test"};
  if(bhs >= (int)options.size()) {
    cerr<<"Asked for humanBHS "<<bhs<<endl;
    return "??";
  }
  return options.at(bhs);
}

void addHeaders(h2o_req_t* req)
{
  h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_CACHE_CONTROL, 
                     NULL, H2O_STRLIT("max-age=3"));
  
  // Access-Control-Allow-Origin
  h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_ACCESS_CONTROL_ALLOW_ORIGIN, 
                 NULL, H2O_STRLIT("*"));

}

int main(int argc, char** argv)
try
{
  bool doVERSION{false};

  CLI::App app(program);
  string localAddress("127.0.0.1:29599");
  string htmlDir("./html");
  string influxDBName("null");
  
  app.add_flag("--version", doVERSION, "show program version and copyright");
  app.add_option("--bind,-b", localAddress, "Address to bind to");
  app.add_option("--html", htmlDir, "Where to source the HTML & JavaScript");
  app.add_option("--influxdb", influxDBName, "Name of influxdb database");
    
  try {
    app.parse(argc, argv);
  } catch(const CLI::Error &e) {
    return app.exit(e);
  }

  if(doVERSION) {
    showVersion(program, g_gitHash);
    exit(0);
  }

  //  feenableexcept(FE_DIVBYZERO | FE_INVALID | FE_OVERFLOW ); 

  
  //  g_tles.parseFile("active.txt");

  g_tles.parseFile("galileo.txt");
  g_tles.parseFile("glo-ops.txt");
  g_tles.parseFile("gps-ops.txt");
  g_tles.parseFile("beidou.txt");

  
  signal(SIGPIPE, SIG_IGN);
  InfluxPusher idb(influxDBName);
  MiniCurl::init();
  
  H2OWebserver h2s("galmon");
  
  h2s.addHandler("/global.json", [](auto handler, auto req) {
      addHeaders(req);
      
      nlohmann::json ret = nlohmann::json::object();
      auto svstats = g_statskeeper.get();
      ret["leap-seconds"] = g_dtLS;
      try {
        // if we haven't seen galileo, we have no idea
        ret["last-seen"]=utcFromGST(latestWN(2, svstats), latestTow(2, svstats));
      }
      catch(...)
        {
           try {
             ret["last-seen"]=utcFromGPS(latestWN(0, svstats), latestTow(0, svstats));        
           }
           catch(...)
           {}
        }
      
      ret["gst-utc-offset-ns"] = g_GSTUTCOffset;
      ret["gst-gps-offset-ns"] = g_GSTGPSOffset;
      ret["gps-utc-offset-ns"] = g_GPSUTCOffset;
      ret["beidou-utc-offset-ns"] = g_BeiDouUTCOffset;
      ret["glonass-utc-offset-ns"] = g_GlonassUTCOffset;
      ret["glonass-gps-offset-ns"] = g_GlonassGPSOffset;

      map<int, int> siggnsscount, svgnsscount;
      map<SatID, int> svcount, sigcount;
      map<int, int> receivers;
      time_t now=time(0);
      for(const auto& sv : svstats) {
        bool fresh=false;
        for(const auto& pr : sv.second.perrecv) {
          int age = now - pr.second.t;
          if(age < 30) {
            fresh= true;
            receivers[pr.first]++;
          }
        }
        if(fresh) {

          sigcount[sv.first]++;
          siggnsscount[sv.first.gnss]++;
          SatID id = sv.first;
          id.sigid=0;
          svcount[id]++;
        }
      }
      for(const auto& sv : svcount)
        svgnsscount[sv.first.gnss]++;

      ret["total-live-receivers"] = receivers.size();
      ret["total-live-svs"] = svcount.size();
      ret["total-live-signals"] = sigcount.size();
      ret["gps-svs"] = svgnsscount[0];
      ret["galileo-svs"] = svgnsscount[2];
      ret["beidou-svs"] = svgnsscount[3];
      ret["glonass-svs"] = svgnsscount[6];

      ret["gps-sigs"] = siggnsscount[0];
      ret["galileo-sigs"] = siggnsscount[2];
      ret["beidou-sigs"] = siggnsscount[3];
      ret["glonass-sigs"] = siggnsscount[6];

      return ret;
    });

  h2s.addHandler("/almanac.json", [](auto handler, auto req) {
      addHeaders(req);
      
      auto beidoualma = g_beidoualmakeeper.get();
      auto svstats = g_statskeeper.get();
      nlohmann::json ret = nlohmann::json::object();
      for(const auto& ae : beidoualma) {
        
        nlohmann::json item  = nlohmann::json::object();
        item["gnssid"]=3;
        if(ae.second.alma.getT0e() > 7*86400)
          continue;
        Point sat;
        getCoordinates(latestTow(3, svstats), ae.second.alma, &sat);
        item["eph-ecefX"]= sat.x/1000;
        item["eph-ecefY"]= sat.y/1000;
        item["eph-ecefZ"]= sat.z/1000;

        auto longlat = getLongLat(sat.x, sat.y, sat.z);
        item["eph-longitude"] = 180*longlat.first/M_PI;
        item["eph-latitude"]= 180*longlat.second/M_PI;
        item["t0e"] = ae.second.alma.getT0e();
        item["t"]= ephAge(ae.second.alma.getT0e(), latestTow(3, svstats))/86400.0;
        item["inclination"] = 180 * ae.second.alma.getI0() /M_PI;

        item["observed"]=false;
        if(auto iter = svstats.find({3, (uint32_t)ae.first, 0}); iter != svstats.end()) {
          if(time(0) - nanoTime(3, iter->second.wn(), iter->second.tow())/1000000000 < 300)
            item["observed"] = true;
        }
        
        if(ephAge(ae.second.alma.getT0e(), latestTow(3, svstats)) < 0) {
          auto match = g_tles.getBestMatch(nanoTime(3, latestWN(3, svstats), latestTow(3, svstats))/1000000000.0,
                                         sat.x, sat.y, sat.z);

          if(match.distance < 200000) {
            item["best-tle"] = match.name;
            item["best-tle-norad"] = match.norad;
            item["best-tle-int-desig"] = match.internat;
            item["best-tle-dist"] = match.distance/1000.0;
            
            item["tle-ecefX"] = match.ecefX/1000;
            item["tle-ecefY"] = match.ecefY/1000;
            item["tle-ecefZ"] = match.ecefZ/1000;
            
            item["tle-eciX"] = match.eciX/1000;
            item["tle-eciY"] = match.eciY/1000;
            item["tle-eciZ"] = match.eciZ/1000;
            
            item["tle-latitude"] = 180*match.latitude/M_PI;
            item["tle-longitude"] = 180*match.longitude/M_PI;
            item["tle-altitude"] = match.altitude;
          }
        }
        auto name = fmt::sprintf("C%02d", ae.first);
        item["name"]=name;
        ret[name] = item;
      }

      auto glonassalma = g_glonassalmakeeper.get();
      for(const auto& ae : glonassalma) {
        nlohmann::json item  = nlohmann::json::object();

        // ae.second.first -> even ae.second.sceond -> odd 
        item["gnssid"]=6;
        item["e"] = ae.second.first.getE();
        item["inclination"] = 180 * ae.second.first.getI0() /M_PI;
        item["health"] = ae.second.first.CnA;
        item["tlambdana"] = ae.second.second.gettLambdaNa();
        item["lambdana"] = ae.second.second.getLambdaNaDeg();
        item["hna"] = ae.second.second.hna;

        item["observed"] = false;
        for(uint32_t sigid : {0,1,2}) { // XXX SIGIDS
          if(auto iter = svstats.find({6, (uint32_t)ae.first, sigid}); iter != svstats.end()) {
            if(time(0) - nanoTime(6, iter->second.wn(), iter->second.tow())/1000000000 < 300) {
              item["observed"] = true;
              auto longlat = getLongLat(iter->second.glonassMessage.x, iter->second.glonassMessage.y, iter->second.glonassMessage.z);
              item["eph-longitude"] = 180*longlat.first/M_PI;
              item["eph-latitude"]= 180*longlat.second/M_PI;
              break;
            }
            
          }
        }


        auto name = fmt::sprintf("R%02d", ae.first);
        item["name"]=name;
        ret[name] = item;
      }

      auto galileoalma = g_galileoalmakeeper.get();
      for(const auto& ae : galileoalma) {
        nlohmann::json item  = nlohmann::json::object();
        item["gnssid"]=2;
        item["e"] = ae.second.getE();
        item["e1bhs"] = ae.second.e1bhs;
        item["e5bhs"] = ae.second.e5bhs;
        item["t0e"] = ae.second.getT0e();
        item["t"]= ephAge(ae.second.getT0e(), latestTow(2, svstats))/86400.0;
        item["eph-age"] = ephAge(latestTow(2, svstats), ae.second.getT0e());
        item["i0"] = 180.0 * ae.second.getI0()/ M_PI;
        item["inclination"] = 180 * ae.second.getI0() /M_PI;

        item["omega"] = ae.second.getOmega();
        item["sqrtA"]= ae.second.getSqrtA();
        item["M0"] = ae.second.getM0();
        item["delta-n"] = ae.second.getDeltan();
        item["omega-dot"] = ae.second.getOmegadot();
        item["omega0"] = ae.second.getOmega0();
        item["idot"] = ae.second.getIdot();
        item["t0e"] = ae.second.getT0e();

        Point sat;
        double E=getCoordinates(latestTow(2, svstats), ae.second, &sat);
        item["E"]=E;
        item["eph-ecefX"]= sat.x/1000;
        item["eph-ecefY"]= sat.y/1000;
        item["eph-ecefZ"]= sat.z/1000;

        auto longlat = getLongLat(sat.x, sat.y, sat.z);
        item["eph-longitude"] = 180*longlat.first/M_PI;
        item["eph-latitude"]= 180*longlat.second/M_PI;


        item["observed"] = false;
        for(uint32_t sigid : {0,1,5}) {
          if(auto iter = svstats.find({2, (uint32_t)ae.first, sigid}); iter != svstats.end()) {

            if(iter->second.completeIOD()) { 
              item["sisa"] = iter->second.galmsg.sisa;
            }
            // if we hit an 'observed', stop trying sigids
            if(time(0) - nanoTime(2, iter->second.wn(), iter->second.tow())/1000000000 < 300) {
              item["observed"] = true;
              break;
            }

            
          }
        }

        
        auto match = g_tles.getBestMatch(nanoTime(2, latestWN(2, svstats), latestTow(2, svstats))/1000000000.0,
                                         sat.x, sat.y, sat.z);
        
        if(match.distance < 200000) {
          item["best-tle"] = match.name;
          item["best-tle-norad"] = match.norad;
          item["best-tle-int-desig"] = match.internat;
          item["best-tle-dist"] = match.distance/1000.0;
          
          item["tle-ecefX"] = match.ecefX/1000;
          item["tle-ecefY"] = match.ecefY/1000;
          item["tle-ecefZ"] = match.ecefZ/1000;
          
          item["tle-eciX"] = match.eciX/1000;
          item["tle-eciY"] = match.eciY/1000;
          item["tle-eciZ"] = match.eciZ/1000;
          
          item["tle-latitude"] = 180*match.latitude/M_PI;
          item["tle-longitude"] = 180*match.longitude/M_PI;
          item["tle-altitude"] = match.altitude;
        }
        auto name = fmt::sprintf("E%02d", ae.first);
        item["name"]= name;
        ret[name] = item;
      }

      auto gpsalma = g_gpsalmakeeper.get();
      for(const auto& ae : gpsalma) {
        nlohmann::json item  = nlohmann::json::object();
        item["gnssid"]=0;
        item["e"] = ae.second.getE();
        item["health"] = ae.second.health;
        item["t0e"] = ae.second.getT0e();
        item["t"]= ephAge(ae.second.getT0e(), latestTow(0, svstats))/86400.0;
        item["eph-age"] = ephAge(latestTow(0, svstats), ae.second.getT0e());
        item["i0"] = 180.0 * ae.second.getI0()/ M_PI;
        item["inclination"] = 180 * ae.second.getI0() /M_PI;
        Point sat;
        getCoordinates(latestTow(0, svstats), ae.second, &sat);
        item["eph-ecefX"]= sat.x/1000;
        item["eph-ecefY"]= sat.y/1000;
        item["eph-ecefZ"]= sat.z/1000;

        auto longlat = getLongLat(sat.x, sat.y, sat.z);
        item["eph-longitude"] = 180*longlat.first/M_PI;
        item["eph-latitude"]= 180*longlat.second/M_PI;

        item["observed"] = false;
        for(uint32_t sigid : {0,1,4}) {
          if(auto iter = svstats.find({0, (uint32_t)ae.first, sigid}); iter != svstats.end()) {
            if(time(0) - nanoTime(0, iter->second.wn(), iter->second.tow())/1000000000 < 300)
              item["observed"] = true;
          }
        }

        
        auto match = g_tles.getBestMatch(nanoTime(0, latestWN(0, svstats), latestTow(0, svstats))/1000000000.0,
                                         sat.x, sat.y, sat.z);
        
        if(match.distance < 200000) {
          item["best-tle"] = match.name;
          item["best-tle-norad"] = match.norad;
          item["best-tle-int-desig"] = match.internat;
          item["best-tle-dist"] = match.distance/1000.0;
          
          item["tle-ecefX"] = match.ecefX/1000;
          item["tle-ecefY"] = match.ecefY/1000;
          item["tle-ecefZ"] = match.ecefZ/1000;
          
          item["tle-eciX"] = match.eciX/1000;
          item["tle-eciY"] = match.eciY/1000;
          item["tle-eciZ"] = match.eciZ/1000;
          
          item["tle-latitude"] = 180*match.latitude/M_PI;
          item["tle-longitude"] = 180*match.longitude/M_PI;
          item["tle-altitude"] = match.altitude;
        }
        auto name = fmt::sprintf("G%02d", ae.first);
        item["name"]=name;
        ret[name] = item;
      }
      
      return ret;
    });

  h2s.addHandler("/observers.json", [](auto handler, auto req) {
      addHeaders(req);
      
      nlohmann::json ret = nlohmann::json::array();
      for(const auto& src : g_srcpos) {
        nlohmann::json obj;
        obj["id"] = src.first;
        auto latlonh = ecefToWGS84(src.second.pos.x, src.second.pos.y, src.second.pos.z);
        get<0>(latlonh) *= 180.0/M_PI;
        get<1>(latlonh) *= 180.0/M_PI;
        get<0>(latlonh) = ((int)(10*get<0>(latlonh)))/10.0;
        get<1>(latlonh) = ((int)(10*get<1>(latlonh)))/10.0;

        get<2>(latlonh) = ((int)(10*get<2>(latlonh)))/10.0;

        obj["latitude"] = get<0>(latlonh);
        obj["longitude"] = get<1>(latlonh);
        obj["last-seen"] = src.second.lastSeen;
        obj["ground-speed"] = src.second.groundSpeed;
        obj["swversion"] = src.second.swversion;
        obj["hwversion"] = src.second.hwversion;
        obj["mods"] = src.second.mods;
        obj["serialno"] = src.second.serialno;
        obj["clockoffsetns"]= src.second.clockOffsetNS;
        obj["clockdriftns"]= src.second.clockOffsetDriftNS;
        obj["clockacc"]= src.second.clockAccuracyNS;
        obj["freqacc"]= src.second.freqAccuracyPS;
        obj["uptime"]= src.second.uptime;
        obj["githash"]= src.second.githash;
        obj["owner"]= src.second.owner;
        obj["vendor"]= src.second.vendor;
        obj["remark"]= src.second.remark;

        obj["acc"] = src.second.accuracy;
        obj["h"] = get<2>(latlonh);
        auto svstats = g_statskeeper.get();
        nlohmann::json svs = nlohmann::json::object();

        for(const auto& sv : svstats) {
          if(auto iter = sv.second.perrecv.find(src.first); iter != sv.second.perrecv.end()) {
            if(iter->second.db > 0 &&  time(0) - iter->second.t < 120) {
              nlohmann::json svo = nlohmann::json::object();
              svo["db"] = iter->second.db;

              svo["elev"] = roundf(10.0*iter->second.el)/10.0;
              svo["azi"] = roundf(10.0*iter->second.azi)/10.0;

              Point sat;
              
              if((sv.first.gnss == 0 || sv.first.gnss == 2) && sv.second.completeIOD()) {
                svo["delta_hz"] = truncPrec(iter->second.deltaHz, 2);
                auto hzCorrection = getHzCorrection(time(0), src.first , sv.first.gnss, sv.first.sigid, svstats);
                if(hzCorrection)
                  svo["delta_hz_corr"] = truncPrec(iter->second.deltaHz - *hzCorrection, 2);
                
                sv.second.getCoordinates(latestTow(sv.first.gnss, svstats), & sat);
              }
              if(sv.first.gnss == 3 && sv.second.ephBeidoumsg.sow >= 0 && sv.second.ephBeidoumsg.sqrtA != 0) {
                getCoordinates(latestTow(sv.first.gnss, svstats), sv.second.ephBeidoumsg, &sat);
              }
              if(sv.first.gnss == 6 && sv.second.wn() > 0) {
		getCoordinates(latestTow(6, svstats), sv.second.glonassMessage, &sat);
              }
              if(sat.x) {
                Point our = g_srcpos[iter->first].pos;
                svo["elev"] = roundf(10.0*getElevationDeg(sat, our))/10.0;
                svo["azi"] = roundf(10.0*getAzimuthDeg(sat, our))/10.0;
              }

              svo["prres"] = truncPrec(iter->second.prres, 2);
              svo["qi"] = iter->second.qi;
              svo["used"] = iter->second.used;
              svo["age-s"] = time(0) - iter->second.t;
              svo["last-seen"] = iter->second.t;
              svo["gnss"] = sv.first.gnss;
              svo["sv"] = sv.first.sv;
              svo["sigid"] = sv.first.sigid;
              svo["fullName"] = makeSatIDName(sv.first);
              svo["name"] = makeSatPartialName(sv.first);

              svs[makeSatIDName(sv.first)] = svo;
            }

          }
        }
        obj["svs"]=svs;
        
        ret.push_back(obj);
      }
      return ret;
    });

  h2s.addHandler("/sv.json", [](auto handler, auto req) {
      addHeaders(req);
      string_view path = convert(req->path);
      nlohmann::json ret = nlohmann::json::object();

      SatID id;
      auto pos = path.find("sv=");
      if(pos == string::npos) {
        return ret;
      }
      id.sv = atoi(&path[0] + pos+3);

      pos = path.find("gnssid=");
      if(pos == string::npos) {
        return ret;
      }
      id.gnss = atoi(&path[0]+pos+7);
      
      id.sigid = 1;
      pos = path.find("sigid=");
      if(pos != string::npos) {
        id.sigid = atoi(&path[0]+pos+6);
      }
      
      auto svstats = g_statskeeper.get();

      ret["sv"] = id.sv;
      ret["gnssid"] =id.gnss;
      ret["sigid"] = id.sigid;
      auto iter = svstats.find(id);
      if(iter == svstats.end())
        return ret;
      const auto& s= iter->second;
      // XXX CONVERSION
      /*
      ret["a0"] = s.a0;
      ret["a1"] = s.a1;
      ret["a0g"] = s.a0g;
      ret["a1g"] = s.a1g;
      */
      if(id.gnss == 2) {
        ret["sf1"] = s.galmsg.sf1;
        ret["sf2"] = s.galmsg.sf2;
        ret["sf3"] = s.galmsg.sf3;
        ret["sf4"] = s.galmsg.sf4;
        ret["sf5"] = s.galmsg.sf5;
        ret["BGDE1E5a"] = s.galmsg.BGDE1E5a;
        ret["BGDE1E5b"] = s.galmsg.BGDE1E5b;
        ret["e5bdvs"]=s.galmsg.e5bdvs;
        ret["e1bdvs"]=s.galmsg.e1bdvs;
        ret["e5bhs"]=s.galmsg.e5bhs;
        ret["e1bhs"]=s.galmsg.e1bhs;
      }
      // XXX CONVERSION
      /*
      ret["ai0"] = s.ai0;
      ret["ai1"] = s.ai1;
      ret["ai2"] = s.ai2;
      */
      ret["wn"] = s.wn();
      ret["tow"] = s.tow();
      // XXX CONVERSION
      /*
      ret["dtLS"] = s.dtLS;
      ret["dtLSF"] = s.dtLSF;
      ret["wnLSF"] = s.wnLSF;
      ret["dn"] = s.dn;
      */

      if(id.gnss == 3 && svstats[id].ephBeidoumsg.sow >= 0 && svstats[id].ephBeidoumsg.sqrtA != 0) {
        const auto& iod = svstats[id].ephBeidoumsg;
        ret["e"] = iod.getE();
        ret["omega"] = iod.getOmega();
        ret["sqrtA"]= iod.getSqrtA();
        ret["M0"] = iod.getM0();
        ret["i0"] = iod.getI0();
        ret["delta-n"] = iod.getDeltan();
        ret["omega-dot"] = iod.getOmegadot();
        ret["omega0"] = iod.getOmega0();
        ret["idot"] = iod.getIdot();
        ret["t0e"] = iod.getT0e();
        ret["t0c"] = iod.getT0c();
        ret["cuc"] = iod.getCuc();
        ret["cus"] = iod.getCus();
        ret["crc"] = iod.getCrc();
        ret["crs"] = iod.getCrs();
        ret["cic"] = iod.getCic();
        ret["cis"] = iod.getCis();
        //        ret["sisa"] = iod.sisa;
        ret["af0"] = iod.a0;
        ret["af1"] = iod.a1;
        ret["af2"] = iod.a2;
        Point p;
        s.getCoordinates(s.tow(), &p);
        ret["x"] = p.x;
        ret["y"] = p.y;
        ret["z"] = p.z;
        auto longlat = getLongLat(p.x, p.y, p.z);
        ret["longitude"] = 180.0*longlat.first/M_PI;
        ret["latitude"] = 180.0*longlat.second/M_PI;

      }
      else if(svstats[id].completeIOD()) {
        const auto& iod =  svstats[id].liveIOD();
        ret["iod"]= svstats[id].liveIOD().getIOD();
        ret["e"] = iod.getE();
        ret["omega"] = iod.getOmega();
        ret["sqrtA"]= iod.getSqrtA();
        ret["M0"] = iod.getM0();
        ret["i0"] = iod.getI0();
        ret["delta-n"] = iod.getDeltan();
        ret["omega-dot"] = iod.getOmegadot();
        ret["omega0"] = iod.getOmega0();
        ret["idot"] = iod.getIdot();
        ret["t0e"] = iod.getT0e();
        // XXX conversion 
        //        ret["t0c"] = iod.getT0c();
        ret["cuc"] = iod.getCuc();
        ret["cus"] = iod.getCus();
        ret["crc"] = iod.getCrc();
        ret["crs"] = iod.getCrs();
        ret["cic"] = iod.getCic();
        ret["cis"] = iod.getCis();
        // XXX conversion
        /*
        ret["sisa"] = iod.sisa;
        ret["af0"] = iod.af0;
        ret["af1"] = iod.af1;
        ret["af2"] = iod.af2;
        */
        Point p;
        s.getCoordinates(s.tow(), &p);
        ret["x"] = p.x;
        ret["y"] = p.y;
        ret["z"] = p.z;
        auto longlat = getLongLat(p.x, p.y, p.z);
        ret["longitude"] = 180.0*longlat.first/M_PI;
        ret["latitude"] = 180.0*longlat.second/M_PI;
       
      }

      nlohmann::json recvs = nlohmann::json::object();

      for(const auto& perrecv : s.perrecv) {
        nlohmann::json recv = nlohmann::json::object();
        recv["db"] = perrecv.second.db;
        recv["qi"] = perrecv.second.qi;
        recv["used"] = perrecv.second.used;
        recv["prres"] = truncPrec(perrecv.second.prres, 2);
        recv["elev"] = perrecv.second.el;
        recv["last-seen-s"] = time(0) - perrecv.second.t;
        recvs[std::to_string(perrecv.first)] = recv;
                
      }
      ret["recvs"]=recvs;
      return ret;
    }
    );

  h2s.addHandler("/cov.json", [](auto handler, auto req) {
      addHeaders(req);
      vector<Point> sats;
      auto galileoalma = g_galileoalmakeeper.get();
      auto gpsalma = g_gpsalmakeeper.get();
      auto beidoualma = g_beidoualmakeeper.get();
      auto svstats = g_statskeeper.get();
      //  cout<<"pseudoTow "<<pseudoTow<<endl;
      string_view path = convert(req->path);

      bool doGalileo{true}, doGPS{false}, doBeidou{false}, doGlonass{false};
      auto pos = path.find("gps=");
      if(pos != string::npos) {
        doGPS = (path[pos+4]=='1');
      }
      pos = path.find("galileo=");
      if(pos != string::npos) {
        doGalileo = (path[pos+8]=='1');
      }
      pos = path.find("beidou=");
      if(pos != string::npos) {
        doBeidou = (path[pos+7]=='1');
      }
      pos = path.find("glonass=");
      if(pos != string::npos) {
        doGlonass = (path[pos+8]=='1');
      }
      
      if(doGalileo)
      for(const auto &g : galileoalma) {
        Point sat;
        getCoordinates(latestTow(2, svstats), g.second, &sat);
        
        if(g.first < 0)
          continue;
        SatID id{2,(uint32_t)g.first,1};
        if(svstats[id].completeIOD() && svstats[id].galmsg.sisa == 255) {
          continue;
        }
        if(svstats[id].galmsg.e1bhs || svstats[id].galmsg.e1bdvs)
          continue;
        sats.push_back(sat);
      }

      if(doGPS)
      for(const auto &g : gpsalma) {
        Point sat;
        getCoordinates(latestTow(0, svstats), g.second, &sat);
        
        if(g.first < 0)
          continue;
        SatID id{0,(uint32_t)g.first,0};
        if(svstats[id].completeIOD() && svstats[id].gpsmsg.ura == 16) {
          //          cout<<"Skipping G"<<id.sv<<" because of URA"<<endl;
          continue;
        }
        if(svstats[id].gpsmsg.gpshealth) {
          //          cout<<"Skipping G"<<id.sv<<" because of health"<<endl;
          continue;
        }
        sats.push_back(sat);
      }

      if(doBeidou)
      for(const auto &g : beidoualma) {
        Point sat;
        getCoordinates(latestTow(3, svstats), g.second.alma, &sat);
        
        if(g.first < 0)
          continue;
        SatID id{3,(uint32_t)g.first,0};
        /*
        if(svstats[id].completeIOD() && svstats[id].ura == 16) {
          cout<<"Skipping G"<<id.sv<<" because of URA"<<endl;
          continue;
        }
        */
        if(svstats[id].gpsmsg.gpshealth) {
          //          cout<<"Skipping C"<<id.sv<<" because of health"<<endl;
          continue;
        }
        sats.push_back(sat);
      }

      // fake almanac from svstats -- to avoid ejecting Glonasses to another galaxy due
      // to excessive extrapolation, they freeze where they were last seen.
      if(doGlonass)
      for(const auto &s : svstats) {
	if(s.first.gnss == 6 && s.second.wn() > 0) {
	  Point sat;
	  getCoordinates(s.second.tow(), s.second.glonassMessage, &sat);
	  if(svstats[s.first].glonassMessage.Bn & 7) {
	    continue;
	  }
	  sats.push_back(sat);
	}
      }
      
      
      auto cov = emitCoverage(sats);
      auto ret = nlohmann::json::array();

      // ret = 
      // [ [90, [[-180, 3,2,1], [-179, 3,2,1], ... [180,3,2,1] ]]  
      //   [89, [[-180, 4], [-179, 4], ... [180,2] ]]
      // ]
      for(const auto& latvect : cov) {
        auto jslatvect = nlohmann::json::array();
        auto jslongvect = nlohmann::json::array();
        for(const auto& longpair : latvect.second) {
          auto jsdatum = nlohmann::json::array();
          jsdatum.push_back((int)get<0>(longpair));
          jsdatum.push_back(get<1>(longpair));
          jsdatum.push_back(get<2>(longpair));
          jsdatum.push_back(get<3>(longpair));
          jsdatum.push_back((int)(10*get<4>(longpair)));
          jsdatum.push_back((int)(10*get<5>(longpair)));
          jsdatum.push_back((int)(10*get<6>(longpair)));

          jsdatum.push_back((int)(10*get<7>(longpair)));
          jsdatum.push_back((int)(10*get<8>(longpair)));
          jsdatum.push_back((int)(10*get<9>(longpair)));

          jsdatum.push_back((int)(10*get<10>(longpair)));
          jsdatum.push_back((int)(10*get<11>(longpair)));
          jsdatum.push_back((int)(10*get<12>(longpair)));
          jslongvect.push_back(jsdatum);
        }
        jslatvect.push_back(latvect.first);
        jslatvect.push_back(jslongvect);
        
        ret.push_back(jslatvect);
      }
      return ret;
    });

  h2s.addHandler("/sbas.json", [](auto handler, auto req) {
      addHeaders(req);
      auto svstats = g_statskeeper.get();
      auto sbas = g_sbaskeeper.get();
      nlohmann::json ret = nlohmann::json::object();
      h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_CACHE_CONTROL, 
                     NULL, H2O_STRLIT("max-age=3"));

      // Access-Control-Allow-Origin
      h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_ACCESS_CONTROL_ALLOW_ORIGIN, 
                     NULL, H2O_STRLIT("*"));

      
      for(const auto& s: sbas) {
        nlohmann::json item  = nlohmann::json::object();
        item["last-seen"] = s.second.status.d_lastSeen;
        item["last-seen-s"] = time(0) - s.second.status.d_lastSeen;

        item["last-type-0"] = s.second.status.d_lastDNU;
        item["last-type-0-s"] = time(0) - s.second.status.d_lastDNU;

        // this 59 is to make sure galmonmon doesn't trigger on a single message
        if(s.second.status.d_lastSeen - s.second.status.d_lastDNU < 59)
          item["health"]="DON'T USE";
        else
          item["health"]="OK";
        
        nlohmann::json perrecv  = nlohmann::json::object();
        for(const auto& recv : s.second.perrecv) {
          perrecv[to_string(recv.first)]["last-seen"] = recv.second.last_seen;
          perrecv[to_string(recv.first)]["last-seen-s"] = time(0) - recv.second.last_seen;
        }
        item["perrecv"]=perrecv;
        
        ret[to_string(s.first)]=item;
      }
      return ret;
    });
  
  h2s.addHandler("/svs.json", [](auto handler, auto req) {
      addHeaders(req);
      auto svstats = g_statskeeper.get();
      nlohmann::json ret = nlohmann::json::object();
      h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_CACHE_CONTROL, 
                     NULL, H2O_STRLIT("max-age=3"));

      // Access-Control-Allow-Origin
      h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_ACCESS_CONTROL_ALLOW_ORIGIN, 
                     NULL, H2O_STRLIT("*"));

      
      for(const auto& s: svstats) {
        nlohmann::json item  = nlohmann::json::object();
        if(!s.second.tow()) // I know, I know, will suck briefly
          continue;

        item["gnssid"] = s.first.gnss;
        item["svid"] = s.first.sv;
        item["sigid"] = s.first.sigid;
        // perhaps check ephBeidoumsg for sow >=0 as 'completeIOD'?

        if(s.first.gnss == 3) { // beidou
          item["sisa"]=humanUra(s.second.ephBeidoumsg.urai);
          item["sisa-m"]=numUra(s.second.ephBeidoumsg.urai);
          if(s.second.completeIOD()) {
            item["eph-age-m"] = ephAge(s.second.tow(), s.second.ephBeidoumsg.getT0e())/60.0;

          }

          Point p;
          if(s.second.ephBeidoumsg.sqrtA != 0) {
            getCoordinates(s.second.tow(), s.second.ephBeidoumsg, &p);
            auto beidoualma = g_beidoualmakeeper.get();
            if(auto iter = beidoualma.find(s.first.sv); iter != beidoualma.end()) {
              Point almapos;
              getCoordinates(s.second.tow(), iter->second.alma, &almapos);
              item["alma-dist"] = truncPrec(Vector(almapos, p).length()/1000.0, 1);
            }
          }
        }
        else if(s.first.gnss == 6) { // glonass
          if(s.second.glonassMessage.FT < 16) {
            item["sisa"] = humanFt(s.second.glonassMessage.FT);
            item["sisa-m"] = numFt(s.second.glonassMessage.FT);
          }
          item["aode"] = s.second.ephglomsg.En*24;
          item["iod"] = s.second.ephglomsg.Tb;

          time_t glonow =  nanoTime(6, s.second.wn(), s.second.tow())/1000000000.0;
          // the 820368000 stuff is to rebase to 'day 1' so the % works
          auto pseudoTow = (getGlonassT0e(glonow, s.second.glonassMessage.Tb) - 820368000) % (7*86400);
          
          //          cout<<std::fixed<<"wn: "<<s.second.wn() <<" tow "<<s.second.tow() <<" " << (int) s.second.glonassMessage.Tb << " " << glonow <<" " << humanTime(glonow) <<" ";
          //          cout<< pseudoTow <<" " <<  ephAge(s.second.tow(), getGlonassT0e(pseudoTow, s.second.glonassMessage.Tb))/60.0<<endl;
          item["eph-age-m"] = ephAge(s.second.tow(), getGlonassT0e(pseudoTow, s.second.glonassMessage.Tb))/60.0;

          // this should actually use local time!
          Point p;
          s.second.getCoordinates(s.second.tow(), &p);
          auto match = g_tles.getBestMatch(nanoTime(s.first.gnss, s.second.wn(), s.second.tow())/1000000000.0,
                                           p.x, p.y, p.z);

          item["best-tle"] = match.name;
          item["best-tle-norad"] = match.norad;
          item["best-tle-int-desig"] = match.internat;
          item["best-tle-dist"] = match.distance/1000.0;
        }
        if(s.second.completeIOD()) {
          item["iod"]=s.second.liveIOD().getIOD();
          if(s.first.gnss == 0 || s.first.gnss == 3) {
            item["sisa"]=humanUra(s.second.gpsmsg.ura);
            item["sisa-m"]=truncPrec(numUra(s.second.gpsmsg.ura),3);
            //            cout<<"Asked to convert "<<s.second.ura<<" for sv "<<s.first.first<<","<<s.first.second<<endl;
          }
          else {
            item["sisa"]=humanSisa(s.second.galmsg.sisa);
            item["sisa-m"]=numSisa(s.second.galmsg.sisa);
          }
          item["eph-age-m"] = ephAge(s.second.tow(), s.second.liveIOD().getT0e())/60;
          // CONVERSION
          item["af0"] = 0; // s.second.liveIOD().af0;
          item["af1"] = 0; //s.second.liveIOD().af1;
          item["af2"] = 0; // (int)s.second.liveIOD().af2;
          // XXX conversion
          //          item["t0c"] = s.second.liveIOD().getT0c();
          

          if(s.second.rtcmEphDelta.id.gnss == s.first.gnss && s.second.rtcmEphDelta.id.sv == s.first.sv && s.second.rtcmEphDelta.iod == s.second.liveIOD().getIOD()
             && abs(s.second.rtcmEphDelta.sow - s.second.tow())<60) {
            const auto& ed = s.second.rtcmEphDelta;
            item["rtcm-eph-delta-cm"] = truncPrec(sqrt(ed.radial*ed.radial + ed.along*ed.along + ed.cross*ed.cross)/10.0, 2);
            item["rtcm-eph-radial-cm"] = truncPrec(ed.radial/10.0, 2);
            item["rtcm-eph-along-cm"] = truncPrec(ed.along/10.0, 2);
            item["rtcm-eph-cross-cm"] = truncPrec(ed.cross/10.0, 2);
            item["rtcm-eph-dradial-cm"] = truncPrec(ed.dradial/10.0, 2);
            item["rtcm-eph-dalong-cm"] = truncPrec(ed.dalong/10.0, 2);
            item["rtcm-eph-dcross-cm"] = truncPrec(ed.dcross/10.0, 2);
            
          }
          
          Point p;
          Point core;
          
          // this should actually use local time!
          s.second.getCoordinates(s.second.tow(), &p);
          auto match = g_tles.getBestMatch(nanoTime(s.first.gnss, s.second.wn(), s.second.tow())/1000000000.0,
                                           p.x, p.y, p.z);

          item["best-tle"] = match.name;
          item["best-tle-norad"] = match.norad;
          item["best-tle-int-desig"] = match.internat;
          item["best-tle-dist"] = match.distance/1000.0;

          
          item["x"]=p.x;
          item["y"]=p.y;
          item["z"]=p.z;

          if(s.first.gnss == 0) {
            auto gpsalma = g_gpsalmakeeper.get();
            if(auto iter = gpsalma.find(s.first.sv); iter != gpsalma.end()) {
              Point almapos;
              getCoordinates(s.second.tow(), iter->second, &almapos);
              item["alma-dist"] = Vector(almapos, p).length()/1000.0;
            }
          }
          if(s.first.gnss == 2) {
            auto galileoalma = g_galileoalmakeeper.get();
            if(auto iter = galileoalma.find(s.first.sv); iter != galileoalma.end()) {
              Point almapos;
              getCoordinates(s.second.tow(), iter->second, &almapos);
              item["alma-dist"] = Vector(almapos, p).length()/1000.0;
            }
          }
          
          
        }
        // XX conversion
        /*
        item["a0"]=s.second.a0;
        item["a1"]=s.second.a1;
        */
        if(s.first.gnss == 0) { // GPS
          auto deltaUTC = getGPSUTCOffset(s.second.tow(), s.second.wn(), s.second.gpsmsg);
          item["delta-utc"] = fmt::sprintf("%.1f %+.1f/d", deltaUTC.first, deltaUTC.second * 86400);

          // CONVERSION
          //          item["t0t"] = s.second.gpsmsg.t0t;
          // item["wn0t"] = s.second.gpsmsg.wn0t;
        }
        if(s.first.gnss == 2) {
          auto deltaUTC = s.second.galmsg.getUTCOffset(s.second.tow(), s.second.wn());
          item["delta-utc"] = fmt::sprintf("%.1f %+.1f/d", deltaUTC.first, deltaUTC.second * 86400);
          auto deltaGPS = s.second.galmsg.getGPSOffset(s.second.tow(), s.second.wn());
          item["delta-gps"] = fmt::sprintf("%.1f %+.1f/d", deltaGPS.first, deltaGPS.second * 86400);
          item["t0t"] = s.second.galmsg.t0t;
          item["wn0t"] = s.second.galmsg.wn0t;
        }
        if(s.first.gnss == 3) {
          auto deltaUTC = s.second.ephBeidoumsg.getUTCOffset(s.second.ephBeidoumsg.sow);
          item["delta-utc"] = fmt::sprintf("%.1f %+.1f/d", deltaUTC.first, deltaUTC.second * 86400);

          auto deltaGPS = s.second.ephBeidoumsg.getGPSOffset(s.second.ephBeidoumsg.sow);
          item["delta-gps"] = fmt::sprintf("%.1f %+.1f/d", deltaGPS.first, deltaGPS.second * 86400);
          item["t0g"] =0;
          item["wn0g"] = 0;
          
          item["t0t"] = 0;
          item["wn0t"] = 0;
        }


        // XXX conversion
        //        item["dtLS"]=s.second.dtLS;

        if(s.first.gnss == 3) {  // beidou
          item["a0g"]=s.second.ephBeidoumsg.a0gps;
          item["a1g"]=s.second.ephBeidoumsg.a1gps;
          if(s.second.ephBeidoumsg.aode >= 0)
            item["aode"]=s.second.ephBeidoumsg.aode;
          if(s.second.ephBeidoumsg.aodc >= 0)
            item["aodc"]=s.second.ephBeidoumsg.aodc;

          item["health"] = s.second.beidoumsg.sath1 ? "NOT OK" : "OK";
          item["healthissue"] = !!s.second.beidoumsg.sath1;
        }

        if(s.first.gnss == 6) { // GLONASS
          auto deltaUTC = s.second.glonassMessage.getUTCOffset(0);
          item["delta-utc"] = fmt::sprintf("%.1f %+.1f/d", deltaUTC.first, deltaUTC.second * 86400);

          auto deltaGPS = s.second.glonassMessage.getGPSOffset(0);
          item["delta-gps"] = fmt::sprintf("%.1f %+.1f/d", deltaGPS.first, deltaGPS.second * 86400);
        }
        
        if(s.first.gnss == 2) {  // galileo
          item["a0g"]=s.second.galmsg.a0g;
          item["a1g"]=s.second.galmsg.a1g;
          item["t0g"]=s.second.galmsg.t0g;
          item["wn0g"]=s.second.galmsg.wn0g;

          item["health"] =
            humanBhs(s.second.galmsg.e1bhs)                       +"/" +
            humanBhs(s.second.galmsg.e5bhs)                      +"/" +
            (s.second.galmsg.e1bdvs ? "NG" : "val") +"/"+
            (s.second.galmsg.e5bdvs ? "NG" : "val");
          item["e5bdvs"]=s.second.galmsg.e5bdvs;
          item["e1bdvs"]=s.second.galmsg.e1bdvs;
          item["e5bhs"]=s.second.galmsg.e5bhs;
          item["e1bhs"]=s.second.galmsg.e1bhs;
          item["healthissue"]=0;
          if(s.second.galmsg.e1bhs == 2 || s.second.galmsg.e5bhs == 2)
            item["healthissue"] = 1;
          if(s.second.galmsg.e1bhs == 3 || s.second.galmsg.e5bhs == 3)
            item["healthissue"] = 1;
          if(s.second.galmsg.e1bdvs || s.second.galmsg.e5bdvs || s.second.galmsg.e1bhs == 1 || s.second.galmsg.e5bhs == 1)
            item["healthissue"] = 2;
          
        }
        else if(s.first.gnss == 0) {
          item["health"] =s.second.gpsmsg.gpshealth ? ("NOT OK: "+to_string(s.second.gpsmsg.gpshealth)) : string("OK");
          item["healthissue"]= 2* !!s.second.gpsmsg.gpshealth;
        }
        else if(s.first.gnss == 6) { // GLONASS
          item["health"]= s.second.glonassMessage.Bn ? ("NOT OK: "+to_string(s.second.glonassMessage.Bn)) : string("OK");
          item["healthissue"]= 2* !!s.second.glonassMessage.Bn;
        }
        
        nlohmann::json perrecv  = nlohmann::json::object();
        for(const auto& pr : s.second.perrecv) {
          if(pr.second.db > 0 || (time(0) - pr.second.t < 1800)) {
            nlohmann::json det  = nlohmann::json::object();
            det["elev"] = pr.second.el;
            Point sat;
                        
            if(s.second.completeIOD())
              s.second.getCoordinates(latestTow(s.first.gnss, svstats), & sat);

            if(sat.x) {
              Point our = g_srcpos[pr.first].pos;
              det["elev"] = roundf(10.0*getElevationDeg(sat, our))/10.0;
              det["azi"] = roundf(10.0*getAzimuthDeg(sat, our))/10.0;
            }
            else
              det["elev"] = pr.second.el;
            
            det["db"] = pr.second.db;
            det["last-seen-s"] = time(0) - pr.second.t;
            det["prres"] = truncPrec(pr.second.prres, 2);
            det["qi"] = pr.second.qi;
            det["used"] = pr.second.used;

            if(time(0) - pr.second.deltaHzTime < 60) {
              det["delta_hz"] = pr.second.deltaHz;
              auto hzCorrection = getHzCorrection(time(0), pr.first, s.first.gnss, s.first.sigid, svstats);
              if(hzCorrection)
                det["delta_hz_corr"] = pr.second.deltaHz - *hzCorrection;
            }

            
            perrecv[to_string(pr.first)]=det;
          }
        }
        item["perrecv"]=perrecv;

        item["last-seen-s"] = time(0) - nanoTime(s.first.gnss, s.second.wn(), s.second.tow())/1000000000;

        if(s.second.latestDisco >=0) {
          item["latest-disco"]= truncPrec(s.second.latestDisco, 3);
          item["latest-disco-age"]= s.second.latestDiscoAge;
        }
        if(s.second.timeDisco > -100 && s.second.timeDisco < 100) {
          item["time-disco"]= truncPrec(s.second.timeDisco, 1);
        }

        
        item["wn"] = s.second.wn();
        item["tow"] = s.second.tow();
        item["fullName"] = makeSatIDName(s.first);
        item["name"] = makeSatPartialName(s.first);
        ret[makeSatIDName(s.first)] = item;
      }
      return ret;
    });

  h2s.addHandler("/sbstatus.json", [](auto handler, auto req) {
      addHeaders(req);
      auto svstats = g_statskeeper.get();
      h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_CACHE_CONTROL, 
                     NULL, H2O_STRLIT("max-age=3"));

      // Access-Control-Allow-Origin
      h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_ACCESS_CONTROL_ALLOW_ORIGIN, 
                     NULL, H2O_STRLIT("*"));

      auto ret = nlohmann::json::array();

      time_t now = time(0);
      for(const auto& s: svstats) {
        for(const auto& sb : s.second.sbas) {
          if(now - sb.second.longterm.lastUpdate > 120)
            continue;
          if(now - sb.second.fast.lastUpdate > 120)
            continue;
          if(sb.second.fast.udrei == 14)
            continue;

          auto obj = nlohmann::json::object();
          obj["name"] = makeSatPartialName(s.first);
          obj["sbasprn"] = sb.first;

          obj["dx"] = sb.second.longterm.dx;
          obj["dy"] = sb.second.longterm.dy;
          obj["dz"] = sb.second.longterm.dz;
          obj["dai"] = sb.second.longterm.dai;
          obj["iod8"] = sb.second.longterm.iod8;
          obj["velocity"] = sb.second.longterm.velocity;

          obj["last-longterm-update-s"] = now - sb.second.longterm.lastUpdate;
          obj["last-fast-update-s"] = now - sb.second.fast.lastUpdate;

     
          if(sb.second.longterm.velocity) {
            obj["toa"] = sb.second.longterm.toa;
            int toadelta = (now % 86400) - sb.second.longterm.toa;
            
            obj["toa-delta"] = toadelta;

            obj["ddx"] = sb.second.longterm.ddx;
            obj["ddy"] = sb.second.longterm.ddy;
            obj["ddz"] = sb.second.longterm.ddz;
            obj["ddai"] = sb.second.longterm.ddai;

            obj["adx"] = sb.second.longterm.dx + toadelta*sb.second.longterm.ddx;
            obj["ady"] = sb.second.longterm.dy + toadelta*sb.second.longterm.ddy;
            obj["adz"] = sb.second.longterm.dz + toadelta*sb.second.longterm.ddz;
            obj["adai"] = sb.second.longterm.dai + toadelta*sb.second.longterm.ddai;

            
          }
          obj["correction"] = sb.second.fast.correction;
          obj["udrei"] = sb.second.fast.udrei;

          if(s.second.completeIOD() && (s.second.liveIOD().getIOD() & 0xff) == sb.second.longterm.iod8) {
            Point sat;
            
            s.second.getCoordinates(s.second.tow(), &sat);
            Point adjsat=sat;
            adjsat.x += sb.second.longterm.dx;
            adjsat.y += sb.second.longterm.dy;
            adjsat.z += sb.second.longterm.dz;
            Point sbasCenter;
            int prn = sb.first;
            if(prn== 126 || prn == 136 || prn == 123)
              sbasCenter = c_egnosCenter;
            else if(prn == 138 || prn == 131 || prn == 133)
              sbasCenter = c_waasCenter;
            else
              sbasCenter = Point{0,0,0};
            
            double dist = Vector(sbasCenter, adjsat).length() - Vector(sbasCenter, sat).length();
            obj["space-shift"] = dist;
            dist -= sb.second.longterm.dai / 3;
            obj["eph-shift"] = dist;
            obj["range-shift"] = dist - sb.second.fast.correction;
          }

          
          ret.push_back(obj);
        }
      }
      return ret;
    });

  
  h2s.addDirectory("/", htmlDir);

  const char *address = localAddress.c_str();
  std::thread ws([&h2s, address]() {
      auto actx = h2s.addContext();
      ComboAddress listenOn(address);
      h2s.addListener(listenOn, actx);
      cout<<"Listening on "<< listenOn.toStringWithPort() <<endl;
      h2s.runLoop();
    });
  ws.detach();

  try {
  for(;;) {
    static time_t lastWebSync;
    if(lastWebSync != time(0)) {
      g_statskeeper.set(g_svstats);
      g_sbaskeeper.set(g_sbas);
      g_galileoalmakeeper.set(g_galileoalma);
      g_glonassalmakeeper.set(g_glonassalma);
      g_beidoualmakeeper.set(g_beidoualma);
      g_gpsalmakeeper.set(g_gpsalma);
      lastWebSync = time(0);
    }

    char bert[6];
    // I apologise deeply
    if(fread(bert, 1, 6, stdin) != 6 || bert[0]!='b' || bert[1]!='e' || bert[2] !='r' || bert[3]!='t') {
      cerr<<"EOF or bad magic"<<endl;
      break;
    }
    
    uint16_t len;
    memcpy(&len, bert+4, 2);
    len = htons(len);
    char buffer[len];
    if(fread(buffer, 1, len, stdin) != len)
      break;

    
    NavMonMessage nmm;
    nmm.ParseFromString(string(buffer, len));
    /*
    static time_t lastCovSyncHour;
    if(nmm.localutcseconds() / 1800 > (unsigned int)lastCovSyncHour) {
      lastCovSyncHour = nmm.localutcseconds() / 1800;
      int tow;
      static int totexceeds, totcells;
      try {
        tow=latestTow(2, g_svstats);
        vector<Point> sats;
        for(const auto &g : g_galileoalma) {
          Point sat;
          getCoordinates(tow, g.second, &sat);
          
          if(g.first < 0)
            continue;
          SatID id{2,(uint32_t)g.first,1};
          const auto& svstat = g_svstats[id];
          if(svstat.completeIOD() && svstat.liveIOD().sisa == 255) {
            continue;
          }
          if(svstat.e1bhs || svstat.e1bdvs)
            continue;
          sats.push_back(sat);
        }
        
        auto cov = emitCoverage(sats);
        int exceeds=0, cells=0;
        for(const auto& latvect : cov) {
          for(const auto& longpair : latvect.second) {
            cells++;
            if(get<4>(longpair) >= 6.0)
              exceeds++;
            //            else
            //cout<<get<6>(longpair) << endl;
          }
        }
        totexceeds += exceeds;
        totcells += cells;
        fmt::printf("At %s, %.2f%% (%d) of %d cells exceeded PDOP 6 for 5 degrees horizon (%d sats), running %.2f%%\n", humanTime(nmm.localutcseconds()), 100.0*exceeds/cells, exceeds, cells, sats.size(), 100.0*totexceeds/totcells);
      }
      catch(std::exception&e) {
        cout<<"Error with coverage: "<<e.what()<<endl;
      }

    }
    */
    

    
    if(nmm.type() == NavMonMessage::ReceptionDataType) {
      int gnssid = nmm.rd().gnssid();
      int sv = nmm.rd().gnsssv();
      int sigid = nmm.rd().sigid();
      if(gnssid==2 && sigid == 0)
        sigid = 1;
      
      SatID id{(uint32_t)gnssid, (uint32_t)sv, (uint32_t)sigid};
      auto& perrecv = g_svstats[id].perrecv[nmm.sourceid()];

      perrecv.db = nmm.rd().db();
      
      perrecv.el = nmm.rd().el();
      perrecv.azi = nmm.rd().azi();

      perrecv.prres = nmm.rd().prres();
      if(nmm.rd().has_qi())
        perrecv.qi = nmm.rd().qi();
      else
        perrecv.qi = -1;

      if(nmm.rd().has_used())
        perrecv.used = nmm.rd().used();
      else
        perrecv.used = -1;
      
      Point sat{0,0,0};
      //cout<<"Got recdata for "<<id.gnss<<","<<id.sv<<","<<id.sigid<<": count="<<g_svstats.count(id)<<endl;
      if(g_svstats[id].completeIOD() && (id.gnss != 6 || !(random() % 16))) { // glonass is too slow
        g_svstats[id].getCoordinates(g_svstats[id].tow(), &sat);
      }
      
      if(sat.x != 0 && g_srcpos[nmm.sourceid()].pos.x != 0) {
        if(!(random() % 4))
        idb.addValue(id, "recdata",
                     {
                     {"db", nmm.rd().db()},
                       {"azi", getAzimuthDeg(sat, g_srcpos[nmm.sourceid()].pos)},
                         {"ele", getElevationDeg(sat, g_srcpos[nmm.sourceid()].pos)},
                           {"prres", nmm.rd().prres()},
                             {"qi", perrecv.qi},
                               {"used", perrecv.used}
                     }, nmm.localutcseconds() + nmm.localutcnanoseconds()/1000000000.0, nmm.sourceid());
      }

    }
    else if(nmm.type() == NavMonMessage::GalileoInavType) {
      basic_string<uint8_t> inav((uint8_t*)nmm.gi().contents().c_str(), nmm.gi().contents().size());
      int sv = nmm.gi().gnsssv();
      int sigid;
      if(nmm.gi().has_sigid())
        sigid = nmm.gi().sigid();
      else
        sigid = 1;  // default to E1B
      SatID id={2,(uint32_t)sv,(uint32_t)sigid};

      // XXX conversion, may be vital
      // g_svstats[id].wn = nmm.gi().gnsswn();
      
      auto& svstat = g_svstats[id];
      svstat.gnss = id.gnss;
      auto oldgm = svstat.galmsg;

      auto& gm = svstat.galmsg;
      unsigned int wtype = gm.parse(inav);

      
      if(wtype == 5 && svstat.galmsgTyped.count(5)) {
        const auto& old5gm = svstat.galmsgTyped[5];
        if(make_tuple(old5gm.e5bhs, old5gm.e1bhs, old5gm.e5bdvs, old5gm.e1bdvs) !=
           make_tuple(gm.e5bhs, gm.e1bhs, gm.e5bdvs, gm.e1bdvs)) {
          cout<<humanTime(id.gnss, svstat.wn(), svstat.tow())<<" src "<<nmm.sourceid()<<" Galileo "<<id.sv <<" sigid "<<id.sigid<<" change in health: ["<<humanBhs(old5gm.e5bhs)<<", "<<humanBhs(old5gm.e1bhs)<<", "<<(int)old5gm.e5bdvs <<", " << (int)old5gm.e1bdvs<<"] -> ["<< humanBhs(gm.e5bhs)<<", "<< humanBhs(gm.e1bhs)<<", "<< (int)gm.e5bdvs <<", " << (int)gm.e1bdvs<<"], lastseen "<<ephAge(old5gm.tow, gm.tow)/3600.0 <<" hours"<<endl;
        }
      }
      
      svstat.galmsgTyped[wtype] = gm;

      if(wtype == 1 || wtype == 2 || wtype == 3 || wtype == 4) {
        idb.addValue(id, "ephemeris", {{"iod-live", svstat.galmsg.iodnav},
              {"eph-age", ephAge(gm.tow, gm.getT0e())}}, satUTCTime(id));

        int w = 1;
        for(; w < 5; ++w) {
          if(!svstat.galmsgTyped.count(w))
            break;
          if(w > 1 && svstat.galmsgTyped[w-1].iodnav != svstat.galmsgTyped[w].iodnav)
            break;
        }
        if(w==5) {
          if(svstat.ephgalmsg.iodnav != svstat.galmsgTyped[1].iodnav) {
            svstat.oldephgalmsg = svstat.ephgalmsg;
            svstat.ephgalmsg = svstat.galmsgTyped[wtype];
            svstat.reportNewEphemeris(id, idb);
          }
        }
        
      }

      
      // XXX conversion possibly vital
      //      g_svstats[id].tow = nmm.gi().gnsstow();

      //      g_svstats[id].perrecv[nmm.sourceid()].wn = nmm.gi().gnsswn();
      //      g_svstats[id].perrecv[nmm.sourceid()].tow = nmm.gi().gnsstow();
      g_svstats[id].perrecv[nmm.sourceid()].t = nmm.localutcseconds();

      if(wtype >=1 && wtype <= 4) { // ephemeris 
        if(wtype == 3) {
          idb.addValue(id, "sisa", {{"value", g_svstats[id].galmsg.sisa}}, satUTCTime(id));
        }
        else if(wtype == 4) {
          idb.addValue(id, "clock", {{"offset_ns", svstat.galmsg.getAtomicOffset(svstat.tow()).first}, 
                  {"t0c", g_svstats[id].galmsg.t0c*60}, // getT0c()??
                {"af0", g_svstats[id].galmsg.af0},
                  {"af1", g_svstats[id].galmsg.af1},
                    {"af2", g_svstats[id].galmsg.af2}}, satUTCTime(id)); 


            if(oldgm.af0 && oldgm.t0c != svstat.galmsg.t0c) {
              auto oldOffset = oldgm.getAtomicOffset(svstat.tow());
              auto newOffset = svstat.galmsg.getAtomicOffset(svstat.tow());
              svstat.timeDisco = oldOffset.first - newOffset.first;
              if(fabs(svstat.timeDisco) < 10000)
                idb.addValue(id, "clock_jump_ns", {{"value", svstat.timeDisco}}, satUTCTime(id));
            }
          }
        }
        else if(wtype == 5) {
          idb.addValue(id, "iono", {
              {"ai0", g_svstats[id].galmsg.ai0},
                {"ai1", g_svstats[id].galmsg.ai1},
                  {"ai2", g_svstats[id].galmsg.ai2},
                    {"sf1", g_svstats[id].galmsg.sf1},
                      {"sf2", g_svstats[id].galmsg.sf2},
                        {"sf3", g_svstats[id].galmsg.sf3},
                          {"sf4", g_svstats[id].galmsg.sf4},
                            {"sf5", g_svstats[id].galmsg.sf5}}, satUTCTime(id));

          
          idb.addValue(id, "galbgd", {
              {"BGDE1E5a", g_svstats[id].galmsg.BGDE1E5a},
                {"BGDE1E5b", g_svstats[id].galmsg.BGDE1E5b}}, satUTCTime(id));

          
          idb.addValue(id, "galhealth", {
              {"e1bhs", g_svstats[id].galmsg.e1bhs}, 
                {"e5bhs", g_svstats[id].galmsg.e5bhs},
                  {"e5bdvs", g_svstats[id].galmsg.e5bdvs},
                    {"e1bdvs", g_svstats[id].galmsg.e1bdvs}}, satUTCTime(id));
        }
        else if(wtype == 6) {  // GST-UTC
          const auto& sv = g_svstats[id];
          g_GSTUTCOffset = sv.galmsg.getUTCOffset(sv.tow(), sv.wn()).first;
          idb.addValue(id, "utcoffset", {
              {"a0", sv.galmsg.a0},
                {"a1", sv.galmsg.a1},
                  {"t0t", sv.galmsg.t0t},
                    {"delta", g_GSTUTCOffset}
            },
            satUTCTime(id));
          
          g_dtLS = sv.galmsg.dtLS;
        }
        else if(wtype == 7) {
          // this contains first part of alma1
        }
        else if(wtype == 8) {
          if(gm.tow - svstat.galmsgTyped[7].tow < 5 &&
             svstat.galmsgTyped[7].alma1.svid && gm.iodalmanac == svstat.galmsgTyped[7].iodalmanac) {
            //            cout<<(int)wtype<<" alma-sv "<<gm.alma1.svid<< " "<<gm.alma1.deltaSqrtA<<" " <<gm.alma1.t0almanac << endl;
            g_galileoalma[gm.alma1.svid] = gm.alma1;
          }
          
        }
        else if(wtype == 9) {          
          if(gm.tow - svstat.galmsgTyped[8].tow <= 30 &&
             svstat.galmsgTyped[8].alma2.svid && gm.iodalmanac == svstat.galmsgTyped[8].iodalmanac) {
            //            cout<<(int)wtype<<" alma-sv "<<gm.alma2.svid<< " "<<gm.alma2.deltaSqrtA<<" " <<gm.alma2.t0almanac << endl;
            g_galileoalma[gm.alma2.svid] = gm.alma2;
          }
        }

        else if(wtype == 10) { // GSTT GPS
          if(gm.tow - svstat.galmsgTyped[9].tow < 5 &&
             svstat.galmsgTyped[9].alma3.svid && gm.iodalmanac == svstat.galmsgTyped[9].iodalmanac) {
            //            cout<<(int)wtype<<" alma-sv "<<gm.alma3.svid<< " "<<gm.alma3.deltaSqrtA<<" " <<gm.alma3.t0almanac << endl;
            //            cout<<(int)wtype<<"b alma-sv "<<svstat.galmsgTyped[9].alma3.svid<< " "<<svstat.galmsgTyped[9].alma3.deltaSqrtA<<" " <<svstat.galmsgTyped[9].alma3.t0almanac << endl;
            g_galileoalma[gm.alma3.svid] = gm.alma3;
          }
          g_GSTGPSOffset = gm.getGPSOffset(gm.tow, gm.wn).first;
          idb.addValue(id, "gpsoffset", {{"a0g", g_svstats[id].galmsg.a0g},
                {"a1g", g_svstats[id].galmsg.a1g},
                  {"t0g", g_svstats[id].galmsg.t0g},
                    {"delta", g_GSTGPSOffset}
            }, satUTCTime(id));
          
        }

        // CONVERSION XXX
#if 0        
        for(auto& ent : g_svstats) {
          //        fmt::printf("%2d\t", ent.first);
          id=ent.first;
          if(ent.second.completeIOD() && ent.second.prevIOD.first >= 0) {

            ent.second.clearPrev();          
          }
        }
#endif
    }
    else if(nmm.type() == NavMonMessage::ObserverPositionType) {
      g_srcpos[nmm.sourceid()].lastSeen = nmm.localutcseconds();
      g_srcpos[nmm.sourceid()].pos.x = nmm.op().x();
      g_srcpos[nmm.sourceid()].pos.y = nmm.op().y();
      g_srcpos[nmm.sourceid()].pos.z = nmm.op().z();
      if(nmm.op().has_groundspeed()) {
        g_srcpos[nmm.sourceid()].groundSpeed = nmm.op().groundspeed();
      }
      
      g_srcpos[nmm.sourceid()].accuracy = nmm.op().acc();
      //      idb.addValueObserver(nmm.sourceid(), "accfix", nmm.op().acc(), nmm.localutcseconds());

      auto latlonh = ecefToWGS84(nmm.op().x(), nmm.op().y(), nmm.op().z());
      idb.addValueObserver(nmm.sourceid(), "fix", {
          {"x", nmm.op().x()},
            {"y", nmm.op().y()},
              {"z", nmm.op().z()},
                {"lat", 180.0*std::get<0>(latlonh)/M_PI},
                    {"lon", 180.0*std::get<1>(latlonh)/M_PI},
                      {"h", std::get<2>(latlonh)},
                        {"acc", nmm.op().acc()},
                          {"groundspeed", nmm.op().has_groundspeed() ? nmm.op().groundspeed() : -1.0}
        }, 
        nmm.localutcseconds());
    }
    else if(nmm.type() == NavMonMessage::RFDataType) {
      SatID id{nmm.rfd().gnssid(), nmm.rfd().gnsssv(), nmm.rfd().sigid()};

      
      if(id.gnss==2 && id.sigid == 0) // old ubxtool output gets this wrong
        id.sigid = 1;

      // some sources ONLY have RFDatatype & not ReceptionDataType
      g_svstats[id].perrecv[nmm.sourceid()].db = nmm.rfd().cno();

      
      idb.addValueObserver(nmm.sourceid(),  "rfdata",
                   {{"carrierphase", nmm.rfd().carrierphase()},
                       {"doppler", nmm.rfd().doppler()},
                         {"locktime", nmm.rfd().locktimems()},
                           {"pseudorange", nmm.rfd().pseudorange()},
                             {"prstd", nmm.rfd().prstd()},
                               {"cpstd", nmm.rfd().cpstd()},
                                 {"dostd", nmm.rfd().dostd()}

                   }, nanoTime(0, nmm.rfd().rcvwn(), nmm.rfd().rcvtow())/1000000000.0, id);
      
      if(id.gnss == 3 && g_svstats[id].ephBeidoumsg.sow >= 0 && g_svstats[id].ephBeidoumsg.sqrtA != 0) {
        double freq = 1561.098;
        if(nmm.rfd().sigid() != 0)
          freq = 1207.140;

        // the magic 14 is because 'rcvtow()' is in GPS/Galileo TOW
        // but BeiDou operates with 14 leap seconds less than GPS/Galileo
        auto res = doDoppler(nmm.rfd().rcvtow()-14, g_srcpos[nmm.sourceid()].pos, g_svstats[id].ephBeidoumsg, freq * 1000000);

        if(isnan(res.preddop)) {
          cerr<<"Problem with doppler calculation for C"<<id.sv<<": "<<endl;
          exit(1);
        }
        
        time_t t = utcFromGPS(nmm.rfd().rcvwn(), nmm.rfd().rcvtow());
        if(t - g_svstats[id].perrecv[nmm.sourceid()].deltaHzTime > 10) {
          
          g_svstats[id].perrecv[nmm.sourceid()].deltaHz = nmm.rfd().doppler() -  res.preddop;
          g_svstats[id].perrecv[nmm.sourceid()].deltaHzTime = t;
          
          //          idb.addValue(id, "delta_hz", nmm.rfd().doppler() -  res.preddop);
          auto corr = getHzCorrection(t, nmm.sourceid(), id.gnss, id.sigid, g_svstats);
          if(corr) {
            //            idb.addValue(id, "delta_hz_cor", nmm.rfd().doppler() -  res.preddop - (*corr));
            Point sat;
            getCoordinates(nmm.rfd().rcvtow(), g_svstats[id].ephBeidoumsg, &sat);

            idb.addValue(id, "correlator",
                   {{"delta_hz_cor", nmm.rfd().doppler() -  res.preddop - (*corr)},
                       {"delta_hz", nmm.rfd().doppler() -  res.preddop},
                         {"elevation", getElevationDeg(sat, g_srcpos[nmm.sourceid()].pos)},
                           {"hz", nmm.rfd().doppler()},
                           {"prres", g_svstats[id].perrecv[nmm.sourceid()].prres},
                             {"qi", g_svstats[id].perrecv[nmm.sourceid()].qi},
                               {"used", g_svstats[id].perrecv[nmm.sourceid()].used},
                               {"db", g_svstats[id].perrecv[nmm.sourceid()].db}
                   }, nanoTime(0, nmm.rfd().rcvwn(), nmm.rfd().rcvtow())/1000000000.0, nmm.sourceid()); //this time is supplied in GPS timeframe

            
          }
        }
      }
      else if(g_svstats[id].completeIOD() &&
              (id.gnss != 6 || !(random() % 16))) { // GLONASS is too slow
        double freqMHZ =  1575.42;
        if(id.gnss == 2 && id.sigid == 5) // this is exactly the beidou b2i freq?
          freqMHZ = 1207.140;
        
        auto res = g_svstats[id].doDoppler(nmm.rfd().rcvtow(), g_srcpos[nmm.sourceid()].pos,freqMHZ * 1000000);
        
        Point sat;
        g_svstats[id].getCoordinates(nmm.rfd().rcvtow(),  &sat);
        
        time_t t = utcFromGPS(nmm.rfd().rcvwn(), nmm.rfd().rcvtow());
//        idb.addValueObserver((int)nmm.sourceid(), "orbit",
  //                     {{"preddop", res.preddop},
    //                       {"radvel", res.radvel}}, t, id);



        if(t - g_svstats[id].perrecv[nmm.sourceid()].deltaHzTime > 10) { // only replace after 5 seconds
          g_svstats[id].perrecv[nmm.sourceid()].deltaHz =  nmm.rfd().doppler() -  res.preddop;
          g_svstats[id].perrecv[nmm.sourceid()].deltaHzTime =  t;
          //          idb.addValue(id, "delta_hz", nmm.rfd().doppler() -  res.preddop);
          auto corr = getHzCorrection(t, nmm.sourceid(), id.gnss, id.sigid, g_svstats);
          if(corr) {
            //            idb.addValue(id, "delta_hz_cor", nmm.rfd().doppler() -  res.preddop - *corr, nmm.sourceid());
            idb.addValue(id, "correlator",
                         {{"delta_hz_cor", nmm.rfd().doppler() -  res.preddop - (*corr)},
                             {"delta_hz", nmm.rfd().doppler() -  res.preddop},
                               {"hz", nmm.rfd().doppler()},
                                 {"elevation", getElevationDeg(sat, g_srcpos[nmm.sourceid()].pos)},
                                   {"prres", g_svstats[id].perrecv[nmm.sourceid()].prres},
                                     {"qi", g_svstats[id].perrecv[nmm.sourceid()].qi},
                                       {"used", g_svstats[id].perrecv[nmm.sourceid()].used},
                                         {"db", g_svstats[id].perrecv[nmm.sourceid()].db}
                               
                         }, t, nmm.sourceid());


          }
        }
      }
    }
    else if(nmm.type()== NavMonMessage::ObserverDetailsType) {
      auto& o = g_srcpos[nmm.sourceid()];
      o.serialno = nmm.od().serialno();

      o.swversion = nmm.od().swversion();
      o.hwversion = nmm.od().hwversion();
      o.mods = nmm.od().modules();
      if(nmm.od().has_clockoffsetns())
        o.clockOffsetNS = nmm.od().clockoffsetns();
      else
        o.clockOffsetNS = -1;
      
      if(nmm.od().has_clockoffsetdriftns())
        o.clockOffsetDriftNS = nmm.od().clockoffsetdriftns();
      else
        o.clockOffsetDriftNS = -1;
      
      if(nmm.od().has_clockaccuracyns())
        o.clockAccuracyNS = nmm.od().clockaccuracyns();
      else
        o.clockAccuracyNS = -1;
      
      if(nmm.od().has_freqaccuracyps())
        o.freqAccuracyPS = nmm.od().freqaccuracyps();
      else
        o.freqAccuracyPS = -1;

      if(nmm.od().has_uptime())
        o.uptime = nmm.od().uptime();
      else
        o.uptime = -1;

      if(nmm.od().has_recvgithash())
        o.githash = nmm.od().recvgithash();
      else
        o.githash.clear();
      
      
      o.vendor = nmm.od().vendor();
      o.owner = nmm.od().owner();
      o.remark = nmm.od().remark();


      idb.addValueObserver(nmm.sourceid(), "observer_details", {
          {"clock_offset_ns", o.clockOffsetNS},
            {"clock_drift_ns", o.clockOffsetDriftNS},
              {"clock_acc_ns", o.clockAccuracyNS},
                {"freq_acc_ps", o.freqAccuracyPS},
                  {"uptime", o.uptime}
        }, 
        nmm.localutcseconds());

      
    }
    else if(nmm.type() == NavMonMessage::UbloxJammingStatsType) {
      /*
      cout<<"noisePerMS "<<nmm.ujs().noiseperms() << " agcCnt "<<
        nmm.ujs().agccnt()<<" flags "<<nmm.ujs().flags()<<" jamind "<<
        nmm.ujs().jamind()<<endl;
      */
      idb.addValueObserver(nmm.sourceid(), "ubx_jamming", {
          {"noise_per_ms", nmm.ujs().noiseperms()},
            {"agccnt", nmm.ujs().agccnt()},
              {"jamind", nmm.ujs().jamind()},
                {"flags", nmm.ujs().flags()}
        }, 
        nmm.localutcseconds());

      
    }

    else if(nmm.type()== NavMonMessage::DebuggingType) {
      auto ret =  parseTrkMeas(basic_string<uint8_t>((const uint8_t*)nmm.dm().payload().c_str(), nmm.dm().payload().size()));
      for(const auto& tss : ret) {
        SatID id{static_cast<uint32_t>(tss.gnss), static_cast<uint32_t>(tss.sv), tss.gnss == 2 ? 1u : 0u};
        if(g_svstats[id].completeIOD()) {
          double freqMHZ =  1575.42;
          double tsat = ldexp(1.0* tss.tr, -32) /1000.0;
          auto res = g_svstats[id].doDoppler(tsat, g_srcpos[nmm.sourceid()].pos, freqMHZ * 1000000);

          
          //          idb.addValueObserver((int)nmm.sourceid(), "orbit",
          //            {{"preddop", res.preddop},
          //               {"radvel", res.radvel}}, (time_t)nmm.localutcseconds(), id);

          //          cout << " preddop "<<res.preddop <<" meas "<<tss.dopplerHz <<" delta " << (tss.dopplerHz - res.preddop);
          double t = utcFromGPS(latestWN(0, g_svstats), tsat); 
          if(t - g_svstats[id].perrecv[nmm.sourceid()].deltaHzTime > 5) { // only replace after 5 seconds
            g_svstats[id].perrecv[nmm.sourceid()].deltaHz =  tss.dopplerHz -  res.preddop;
            g_svstats[id].perrecv[nmm.sourceid()].deltaHzTime =  t;
            //            idb.addValue(id, "delta_hz", tss.dopplerHz -  res.preddop);

            auto corr = getHzCorrection(t, nmm.sourceid(), id.gnss, id.sigid, g_svstats);
            if(corr) {
              Point sat;
              g_svstats[id].getCoordinates(tsat, &sat);

              //              idb.addValue(id, "delta_hz_cor", tss.dopplerHz -  res.preddop - *corr, nmm.sourceid());

              idb.addValue(id, "correlator",
                           {{"delta_hz_cor", tss.dopplerHz -  res.preddop - *corr},
                               {"delta_hz", tss.dopplerHz -  res.preddop},
                                 {"hz", tss.dopplerHz},
                                   {"elevation", getElevationDeg(sat, g_srcpos[nmm.sourceid()].pos)},
                                     {"prres", g_svstats[id].perrecv[nmm.sourceid()].prres},
                                       {"qi", g_svstats[id].perrecv[nmm.sourceid()].qi},
                                         {"used", g_svstats[id].perrecv[nmm.sourceid()].used},
                                           {"db", g_svstats[id].perrecv[nmm.sourceid()].db}

                           }, t, nmm.sourceid());

              
            }
          }
        }
        //        cout<<endl;
      }
    }
    else if(nmm.type()== NavMonMessage::GPSInavType) {
      if(nmm.gpsi().sigid()) {
        cout<<"ignoring sigid "<<nmm.gpsi().sigid()<<" for legacy GPS "<<nmm.gpsi().gnsssv()<<endl;
        continue;
      }
      auto cond = getCondensedGPSMessage(std::basic_string<uint8_t>((uint8_t*)nmm.gpsi().contents().c_str(), nmm.gpsi().contents().size()));
      SatID id{nmm.gpsi().gnssid(), nmm.gpsi().gnsssv(), nmm.gpsi().sigid()};

      g_svstats[id].perrecv[nmm.sourceid()].t = nmm.localutcseconds();
      
      auto& svstat = g_svstats[id];
      svstat.gnss = id.gnss;
      auto oldsvstat = svstat;
      uint8_t page;
      auto& gm = svstat.gpsmsg;
      auto oldgm = gm;
      int frame = gm.parseGPSMessage(cond, &page);

      if(frame == 1) {
        
        idb.addValue(id, "clock", {{"offset_ns", getGPSAtomicOffset(svstat.tow(), svstat.gpsmsg).first}, 
              {"t0c", 16.0*gm.t0c},
                {"af0", 8.0*gm.af0},
                  {"af1", 8.0*gm.af1},
                    {"af2", 16.0*gm.af2}}, satUTCTime(id)); 

        //        cout<<"Got ura "<<gm.ura<<" for sv "<<id.first<<","<<id.second<<endl;
        idb.addValue(id, "gpsura", {{"value", gm.ura}}, satUTCTime(id));

        if(oldgm.af0 && oldgm.ura != gm.ura)  { // XX find better way to check
          cout<<humanTime(id.gnss, gm.wn, gm.tow)<<" src "<<nmm.sourceid()<<" wn "<<gm.wn <<" GPS "<<id.sv <<"@"<<id.sigid<<" change in URA ["<< humanUra(oldgm.ura) <<"] -> [" << humanUra(gm.ura)<<"] "<<(int)gm.ura<<", lastseen "<<ephAge(oldgm.tow, gm.tow)/3600.0 <<" hours"<<endl;
        }

        if(oldgm.af0 && oldgm.gpshealth != gm.gpshealth)  { // XX find better way to check
          cout<<humanTime(id.gnss, gm.wn, gm.tow)<<" src " <<nmm.sourceid()<<" wn "<<gm.wn <<" GPS "<<id.sv <<"@"<<id.sigid<<" change in health ["<< (int)oldgm.gpshealth <<"] -> [" << (int)gm.gpshealth <<"], lastseen "<<ephAge(oldgm.tow, gm.tow)/3600.0 <<" hours"<<endl;
        }


        //        double age = ephAge(g_svstats[id].tow, g_svstats[id].t0c * 16);

        if(oldgm.af0 && oldgm.t0c != gm.t0c) {
          auto oldOffset = getGPSAtomicOffset(gm.tow, oldgm);
          auto newOffset = getGPSAtomicOffset(gm.tow, gm);
          svstat.timeDisco = oldOffset.first - newOffset.first;
          if(fabs(svstat.timeDisco) < 10000)
            idb.addValue(id, "clock_jump_ns", {{"value", svstat.timeDisco}}, satUTCTime(id));
        }
      }
      else if(frame==2) {
        if(oldgm.gpshealth != gm.gpshealth) {
          cout<<humanTime(id.gnss, gm.wn, gm.tow)<<" src "<<nmm.sourceid()<<" GPS "<<id.sv <<"@"<<id.sigid<<" change in health: ["<< (int)oldgm.gpshealth<<"] -> ["<< (int)gm.gpshealth<<"] , lastseen "<<ephAge(oldgm.tow, gm.tow)/3600.0 <<" hours"<<endl;
        }
        svstat.gpsmsg2 = gm;
        idb.addValue(id, "gpshealth", {{"value", g_svstats[id].gpsmsg.gpshealth}}, satUTCTime(id));

        if(svstat.gpsmsg2.gpsiod == svstat.gpsmsg3.gpsiod && gm.gpsiod != svstat.ephgpsmsg.gpsiod) {
          svstat.oldephgpsmsg = svstat.ephgpsmsg;
          svstat.ephgpsmsg = gm;
          svstat.reportNewEphemeris(id, idb);
        }

      }
      else if(frame == 3) {
        svstat.gpsmsg3 = gm;
        if(svstat.gpsmsg2.gpsiod == svstat.gpsmsg3.gpsiod && gm.gpsiod != svstat.ephgpsmsg.gpsiod) {
          svstat.oldephgpsmsg = svstat.ephgpsmsg;
          svstat.ephgpsmsg = gm;
          svstat.reportNewEphemeris(id, idb);
        }
      }
      else if(frame==4 && page==18) {
        const auto& sv = g_svstats[id];

        g_GPSUTCOffset = getGPSUTCOffset(sv.tow(), sv.wn(), gm).first;
        idb.addValue(id, "utcoffset", {
            {"a0", gm.a0},
              {"a1", gm.a1},
                {"delta", g_GPSUTCOffset}
          },
          satUTCTime(id));
      }
      else if((frame == 5 && page  <= 24) || (frame==4 && page >=25 && page<=32)) {
        g_gpsalma[gm.gpsalma.sv] = gm.gpsalma;
      }
      g_svstats[id].perrecv[nmm.sourceid()].t = nmm.localutcseconds();
      // XXX conversion possibly vital
      //      g_svstats[id].tow = nmm.gpsi().gnsstow();
      //      g_svstats[id].wn = nmm.gpsi().gnsswn();
      //      if(g_svstats[id].wn < 512) // XXX ROLLOVER
      //        g_svstats[id].wn += 2048;
    }
    else if(nmm.type() == NavMonMessage::RTCMMessageType) {
      RTCMMessage rm;
      rm.parse(nmm.rm().contents());
      if(rm.type == 1057 || rm.type == 1240) {
        for(const auto& ed : rm.d_ephs) {
          auto iter = g_svstats.find(ed.id);
          if(iter != g_svstats.end() && iter->second.completeIOD()  && iter->second.liveIOD().getIOD() == ed.iod)
            iter->second.rtcmEphDelta = ed;
          
          idb.addValue(ed.id, "rtcm-eph-correction", {
                       {"iod", ed.iod},
                       {"radial", ed.radial},
                       {"along", ed.along},
                       {"cross", ed.cross},
                       {"dradial", ed.dradial},
                       {"dalong", ed.dalong},
                       {"dcross", ed.dcross},
                         {"ssr-iod", rm.ssrIOD},
                       {"ssr-provider", rm.ssrProvider},
                       {"ssr-solution", rm.ssrSolution},
                       {"tow", rm.sow},
                         {"udi", rm.udi}},
            nmm.localutcseconds(),
            nmm.sourceid());

        }
      }
      else if(rm.type == 1058 || rm.type == 1241) {
        for(const auto& cd : rm.d_clocks) {
          idb.addValue(cd.id, "rtcm-clock-correction", {
                       {"dclock0", cd.dclock0},
                         {"dclock1", cd.dclock1},
                           {"dclock2", cd.dclock2},
                         {"ssr-iod", rm.ssrIOD},
                       {"ssr-provider", rm.ssrProvider},
                       {"ssr-solution", rm.ssrSolution},
                       {"tow", rm.sow},
                         {"udi", rm.udi}},
            nmm.localutcseconds(),
            nmm.sourceid());
          
        }
      }

    }
    else if(nmm.type()== NavMonMessage::GPSCnavType) {
      SatID id{nmm.gpsc().gnssid(), nmm.gpsc().gnsssv(), nmm.gpsc().sigid()};
      g_svstats[id].perrecv[nmm.sourceid()].t = nmm.localutcseconds();
      
      auto& svstat = g_svstats[id];
      svstat.gnss = id.gnss;

      GPSCNavState gcns;
      parseGPSCNavMessage(
                           std::basic_string<uint8_t>((uint8_t*)nmm.gpsc().contents().c_str(),
                                                      nmm.gpsc().contents().size()),
                           gcns);
      //      cout<<"Got a message from "<<makeSatIDName(id)<<endl;

      // XXX conversion possibly vital
      //      svstat.tow = nmm.gpsc().gnsstow();
      //      svstat.wn = nmm.gpsc().gnsswn();                         
      
    }
    else if(nmm.type()== NavMonMessage::BeidouInavTypeD1) {
    try {
      SatID id{nmm.bid1().gnssid(), nmm.bid1().gnsssv(), nmm.bid1().sigid()};

      g_svstats[id].perrecv[nmm.sourceid()].t = nmm.localutcseconds();
      
      auto& svstat = g_svstats[id];
      svstat.gnss = id.gnss;
      uint8_t pageno;
      auto cond = getCondensedBeidouMessage(std::basic_string<uint8_t>((uint8_t*)nmm.bid1().contents().c_str(), nmm.bid1().contents().size()));
      auto& bm = svstat.beidoumsg;
      auto oldbm = bm;
      int fraid=bm.parse(cond, &pageno);
      // XXX conversion possibly vital
      //      svstat.tow = nmm.bid1().gnsstow();
      //      svstat.wn = nmm.bid1().gnsswn();
      if(fraid == 1) {

        if(oldbm.sath1 != bm.sath1) {
          cout<<humanTime(id.gnss, svstat.wn(), svstat.tow())<<" wn "<<bm.wn<<" sow " <<bm.sow<<" BeiDou C"<<id.sv<<"@"<<id.sigid<<" health changed from  "<<(int)oldbm.sath1 <<" to "<< (int)bm.sath1 <<", lastseen "<<ephAge(oldbm.sow, bm.sow)/3600.0<<" hours"<<endl;
        }
        
        idb.addValue(id, "clock", {{"offset_ns", 1000000.0*bm.getAtomicOffset().first}, 
              {"t0c", bm.getT0c()},
                {"af0", bm.a0 * 2},
                  {"af1", bm.a1 / 16},
                    {"af2", bm.a2 / 128}}, satUTCTime(id)); // scaled to galileo units
        
        idb.addValue(id, "beidouhealth", {{"sath1", bm.sath1}}, satUTCTime(id));
        idb.addValue(id, "beidouurai", {{"value", bm.urai}}, satUTCTime(id));
        if(svstat.lastBeidouMessage1.wn >=0 && svstat.lastBeidouMessage1.t0c != bm.t0c) {
          auto oldOffset = svstat.lastBeidouMessage1.getAtomicOffset(bm.sow);
          auto newOffset = bm.getAtomicOffset(bm.sow);
          svstat.timeDisco = oldOffset.first - newOffset.first;
          if(fabs(svstat.timeDisco) < 10000)
            idb.addValue(id, "clock_jump_ns", {{"value", svstat.timeDisco}}, satUTCTime(id));
        }
        svstat.lastBeidouMessage1 = bm;        
      }
      if(fraid == 2) {
        svstat.lastBeidouMessage2 = bm;
      }
      if(fraid == 3) {
        Point oldpoint, newpoint;
        if(bm.sow - svstat.lastBeidouMessage2.sow == 6) {
          if(svstat.ephBeidoumsg.getT0e() != svstat.beidoumsg.getT0e() && bm.sqrtA) {
            svstat.oldephBeidoumsg = svstat.ephBeidoumsg;
            svstat.ephBeidoumsg = bm;
            svstat.reportNewEphemeris(id, idb);
          }          
        }
      }
      else if((fraid == 4 && 1<= pageno && pageno <= 24) ||
              (fraid == 5 && 1<= pageno && pageno <= 6) ||
              (fraid == 5 && 11<= pageno && pageno <= 23) ) {

        struct BeidouAlmanacEntry bae;
        //        bm.alma.AmEpID = svstat.ephBeidoumsg.alma.AmEpID; // this comes from older messages
        
        if(processBeidouAlmanac(bm, bae)) {
          g_beidoualma[bae.sv]=bae;
        }
      }

      if(fraid==5 && pageno == 9) {
        /*
        svstat.a0g = bm.a0gps;
        svstat.a1g = bm.a1gps;        
        */
      }

      if(fraid==5 && pageno == 10) {
        /*
        svstat.a0 = bm.a0utc;
        svstat.a1 = bm.a1utc;
        */
        g_dtLSBeidou = bm.deltaTLS;
        g_BeiDouUTCOffset = bm.getUTCOffset(bm.sow).first;
        //        cout<<"Beidou leap seconds: "<<g_dtLSBeidou<<endl;
      }
      }catch(std::exception& e) {
      cerr<<"Beidou: "<<e.what()<<endl;
      }
    }
    else if(nmm.type()== NavMonMessage::BeidouInavTypeD2) {
      auto cond = getCondensedBeidouMessage(std::basic_string<uint8_t>((uint8_t*)nmm.bid2().contents().c_str(), nmm.bid2().contents().size()));
      /*
      int fraid = getbitu(&cond[0], beidouBitconv(16), 3);
      int sow = getbitu(&cond[0], beidouBitconv(19), 20);
      int pnum = getbitu(&cond[0], beidouBitconv(43), 4);
      int pre = getbitu(&cond[0], beidouBitconv(1), 11);

      //      cout<<"C"<< nmm.bid2().gnsssv() << " sent D2 message, pre "<<pre<<" sow "<<sow<<", FraID "<<fraid;
      //      if(fraid == 1)
      //        cout <<" pnum "<<pnum;
      //      cout<<endl;
      */
    }
    else if(nmm.type()== NavMonMessage::GlonassInavType) {
      SatID id{nmm.gloi().gnssid(), nmm.gloi().gnsssv(), nmm.gloi().sigid()};
      auto& svstat = g_svstats[id];
      svstat.gnss = id.gnss;
      auto& gm = svstat.glonassMessage;
      auto oldgm = gm;
      int strno = gm.parse(std::basic_string<uint8_t>((uint8_t*)nmm.gloi().contents().c_str(), nmm.gloi().contents().size()));
      g_svstats[id].perrecv[nmm.sourceid()].t = nmm.localutcseconds();
      if(strno == 1 && gm.n4 != 0 && gm.NT !=0) {
        //        uint32_t glotime = gm.getGloTime(); // this starts GLONASS time at 31st of december 1995, 00:00 UTC
        // CONVERSION, possibly vital
        //        svstat.wn = glotime / (7*86400);
        //        svstat.tow = glotime % (7*86400);
        //        cout<<"Glonass now: "<<humanTime(glotime + 820368000) << " n4 "<<(int)gm.n4<<" NT "<<(int)gm.NT<< " wn "<<svstat.wn <<" tow " <<svstat.tow<<endl;
      }
      else if(strno == 2) {
        if(svstat.wn() > 0)
          idb.addValue(id, "glohealth", {{"Bn", gm.Bn}}, satUTCTime(id));
        if(oldgm.Bn != gm.Bn) {
          cout<<humanTime(id.gnss, svstat.wn(), svstat.tow())<<" n4 "<< (int)gm.n4<<" NT " << (int)gm.NT<<" GLONASS R"<<id.sv<<"@"<<id.sigid<<" health changed from  "<<(int)oldgm.Bn <<" to "<< (int)gm.Bn <<endl;

        }
      }
      else if(strno == 4) {
        //        svstat.aode = gm.En * 24;
        if(svstat.wn() > 0) {
          idb.addValue(id, "glo_taun_ns", {{"value", gm.getTaunNS()}}, satUTCTime(id));
          idb.addValue(id, "ft", {{"value", gm.FT}}, satUTCTime(id));

          if(oldgm.taun && oldgm.taun != gm.taun) {
            if(gm.getGloTime() - oldgm.getGloTime()  < 300) {
              svstat.timeDisco = gm.getTaunNS() - oldgm.getTaunNS();
              idb.addValue(id, "clock_jump_ns", {{"value", svstat.timeDisco}}, satUTCTime(id));
            }
          }
        }
        if(gm.x && gm.y && gm.z) {
          if(svstat.ephglomsg.x != gm.x &&
             svstat.ephglomsg.y != gm.y &&
             svstat.ephglomsg.z != gm.z) {
            svstat.oldephglomsg = svstat.ephglomsg;
            svstat.ephglomsg = gm;
            svstat.reportNewEphemeris(id, idb);
          }
        }
      }
      else if(strno == 5) {
        g_GlonassUTCOffset = gm.getUTCOffset(0).first;
        g_GlonassGPSOffset = gm.getGPSOffset(0).first;
        idb.addValue(id, "utcoffset", {
            {"tauc", gm.tauc},
                {"delta", g_GlonassUTCOffset}
          },
          satUTCTime(id));

      }
      else if(strno == 6 || strno == 8 || strno == 10 || strno == 12 || strno == 14) {
        svstat.glonassAlmaEven = {nmm.localutcseconds(), gm};
      }
      else if(strno == 7 || strno == 9 || strno == 11 || strno == 13 || strno == 15) {
        if(nmm.localutcseconds() - svstat.glonassAlmaEven.first < 4 && svstat.glonassAlmaEven.second.strtype == gm.strtype -1) {
          g_glonassalma[svstat.glonassAlmaEven.second.nA] = make_pair(svstat.glonassAlmaEven.second, gm);
        }
      }
      //      cout<<"GLONASS R"<<id.second<<" str "<<strno<<endl;
    }
    else if(nmm.type() == NavMonMessage::SBASMessageType) {
      auto& sb = g_sbas[nmm.sbm().gnsssv()];

      sb.perrecv[nmm.sourceid()].last_seen = nmm.localutcseconds();

      basic_string<uint8_t> sbas((uint8_t*)nmm.sbm().contents().c_str(), nmm.sbm().contents().length());
      auto delta = sb.status.parse(sbas, nmm.localutcseconds());
      // fast correction
      for(const auto& f : delta.first) {
        idb.addValue(f.id, "sbas_fast",
                     {{"correction", f.correction},
                         {"udrei", f.udrei}}, nmm.localutcseconds(),
                     nmm.sbm().gnsssv(), "sbas");
                     
                
      }
      for(const auto& lt : delta.second) {
        auto iter = g_svstats.find(lt.id);
        if(iter == g_svstats.end())
          continue;
        const auto& s = *iter;
        bool haveEphemeris=false;
        double spaceShift=0, ephShift = 0, rangeShift =0;
        if(s.second.completeIOD() && (s.second.liveIOD().getIOD() & 0xff) == lt.iod8) {
            Point sat;
            
            s.second.getCoordinates(s.second.tow(), &sat);
            Point adjsat=sat;
            adjsat.x += lt.dx;
            adjsat.y += lt.dy;
            adjsat.z += lt.dz;
            Point sbasCenter;
            int prn = nmm.sbm().gnsssv();
            if(prn== 126 || prn == 136 || prn == 123)
              sbasCenter = c_egnosCenter;
            else if(prn == 138 || prn == 131 || prn == 133)
              sbasCenter = c_waasCenter;
            else
              sbasCenter = Point{0,0,0};
            
            double dist = Vector(sbasCenter, adjsat).length() - Vector(sbasCenter, sat).length();
            spaceShift = dist;
            dist -= lt.dai / 3;
            ephShift = dist;
            rangeShift = dist - g_svstats[lt.id].sbas[nmm.sbm().gnsssv()].fast.correction;
            haveEphemeris=true;
          }
        idb.addValue(lt.id,"sbas_lterm",
                     {
                       {"iod8", lt.iod8},
                         {"toa", lt.toa},
                         {"iodp", lt.iodp},
                           {"dx", lt.dx},
                             {"dy", lt.dy},
                               {"dz", lt.dz},
                                 {"dai", lt.dai},
                                   {"ddx", lt.ddx},
                                     {"ddy", lt.ddy},
                                       {"ddz", lt.ddz},
                                         {"ddai", lt.ddai},
                                           {"ephemeris", 1.0*haveEphemeris},
                                             {"space_shift", spaceShift},
                                               {"eph_shift", ephShift},
                                                 {"range_shift", rangeShift}
                     }, nmm.localutcseconds(), nmm.sbm().gnsssv(), "sbas");

      }

      if(nmm.localutcseconds() - sb.status.d_lastDNU > 120) {
        for(const auto& c : sb.status.d_fast) {
          g_svstats[c.first].sbas[nmm.sbm().gnsssv()].fast = c.second; 
        }
        for(const auto& c : sb.status.d_longterm) {
          g_svstats[c.first].sbas[nmm.sbm().gnsssv()].longterm = c.second; 
        }
      }
    }
    else {
      cout<<"Unknown type "<< (int)nmm.type()<<endl;
    }
    
  }
  }
  catch(EofException& e)
    {}
}
catch(std::exception& e)
{
  cerr<<"Exiting because of fatal error "<<e.what()<<endl;
}
