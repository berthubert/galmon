#include "ext/powerblog/h2o-pp.hh"
#include "minicurl.hh"
#include <iostream>
#include "navmon.hh"
#include "fmt/format.h"
#include "fmt/printf.h"
#include "galileo.hh"
#include "gps.hh"
#include "CLI/CLI.hpp"
#include "version.hh"
#include "ephemeris.hh"
#include "influxpush.hh"
#include "sp3.hh"

static char program[]="reporter";

using namespace std;

extern const char* g_gitHash;

class Stats
{
public:
  void add(double v)
  {
    results.dirty=true;
    results.d.push_back(v);
  }
  struct Results
  {
    vector<double> d;
    mutable bool dirty{false};
    double median() const
    {
      return quantile(0.5);
    }
    double quantile(double p) const
    {
      unsigned int pos = p * d.size();
      if(!d.size())
        throw runtime_error("Empty range for quantile");
      if(pos == d.size())
        --pos;
      return d[p*d.size()];
    }

  };
  const Results& done() const
  {
    if(results.dirty) {
      sort(results.d.begin(), results.d.end());
      results.dirty=false;
    }
    return results;
  }
  bool empty() const
  {
    return results.d.empty();
  }
private:
  mutable Results results;
};

/*
  Goal: generate statistics from influxdb.

  Method: per x minute interval, determine status for all SVs.
  We only count minutes in which we have data
  
  We do only positive measurements, and report absence of data in neutral terms - either there was no reception or no transmission, we don't know.

  If an interval has no data: unobserved
  If interval has any data, it counts as observed
  If interval has a single unhealthy status, it is entirely unhealthy
  If interval is NAPA, but otherwise healthy, status is NAPA
  If observed and nothing else, status is healthy

  Input: time range, width if interval
  Internal: per SV, interval, bitfield
  Output: per SV, number of intervals healthy, number of intervals NAPA, number of intervals unhealthy, number of intervals unobserved
*/


struct IntervalStat
{
  std::optional<int> unhealthy;
  std::optional<int> dataunhealthy;
  std::optional<int> osnma;
  std::optional<int> sisa;
  bool ripe{false};
  bool expired{false};
  double rtcmDist{-1};
  std::optional<double> rtcmDClock;
};


map<SatID, map<time_t,IntervalStat>> g_stats;

