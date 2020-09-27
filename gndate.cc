#include "navmon.hh"
#include <iostream>
#include "CLI/CLI.hpp"
#include "version.hh"

extern const char* g_gitHash;
using namespace std;

int main(int argc, char** argv)
try
{
  string program("gndate");
  CLI::App app(program);
  string date;
  int galwn{-1};
  bool doProgOutput{false};
  bool doGPSWN{false}, doGALWN{false}, doVERSION{false}, doUTC{false};
  app.add_flag("--version", doVERSION, "show program version and copyright");
  app.add_option("--date,-d", date, "yyyy-mm-dd hh:mm[:ss] hh:mm yyyymmdd hhmm");
  app.add_option("--date-gal-wn", galwn, "Give data for this Galileo week number");
  app.add_flag("--utc,-u", doUTC, "Interpret --date,-d as UTC");
  app.add_flag("--gps-wn", doGPSWN, "Print GPS week number");
  app.add_flag("--gal-wn", doGALWN, "Print GPS week number");
  app.add_flag("--prog-output", doProgOutput, "Modulate some date formats for use as parameters to programs");
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

  if(galwn >= 0) {
    time_t week=utcFromGST(galwn, 0);
    if(doProgOutput)
      cout<<influxTime(week) << endl;
    else
      cout<<humanTime(week)<< " - " << humanTime(week+7*86400) << endl;
    return 0;
  }
  
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
catch(exception& e) {
  cerr<<"Error: "<<e.what()<<endl;
}
