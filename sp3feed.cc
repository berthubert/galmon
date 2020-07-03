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

  CLI::App app(program);
  vector<string> fnames;
  string sp3src("default");
  app.add_flag("--version", doVERSION, "show program version and copyright");
  app.add_option("--sp3src,-s", sp3src, "Identifier of SP3 source");
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
    cout<<fn<<endl;
    while(sp3.get(e)) {
      // XXX LEAP SECOND ADJUSTMENT FIXED AT 18 SECONDS
      idb.addValue({{"gnssid", e.gnss}, {"sv", e.sv}, {"sp3src", sp3src}}, "sp3", {{"x", e.x}, {"y", e.y}, {"z", e.z}, {"clock-bias", e.clockBias}}, e.t + 18);
    }
  }
}


