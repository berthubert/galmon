#include "sp3.hh"
#include "influxpush.hh"
#include <iostream>
#include "navmon.hh"
#include "fmt/format.h"
#include "fmt/printf.h"

#include "CLI/CLI.hpp"
#include "version.hh"

static char program[]="sp3feed";

using namespace std;

extern const char* g_gitHash;

int main(int argc, char **argv)
{
  string influxDBName("galileo2");
  bool doVERSION=false;
  int sigid=1;
  CLI::App app(program);
  vector<string> fnames;
  app.add_flag("--version", doVERSION, "show program version and copyright");
  app.add_option("--sigid,-s", sigid, "Signal identifier. 1 or 5 for Galileo.");
  app.add_option("--influxdb", influxDBName, "Name of influxdb database");
  app.add_option("files", fnames, "filenames to parse");
  try {
    app.parse(argc, argv);
  } catch(const CLI::Error &e) {
    return app.exit(e);
  }

  if(doVERSION) {
    showVersion(program, g_gitHash);
    exit(0);
  }

  InfluxPusher idb(influxDBName);
  for(const auto& fn : fnames) {
    SP3Reader sp3(fn);
    SP3Entry e;
    SatID sid;
    cout<<fn<<endl;
    while(sp3.get(e)) {
      sid.gnss = e.gnss;
      sid.sigid = sigid;
      sid.sv = e.sv;
      // XXX LEAP SECOND ADJUSTMENT FIXED AT 18 SECONDS
      idb.addValue(sid, "sp3", {{"x", e.x}, {"y", e.y}, {"z", e.z}, {"clock-bias", e.clockBias}}, e.t + 18);
    }
  }
}