int main(int argc, char **argv)
try
{
  MiniCurl mc;
  MiniCurl::MiniCurlHeaders mch;
  string influxDBName("galileo");


  string period="time > now() - 1w";
  int sigid=1;
  bool doVERSION{false};

  CLI::App app(program);
  string periodarg("1d");
  string beginarg, endarg;
  string sp3src("default");
  int gnssid=2;
  int rtcmsrc=300;
  int galwn=-1;
  string influxserver="http://127.0.0.1:8086";
  app.add_flag("--version", doVERSION, "show program version and copyright");
  app.add_option("--period,-p", periodarg, "period over which to report (1h, 1w)");
  app.add_option("--begin,-b", beginarg, "Beginning");
  app.add_option("--end,-e", endarg, "End");
  app.add_option("--gal-wn", galwn, "Galileo week number to report on");
  app.add_option("--sp3src", sp3src, "Identifier of SP3 source");
  app.add_option("--rtcmsrc", rtcmsrc, "Identifier of RTCM source");
  app.add_option("--sigid,-s", sigid, "Signal identifier. 1 or 5 for Galileo.");
  app.add_option("--gnssid,-g", gnssid, "gnssid, 0 GPS, 2 Galileo");
  app.add_option("--influxdb", influxDBName, "Name of influxdb database");
  app.add_option("--influxserver", influxserver, "Address of influx server");
  try {
    app.parse(argc, argv);
  } catch(const CLI::Error &e) {
    return app.exit(e);
  }

  if(doVERSION) {
    showVersion(program, g_gitHash);
    exit(0);
  }

  if(galwn>= 0) {
    time_t w = utcFromGST(galwn, 0);
    period = "time >= '"+influxTime(w)+"' and time < '"+influxTime(w+7*86400) +"'";
  }
  else if(beginarg.empty() && endarg.empty()) 
    period = "time > now() - "+periodarg;
  else {
    period = "time > '"+ beginarg +"' and time <= '" + endarg +"'";
    cout<<"Period: "<<period<<endl;
  }
  cerr<<"InfluxDBName: "<<influxDBName<<endl;


  // auto res = mc.getURL(url + mc.urlEncode("select distinct(value) from sisa where "+period+" and sigid='"+to_string(sigid)+"' group by gnssid,sv,sigid,time(10m)"));

  if(influxserver.find("http"))
    influxserver="http://"+influxserver;
  if(influxserver.empty() || influxserver[influxserver.size()-1]!='/')
    influxserver+="/";
  string url=influxserver+"query?db="+influxDBName+"&epoch=s&q=";
  string sisaname = (gnssid==2) ? "sisa" : "gpsura";
  string query="select distinct(value) from "+sisaname+" where "+period+" and sigid='"+to_string(sigid)+"' group by gnssid,sv,sigid,time(10m)";

  cout<<"query: "<<query<<endl;
  cout<<"url: "<<(url + mc.urlEncode(query))<<endl;
  auto res = mc.getURL(url + mc.urlEncode(query));

  auto j = nlohmann::json::parse(res);
  //  cout<<j<<endl;
  
  for(const auto& sv : j["results"][0]["series"]) {
    const auto& tags=sv["tags"];
    SatID id{(unsigned int)std::stoi((string)tags["gnssid"]), (unsigned int)std::stoi((string)tags["sv"]), (unsigned int)std::stoi((string)tags["sigid"])};

    for(const auto& v : sv["values"]) {
      auto sisa = (int)v[1];

      g_stats[id][(int)v[0]].sisa = sisa;
    }
  }

  string healthname = (gnssid == 2) ? "galhealth" : "gpshealth";
  string healthfieldname = (gnssid==2) ? "e1bhs" : "value";
  res = mc.getURL(url + mc.urlEncode("select distinct("+healthfieldname+") from "+healthname+" where "+period+" and sigid='"+to_string(sigid)+"' group by gnssid,sv,sigid,time(10m)"));
  j = nlohmann::json::parse(res);
  
  for(const auto& sv : j["results"][0]["series"]) {
    const auto& tags=sv["tags"];
    SatID id{(unsigned int)std::stoi((string)tags["gnssid"]), (unsigned int)std::stoi((string)tags["sv"]), (unsigned int)std::stoi((string)tags["sigid"])};

    for(const auto& v : sv["values"]) {
      auto healthy = (int)v[1];
      g_stats[id][(int)v[0]].unhealthy = healthy; // hngg
    }
  }

  if(gnssid == 2) {
    res = mc.getURL(url + mc.urlEncode("select distinct(e1bdvs) from galhealth where "+period+" and sigid='"+to_string(sigid)+"' group by gnssid,sv,sigid,time(10m)"));
    j = nlohmann::json::parse(res);
    
    for(const auto& sv : j["results"][0]["series"]) {
      const auto& tags=sv["tags"];
      SatID id{(unsigned int)std::stoi((string)tags["gnssid"]), (unsigned int)std::stoi((string)tags["sv"]), (unsigned int)std::stoi((string)tags["sigid"])};
      
      for(const auto& v : sv["values"]) {
	auto dhealthy = (int)v[1]; // if true, "working without guarantee"
	g_stats[id][(int)v[0]].dataunhealthy = dhealthy; 
      }
    }
  }  
  res = mc.getURL(url + mc.urlEncode("select count(\"field\") from osnma where "+period+" and sigid='"+to_string(sigid)+"' group by gnssid,sv,sigid,time(10m)"));
  j = nlohmann::json::parse(res);
  
  for(const auto& sv : j["results"][0]["series"]) {
    const auto& tags=sv["tags"];
    SatID id{(unsigned int)std::stoi((string)tags["gnssid"]), (unsigned int)std::stoi((string)tags["sv"]), (unsigned int)std::stoi((string)tags["sigid"])};


    for(const auto& v : sv["values"]) {
      auto osnma = (int)v[1];
      if(!g_stats[id][(int)v[0]].osnma)
	g_stats[id][(int)v[0]].osnma = osnma;
      else
	(*g_stats[id][(int)v[0]].osnma) += osnma;
    }
  }

  
  res = mc.getURL(url + mc.urlEncode("select max(\"eph-age\") from ephemeris where "+period+" and sigid='"+to_string(sigid)+"' group by gnssid,sv,sigid,time(10m)"));
  j = nlohmann::json::parse(res);
  for(const auto& sv : j["results"][0]["series"]) {
    const auto& tags=sv["tags"];
    SatID id{(unsigned int)std::stoi((string)tags["gnssid"]), (unsigned int)std::stoi((string)tags["sv"]), (unsigned int)std::stoi((string)tags["sigid"])};
    

    for(const auto& v : sv["values"]) {
      if(v.size() > 1 && v[1] != nullptr) {
        int seconds = (int)v[1];
        if(seconds > 86400) { // probably wraparound
        }
        else if(seconds > 4*3600) {
          g_stats[id][(int)v[0]].expired = 1;
          cout<<makeSatIDName(id)<<": "<<humanTimeShort(v[0])<<" " << seconds<<endl;
        }
        else if(seconds > 2*3600)
          g_stats[id][(int)v[0]].ripe = (int)v[1] > 7200;
      }
    }
  }

  ///////////////////// rtcm-eph

  
  string rtcmQuery = "select mean(\"radial\") from \"rtcm-eph-correction\" where "+period+" and sigid='"+to_string(sigid)+"' and gnssid='"+to_string(gnssid)+"' and src='"+to_string(rtcmsrc)+"' group by gnssid,sv,sigid,time(10m)";
  cout<<"rtcmquery: "<<rtcmQuery<<endl;
  res = mc.getURL(url + mc.urlEncode(rtcmQuery));
  j = nlohmann::json::parse(res);
  for(const auto& sv : j["results"][0]["series"]) {
    try {
      const auto& tags=sv["tags"];
      SatID id{(unsigned int)std::stoi((string)tags["gnssid"]), (unsigned int)std::stoi((string)tags["sv"]), (unsigned int)std::stoi((string)tags["sigid"])};
      for(const auto& v : sv["values"]) {
        try {
          auto val = (double)v[1]; // might trigger exception
          g_stats[id][(int)v[0]].rtcmDist = val;
        }
        catch(...){ continue; }
      }
    }
    catch(...) {
      continue;
    }
  }

  
  /////////////////////

  ///////////////////// rtcm-clock

  
  string rtcmClockQuery = "select mean(\"dclock0\") from \"rtcm-clock-correction\" where "+period+" and sigid='"+to_string(sigid)+"' and gnssid='"+to_string(gnssid)+"' and src='"+to_string(rtcmsrc)+"' group by gnssid,sv,sigid,time(10m)";
  cout<<"rtcmquery: "<<rtcmClockQuery<<endl;
  res = mc.getURL(url + mc.urlEncode(rtcmClockQuery));
  j = nlohmann::json::parse(res);
  for(const auto& sv : j["results"][0]["series"]) {
    try {
      const auto& tags=sv["tags"];
      SatID id{(unsigned int)std::stoi((string)tags["gnssid"]), (unsigned int)std::stoi((string)tags["sv"]), (unsigned int)std::stoi((string)tags["sigid"])};
      for(const auto& v : sv["values"]) {
        try {
          auto val = (double) v[1]; // might trigger an exception
          if(g_stats.count(id)) // we have some bad data it appears
            g_stats[id][(int)v[0]].rtcmDClock = val;
        }
        catch(...){ continue; }
      }
    }
    catch(...) {
      continue;
    }
  }

  
  /////////////////////


                                  
  map<SatID, map<time_t, GalileoMessage>> galephs;
  map<SatID, map<time_t, GPSState>> gpsephs;

  res = mc.getURL(url + mc.urlEncode("select * from \"ephemeris-actual\" where "+period+" and sigid='"+to_string(sigid)+"' and gnssid='"+to_string(gnssid)+"' group by gnssid,sv,sigid"));
  j = nlohmann::json::parse(res);
  for(const auto& sv : j["results"][0]["series"]) {
    try {
      const auto& tags=sv["tags"];
      SatID id{(unsigned int)std::stoi((string)tags["gnssid"]), (unsigned int)std::stoi((string)tags["sv"]), (unsigned int)std::stoi((string)tags["sigid"])};
      //      cout << makeSatIDName(id) <<": "<<sv["columns"] << endl;
      map<string, int> cmap;
      for(const auto& c : sv["columns"]) {
        cmap[c]=cmap.size();
      }

      for(const auto& v : sv["values"]) {
          //        cout << makeSatIDName(id)<<": crc "<<v[cmap["crc"]]<<" e " <<v[cmap["e"]] <<  endl;
        if(id.gnss==2) {
          GalileoMessage gm;
          gm.e = v[cmap["e"]];
          gm.crc = v[cmap["crc"]];
          gm.crs = v[cmap["crs"]];
          gm.cuc = v[cmap["cuc"]];
          gm.cus = v[cmap["cus"]];
          gm.cic = v[cmap["cic"]];
          gm.cis = v[cmap["cis"]];
          gm.sqrtA = v[cmap["sqrta"]];
          gm.t0e = v[cmap["t0e"]];
          gm.m0 = v[cmap["m0"]];
          gm.deltan = v[cmap["deltan"]];
          gm.omega0 = v[cmap["omega0"]];
          gm.omegadot = v[cmap["omegadot"]];
          gm.idot = v[cmap["idot"]];
          gm.omega = v[cmap["omega"]];
          gm.i0 = v[cmap["i0"]];
          gm.t0e = v[cmap["t0e"]];
          gm.iodnav = v[cmap["iod"]];
          if(cmap.count("af0")) {
            gm.af0 = v[cmap["af0"]];
            gm.af1 = v[cmap["af1"]];
            gm.af2 = v[cmap["af2"]];
            gm.t0c = v[cmap["t0c"]];
          }

          Point pos;
          galephs[id][v[cmap["time"]]]= gm;
        }
        else if(id.gnss==0) {
          GPSState gm{};
          gm.e = v[cmap["e"]];
          gm.crc = v[cmap["crc"]];
          gm.crs = v[cmap["crs"]];
          gm.cuc = v[cmap["cuc"]];
          gm.cus = v[cmap["cus"]];
          gm.cic = v[cmap["cic"]];
          gm.cis = v[cmap["cis"]];
          gm.sqrtA = v[cmap["sqrta"]];
          gm.t0e = v[cmap["t0e"]];
          gm.m0 = v[cmap["m0"]];
          gm.deltan = v[cmap["deltan"]];
          gm.omega0 = v[cmap["omega0"]];
          gm.omegadot = v[cmap["omegadot"]];
          gm.idot = v[cmap["idot"]];
          gm.omega = v[cmap["omega"]];
          gm.i0 = v[cmap["i0"]];
          gm.t0e = v[cmap["t0e"]];
          gm.gpsiod = v[cmap["iod"]];
          if(cmap.count("af0")) {
            gm.af0 = v[cmap["af0"]];
            gm.af1 = v[cmap["af1"]];
            gm.af2 = v[cmap["af2"]];
            gm.t0c = v[cmap["t0c"]];
          }

          Point pos;
          gpsephs[id][v[cmap["time"]]]= gm;
        }
      }

    }
    catch(...) {
      continue;
    }
  }

  cout<<"Gathered ephemerides for "<<galephs.size()<<" galileo + "<<gpsephs.size()<<" GPS signals"<<endl;
  /////////////////////

  map<SatID, map<time_t, SP3Entry>> sp3s;
  string spq3="select * from \"sp3\" where "+period+" and sp3src =~ /"+sp3src+"/ and gnssid='"+to_string(gnssid)+"' group by gnssid,sv,sigid";
  cout<<"sp3 query: "<<spq3<<endl;
  res = mc.getURL(url + mc.urlEncode(spq3));
  j = nlohmann::json::parse(res);
  cout<<"Gathered sp, got "<< j["results"][0]["series"].size()<< " tags"<<endl;
  for(const auto& sv : j["results"][0]["series"]) {
    try {
      const auto& tags=sv["tags"];
      SatID id{(unsigned int)std::stoi((string)tags["gnssid"]), (unsigned int)std::stoi((string)tags["sv"]), (unsigned int)sigid};

      // SP3 data does not have a sigid, it refers to the center of mass, not the antenna phase center
      //      cout << makeSatIDName(id) <<": "<<sv["columns"] << endl;
      map<string, int> cmap;
      for(const auto& c : sv["columns"]) {
        cmap[c]=cmap.size();
      }
            
      for(const auto& v : sv["values"]) {
        //        cout << makeSatIDName(id)<<": time "<<v[cmap["time"]] <<" x "<<v[cmap["x"]]<<" y " <<v[cmap["y"]] << " z " << v[cmap["z"]] << endl;
        SP3Entry e;
        e.t = v[cmap["time"]]; // UTC!!
        e.x = v[cmap["x"]];
        e.y = v[cmap["y"]];
        e.z = v[cmap["z"]];
        e.clockBias = v[cmap["clock-bias"]];
        sp3s[id][e.t]=e;
      }
    }
    catch(std::exception& e)
    {
      cerr<<"Error: "<<e.what()<<endl;
    }
  }

  ofstream csv("sp3.csv");
  csv<<"timestamp gnss sv sigid zerror"<<endl;

  InfluxPusher idb(influxDBName);

  map<SatID, Stats> sp3zerrors, sp3clockerrors;
  /////////////////////
  for(const auto& svsp3 : sp3s) {
    const auto& id = svsp3.first;
    const auto& es = svsp3.second;
    //    cout<<"Looking at SP3 for " << makeSatIDName(id)<<", have "<<es.size()<< " entries"<<endl;
    for(const auto& e : es) {
      //      cout << humanTimeShort(e.first)<<": ";

      if(id.gnss == 2) {
        const auto& svephs = galephs[id];
        auto iter = svephs.lower_bound(e.first);

        // this logic is actually sort of wrong & ignores the last ephemeris
        
        
        if(iter != svephs.end() && iter != svephs.begin()) {
          --iter;
          //        cout << "found ephemeris from "<< humanTimeShort(iter->first)<<" iod "<<iter->second.iodnav;

          // our UTC timestamp from SP3 need to be converted into a tow
          
          int offset = id.gnss ? 935280000 : 315964800;
          int sp3tow = (e.first - offset) % (7*86400);
          Point epos;
          getCoordinates(sp3tow, iter->second, &epos);
          
          double clkoffset= iter->second.getAtomicOffset(sp3tow).first - e.second.clockBias;
          
          //        cout<<" "<<iter->second.getAtomicOffset(sp3tow).first <<" v " << e.second.clockBias<<" ns ";
          //        cout << " ("<<epos.x<<", "<<epos.y<<", "<<epos.z<<")  - > ("<<e.second.x<<", "<<e.second.y<<", "<<e.second.z<<") " ;
          //        cout <<" ("<<epos.x - e.second.x<<", "<<epos.y - e.second.y <<", "<<epos.z - e.second.z<<")";
          
          Point sp3pos(e.second.x, e.second.y, e.second.z);
          Vector v(epos, sp3pos);
          
          //        cout<< " -> " << v.length();
          
          Vector dir(Point(0,0,0), sp3pos);
          dir.norm();
          
          Point cv=sp3pos;
          cv.x -= 0.519 * dir.x;
          cv.y -= 0.519 * dir.y;
          cv.z -= 0.519 * dir.z;
          
          Vector v2(epos, cv);
          //        cout<< " -> " << v2.length();
          
          //        cout<<" z-error: "<<dir.inner(v);
          
          csv << e.first << " " << id.gnss <<" " << id.sv << " " << id.sigid <<" " << dir.inner(v) << endl;
          idb.addValue({{"gnssid", id.gnss}, {"sv", id.sv}, {"sp3src", sp3src}},
                       "sp3delta",
                       {{"ecef-dx", v.x}, {"ecef-dy", v.y}, {"ecef-dz", v.z}, {"sv-dz", dir.inner(v)}, {"dclock", clkoffset},
                                                                                                         {"iod-nav",iter->second.iodnav}},
                       e.first);
          
          sp3clockerrors[id].add(clkoffset); // nanoseconds
          sp3zerrors[id].add(100*dir.inner(v) - 80); // meters -> cm
        }
      }
      else if(id.gnss==0) {
        const auto& svephs = gpsephs[id];
        // this is keyed on the moment of _issue_
        auto iter = svephs.lower_bound(e.first);
        if(iter != svephs.end() && iter != svephs.begin()) {
          --iter;
          //        cout << "found ephemeris from "<< humanTimeShort(iter->first)<<" iod "<<iter->second.iodnav;
          
          // our UTC timestamp from SP3 need to be converted into a tow
          
          int offset = 315964800;
          int sp3tow = (e.first - offset) % (7*86400);
          Point epos;
          getCoordinates(sp3tow, iter->second, &epos);
          
          double clkoffset= getGPSAtomicOffset(sp3tow, iter->second).first - e.second.clockBias;
          
          //        cout<<" "<<iter->second.getAtomicOffset(sp3tow).first <<" v " << e.second.clockBias<<" ns ";
          //        cout << " ("<<epos.x<<", "<<epos.y<<", "<<epos.z<<")  - > ("<<e.second.x<<", "<<e.second.y<<", "<<e.second.z<<") " ;
          //        cout <<" ("<<epos.x - e.second.x<<", "<<epos.y - e.second.y <<", "<<epos.z - e.second.z<<")";
          
          Point sp3pos(e.second.x, e.second.y, e.second.z);
          Vector v(epos, sp3pos);
          
          //        cout<< " -> " << v.length();
          
          Vector dir(Point(0,0,0), sp3pos);
          dir.norm();
          
          Point cv=sp3pos;
          cv.x -= 0.519 * dir.x;
          cv.y -= 0.519 * dir.y;
          cv.z -= 0.519 * dir.z;
          
          Vector v2(epos, cv);
          //        cout<< " -> " << v2.length();
          
          //        cout<<" z-error: "<<dir.inner(v);
          
          csv << e.first << " " << id.gnss <<" " << id.sv << " " << id.sigid <<" " << dir.inner(v) << endl;
          idb.addValue({{"gnssid", id.gnss}, {"sv", id.sv}, {"sp3src", sp3src}},
                       "sp3delta",
                       {{"ecef-dx", v.x}, {"ecef-dy", v.y}, {"ecef-dz", v.z}, {"sv-dz", dir.inner(v)}, {"dclock", clkoffset},
                                                                                                         {"iod-nav",iter->second.gpsiod}},
                       e.first);
          
          sp3clockerrors[id].add(clkoffset); // nanoseconds
          sp3zerrors[id].add(100*dir.inner(v)); // meters -> cm
        }
      }
    }
  }

  ///// 
  string dishesQuery = "select iod,sv from \"ephemeris-actual\" where "+period+" and sigid='"+to_string(sigid)+"' and gnssid='"+to_string(gnssid)+"' and iod < 128";
  cout<<"dishesquery: "<<dishesQuery<<endl;
  res = mc.getURL(url + mc.urlEncode(dishesQuery));
  cout<<res<<endl;
  j = nlohmann::json::parse(res);
  map<time_t, set<int>> dishcount;
  set<int> totsvs;
  for(const auto& sv : j["results"][0]["series"]) {
    for(const auto& v : sv["values"]) {
      try {
	int sv = (unsigned int)std::stoi((string)v[2]);
	int t = (int)v[0];
	//	t &= (~31);
	dishcount[t].insert(sv);
	totsvs.insert(sv);
      }
      catch(exception& e) {
	cerr<<"error: "<<e.what()<<endl;
	continue;
      }
    }
  }
  map<time_t, unsigned int> maxcounts;
  for(const auto& dc : dishcount) {
    auto& bin = maxcounts[dc.first - (dc.first % 3600)];
    if(bin < dc.second.size())
      bin = dc.second.size();
    cout << dc.first<<" "<<humanTimeShort(dc.first) <<", " << fmt::sprintf("%2d", dc.second.size())<<": ";
    for(const auto& n : totsvs) {
      if(dc.second.count(n))
	cout<<fmt::sprintf("%2d ", n);
      else
	cout<<"   ";
    }
    cout<<"\n";
  }

  ofstream hrcounts("hrcounts.csv");
  hrcounts<<"timestamp,dishcount\n";
  for(const auto& mc: maxcounts)
    hrcounts<<mc.first<<","<<mc.second<<"\n";
  
  /////////////////////
  
  /*
  g_stats[{2,19,1}];
  */
  unsigned int maxintervals=0;
  time_t start=time(0), stop=0;
  for(const auto& sv : g_stats) {
    if(sv.second.size()) {
      if(sv.second.begin()->first < start)
        start = sv.second.begin()->first;
      if(sv.second.rbegin()->first > stop)
        stop = sv.second.rbegin()->first;
    }
    unsigned int liveInterval=0;
    for(const auto i : sv.second) {
      if(i.second.sisa.has_value())
        liveInterval++;
      else {
        //        cout<<makeSatIDName(sv.first)<<": no Sisa, "<< i.second.rtcmDClock.has_value()<<" " << i.second.unhealthy.has_value()<<" " <<i.second.rtcmDist<<" ripe "<<i.second.ripe<<endl;
      }
    }
    
    if(liveInterval > maxintervals) 
      maxintervals = liveInterval;
  }

  cout<<"Report on "<<g_stats.size()<<" SVs from "<<humanTime(start) <<" to " <<humanTime(stop) << endl;
  int totnapa=0, totunhealthy=0, tothealthy=0, tottesting=0;
  int totunobserved=0;
  int totripe = 0,  totexpired = 0;
  Stats totRTCM;
  ofstream texstream("stats.tex");
  for(const auto& sv : g_stats) {

    int napa=0, unhealthy=0, healthy=0, testing=0, ripe=0, expired=0;

    Stats rtcm, clockRtcm;

    for(const auto& i : sv.second) {
      //      cout<<humanTimeShort(i.first)<<" "<<((i.second.sisa.has_value()) ? "S" : " ")<<" ";
      if(i.second.rtcmDist >= 0) {
        rtcm.add(i.second.rtcmDist);
        totRTCM.add(i.second.rtcmDist);
      }

      if(i.second.rtcmDClock)
        clockRtcm.add(*i.second.rtcmDClock * 100);
      
      if(i.second.ripe)
        ripe++;
      if(i.second.expired)
        expired++;

      if(i.second.unhealthy) {
        if(*i.second.unhealthy==1)
          unhealthy++;
        else if(*i.second.unhealthy==3)
          testing++;
        else {
	  if(i.second.dataunhealthy && *i.second.dataunhealthy) {  // this is 'working without guarantee'
	    unhealthy++;
	  }
	  else if(i.second.sisa) {
            if(*i.second.sisa == 255)
              napa++;
            else
              healthy++;
          }
          else
            healthy++;
        }
      }
      else if(i.second.sisa) {
        if(*i.second.sisa == 255)
          napa++;
      }
    }
    //    cout<<endl;
    totnapa += napa;
    totunhealthy += unhealthy;
    tottesting += testing;
    tothealthy += healthy;
    totripe += ripe;
    totexpired += expired;
    int liveInterval=0;

    for(const auto i : sv.second)
      if(i.second.sisa.has_value())
        liveInterval++;

    
    totunobserved += maxintervals - liveInterval;
    
    cout<<fmt::sprintf("%s: %6.2f%% unobserved, %6.2f%% unhealthy, %6.2f%% healthy, %6.2f%% testing, %6.2f%% napa, %6.2f%% ripe, %6.2f%% expired",
                       makeSatPartialName(sv.first),
                       100.0*(maxintervals-liveInterval)/maxintervals,
                       100.0*unhealthy/maxintervals,
                       100.0*healthy/maxintervals,
                       100.0*testing/maxintervals,
                       100.0*napa/maxintervals,
                       100.0*ripe/maxintervals,
                       100.0*expired/maxintervals);

    texstream << fmt::sprintf("%s & %6.2f\\%% & %6.2f\\%% & %6.2f\\%% & %6.2f\\%% & %6.2f\\%% & %6.2f\\%% & %6.2f\\%%\\\\",
                       makeSatPartialName(sv.first),
                       100.0*(maxintervals-liveInterval)/maxintervals,
                       100.0*unhealthy/maxintervals,
                       100.0*healthy/maxintervals,
                       100.0*testing/maxintervals,
                       100.0*napa/maxintervals,
                       100.0*ripe/maxintervals,
                             100.0*expired/maxintervals) << endl;
    
    if(!rtcm.empty()) 
      cout<<fmt::sprintf(", %.1f - %.1f - %.1f cm",
                         rtcm.done().quantile(0.10)/10, rtcm.done().median()/10, rtcm.done().quantile(0.9)/10);


    if(!clockRtcm.empty()) 
      cout<<fmt::sprintf(", c %.1f - %.1f - %.1f cm",
                         clockRtcm.done().quantile(0.10), clockRtcm.done().median(), clockRtcm.done().quantile(0.9));

    if(!sp3zerrors[sv.first].empty()) {
      const auto& z = sp3zerrors[sv.first];
      cout<<fmt::sprintf(", z %.1f - %.1f - %.1f cm",
                         z.done().quantile(0.10), z.done().median(), z.done().quantile(0.9));
    }
    
    cout<<endl;
    
  }

 
  
  cout<<"------------------------------------------------------------------------------------------"<<endl;
  cout<<fmt::sprintf("Tot: %6.2f%% unobserved, %6.2f%% unhealthy, %6.2f%% healthy, %6.2f%% testing, %6.2f%% napa, %6.2f%% ripe, %6.2f%% expired",
                     100.0*(totunobserved)/maxintervals/g_stats.size(),
                     100.0*totunhealthy/maxintervals/g_stats.size(),
                     100.0*tothealthy/maxintervals/g_stats.size(),
                     100.0*tottesting/maxintervals/g_stats.size(),
                     100.0*totnapa/maxintervals/g_stats.size(),
                     100.0*totripe/maxintervals/g_stats.size(),
                     100.0*totexpired/maxintervals/g_stats.size());

  texstream<<fmt::sprintf("\\hline\nTot & %6.2f\\%% & %6.2f\\%% & %6.2f\\%% & %6.2f\\%% & %6.2f\\%% & %6.2f\\%% & %6.2f\\%%\\\\",
                     100.0*(totunobserved)/maxintervals/g_stats.size(),
                     100.0*totunhealthy/maxintervals/g_stats.size(),
                     100.0*tothealthy/maxintervals/g_stats.size(),
                     100.0*tottesting/maxintervals/g_stats.size(),
                     100.0*totnapa/maxintervals/g_stats.size(),
                     100.0*totripe/maxintervals/g_stats.size(),
                          100.0*totexpired/maxintervals/g_stats.size()) <<endl;


  if(!totRTCM.empty())
    cout<<fmt::sprintf(", %.1f - %.1f - %.1f cm",
                       totRTCM.done().quantile(0.10)/10, totRTCM.done().median()/10, totRTCM.done().quantile(0.9)/10);
  
  cout<<endl;

}
catch(exception& e)
{
  cerr<<"Fatal error: "<<e.what()<<endl;
  return EXIT_FAILURE;
}

