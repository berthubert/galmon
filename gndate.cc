#include "navmon.hh"
#include <iostream>
#include "CLI/CLI.hpp"
#include "version.hh"

extern const char* g_gitHash;
using namespace std;

int main(int argc, char** argv)
{
  string program("gndate");
  CLI::App app(program);
  string date;
  bool doGPSWN{false}, doGALWN{false}, doVERSION{false}, doUTC{false};
  app.add_flag("--version", doVERSION, "show program version and copyright");
  app.add_option("--date,-d", date, "yyyy-mm-dd hh:mm");
  app.add_flag("--utc,-u", doUTC, "Interpret --date,-d as UTC");
  app.add_flag("--gps-wn", doGPSWN, "Print GPS week number");
  app.add_flag("--gal-wn", doGALWN, "Print GPS week number");
  try {
    app.parse(argc, argv);
  } catch(const CLI::Error &e) {
    return app.exit(e);
  }

  if(doVERSION) {
    showVersion(program.c_str(), g_gitHash);
    exit(0);
  }

  
  time_t now;
  if(date.empty())
    now = time(0);
  else {
    if(doUTC)
      setenv("TZ", "UTC", 1);
    now = parseTime(date);
  }
  
  int wn, tow;

  if(doGPSWN) {
    getGPSDateFromUTC(now, wn, tow);
    cout<<wn<<endl;
  }
  else if(doGALWN) {
    getGalDateFromUTC(now, wn, tow);
    cout<<wn<<endl;
  }
  else {
    getGPSDateFromUTC(now, wn, tow);
    cout<<"GPS Week Number (non-wrapped): "<< wn << ", tow " << tow << endl;
    getGalDateFromUTC(now, wn, tow);
    cout<<"Galileo Week Number: "<< wn << ", tow " << tow << endl;
  }
}
