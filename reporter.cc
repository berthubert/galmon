#include "ext/powerblog/h2o-pp.hh"
#include "minicurl.hh"
#include <iostream>
#include "navmon.hh"
#include "fmt/format.h"
#include "fmt/printf.h"

#include "CLI/CLI.hpp"
#include "version.hh"

static char program[]="reporter";

using namespace std;

extern const char* g_gitHash;

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
  std::optional<int> sisa;
  bool ripe{false};
  bool expired{false};
};


map<SatID, map<time_t,IntervalStat>> g_stats;

int main(int argc, char **argv)
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
  app.add_flag("--version", doVERSION, "show program version and copyright");
  app.add_option("--period,-p", periodarg, "period over which to report (1h, 1w)");
  app.add_option("--begin,-b", beginarg, "Beginning");
  app.add_option("--end,-e", endarg, "End");
  app.add_option("--sigid,-s", sigid, "Signal identifier. 1 or 5 for Galileo.");
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

  if(beginarg.empty() && endarg.empty()) 
    period = "time > now() - "+periodarg;
  else {
    period = "time > '"+ beginarg +"' and time <= '" + endarg +"'";
    cout<<"Period: "<<period<<endl;
  }

  string url="http://127.0.0.1:8086/query?db="+influxDBName+"&epoch=s&q=";
  
  auto res = mc.getURL(url + mc.urlEncode("select distinct(value) from sisa where "+period+" and sigid='"+to_string(sigid)+"' group by gnssid,sv,sigid,time(10m)"));



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

  
  res = mc.getURL(url + mc.urlEncode("select distinct(e1bhs) from galhealth where "+period+" and sigid='"+to_string(sigid)+"' group by gnssid,sv,sigid,time(10m)"));
  j = nlohmann::json::parse(res);
  
  for(const auto& sv : j["results"][0]["series"]) {
    const auto& tags=sv["tags"];
    SatID id{(unsigned int)std::stoi((string)tags["gnssid"]), (unsigned int)std::stoi((string)tags["sv"]), (unsigned int)std::stoi((string)tags["sigid"])};


    for(const auto& v : sv["values"]) {
      auto healthy = (int)v[1];
      g_stats[id][(int)v[0]].unhealthy = healthy;
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
  
  g_stats.erase({2,14,1});
  g_stats.erase({2,18,1});
  g_stats.erase({2,14,5});
  g_stats.erase({2,18,5});

  g_stats[{2,19,1}];
  
  unsigned int maxintervals=0;
  time_t start=time(0), stop=0;
  for(const auto& sv : g_stats) {
    if(sv.second.size()) {
      if(sv.second.begin()->first < start)
        start = sv.second.begin()->first;
      if(sv.second.rbegin()->first > stop)
        stop = sv.second.rbegin()->first;
    }
      
    if(sv.second.size() > maxintervals) 
      maxintervals = sv.second.size();
  }

  cout<<"Report on "<<g_stats.size()<<" SVs from "<<humanTime(start) <<" to " <<humanTime(stop) << endl;
  int totnapa=0, totunhealthy=0, tothealthy=0, tottesting=0;
  int totunobserved=0;
  int totripe = 0,  totexpired = 0;
  for(const auto& sv : g_stats) {

    int napa=0, unhealthy=0, healthy=0, testing=0, ripe=0, expired=0;
    for(const auto& i : sv.second) {
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
          if(i.second.sisa) {
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
    totnapa += napa;
    totunhealthy += unhealthy;
    tottesting += testing;
    tothealthy += healthy;
    totripe += ripe;
    totexpired += expired;
    totunobserved += maxintervals-sv.second.size();
     
    cout<<fmt::sprintf("E%02d: %6.2f%% unobserved, %6.2f%% unhealthy, %6.2f%% healthy, %6.2f%% testing, %6.2f%% napa, %6.2f%% ripe, %6.2f%% expired",
                       sv.first.sv,
                       100.0*(maxintervals-sv.second.size())/maxintervals,
                       100.0*unhealthy/maxintervals,
                       100.0*healthy/maxintervals,
                       100.0*testing/maxintervals,
                       100.0*napa/maxintervals,
                       100.0*ripe/maxintervals,
                       100.0*expired/maxintervals
                       )<<endl;
  }
  cout<<"------------------------------------------------------------------------------------------"<<endl;
  cout<<fmt::sprintf("Tot: %6.2f%% unobserved, %6.2f%% unhealthy, %6.2f%% healthy, %6.2f%% testing, %6.2f%% napa, %6.2f%% ripe, %6.2f%% expired",
                     100.0*(totunobserved)/maxintervals/g_stats.size(),
                     100.0*totunhealthy/maxintervals/g_stats.size(),
                     100.0*tothealthy/maxintervals/g_stats.size(),
                     100.0*tottesting/maxintervals/g_stats.size(),
                     100.0*totnapa/maxintervals/g_stats.size(),
                     100.0*totripe/maxintervals/g_stats.size(),
                     100.0*totexpired/maxintervals/g_stats.size()
                     )<<endl;

}


