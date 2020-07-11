#include "rtcm.hh"
#include "bits.hh"
#include <vector>
#include <iostream>
#include "nmmsender.hh"
#include "CLI/CLI.hpp"
#include "swrappers.hh"
#include "sclasses.hh"
#include "version.hh"

using namespace std;

bool RTCMReader::get(RTCMFrame& rf)
{
  int c;
  while( ((c=fgetc(d_fp)) != -1) && c != 211) {
    cerr<<"Skipped, not yet 211 character "<<endl;
    continue;
  }

  if(c != 211) {
    cerr<<"EOF"<<endl;
    return false;
  }
  //  cout<<"Found preamble"<<endl;
  unsigned char buffer[2];
  if(fread((char*)buffer, 1, 2, d_fp) != 2)
    return false; 

  //  cout<<"Got two byte buffer"<<endl;
  // 6 bits reserved, 10 bits of size
  int size = getbitu(buffer, 6, 10);
  size += 3;
  //  cout<<"Now reading "<<size<<" bytes"<<endl;
  vector<char> buf(size);
  if((int)fread(&buf[0], 1, size, d_fp) != size)
    return false;

  //  cout<<"Returning true"<<endl;
  rf.payload.assign(&buf[0], size - 3);
  return true;
}


/*
Message types:
Type 1240, Galileo orbit corrections to Broadcast Ephemeris
Type 1241, Galileo clock corrections to Broadcast Ephemeris
1057 message for GPS orbit corrections to Broadcast Ephemeris,
1058 message for GPS clock corrections to Broadcast Ephemeris,
*/


/* 1057 (GPS), 1240 (Galileo)
   0    12 bits message number
   12   20 bits galileo SOW
   32   4 bits SSR update interval
   36   1 bit multiple message indicator
   37   1 bit Satellite Reference Datum (0: ITRF, 1: regional)
   38   4 bit (IOD SSR)
   42   16 bits SSR provider ID
   58   4 bits SSR solution ID
   62   6 bits number of satellites
   68
   Repeat, starting from pos 68:
   0   6 bits sv
   6   10 bits IODE nav                      / 8 for GPS, a shift of -2 everywhere below
   16   22 bits delta radial (0.1mm)
   38   20 bits along track (0.4mm)
   58   20 bits cross track (0.4mm)
   78   21 bits dot delta radial (0.001 mm/s)
   99   19 bits dot delta along (0.004 mm/s)
   118  19 bits dot delta cross-track (0.004 mm/s)
   137 
*/


/* 
   1058 (GPS), 1241 (Galileo)
   0    12 bits message number
   12   20 bits galileo SOW (GPS Epoch time)
   32    4 bits "UDI" ("SSR Update interval")
   36    1 bit "sync" ("multiple message indicator")
   37    4 bit (IOD SSR, link to 1240 I think)
   41   16 bits SSR provider ID
   57    4 bits SSR solution ID
   61    6 bits number of satellites
   67

   67
   Repeat, starting from pos 68:
   0   6  bits sv
   6   22 bits dclk[0] 1e-4 meter // 0.1 mm
   28  21 bits dclk[1] 1e-6 meter/s // 0.001 mm/s
   49  27 bits dclk[2] 2e-8 meter // 0.0002mm/s^2 
   76

   Reference time is Epoch Time + 0.5* SSR update interval, which can be zero.
*/

/*
  CLKA[0,1]_DEU1 – containing the SSR corrections regarding the satellites’ Antenna Phase Center
  CLKC[0,1]_DEU1 – containing the SSR corrections regarding the satellites’ Center of Mass.
*/

static char program[]="rtcmtool";
uint16_t g_srcid{0};

int main(int argc, char** argv)
{
  //  time_t starttime=time(0);
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  vector<string> destinations;

  bool doVERSION{false}, doSTDOUT{false};
  CLI::App app(program);
  app.add_option("--destination,-d", destinations, "Send output to this IPv4/v6 address");
  app.add_option("--station", g_srcid, "Station id")->required();
  app.add_flag("--version", doVERSION, "show program version and copyright");
  app.add_flag("--stdout", doSTDOUT, "Emit output to stdout");
  try {
    app.parse(argc, argv);
  } catch(const CLI::Error &e) {
    return app.exit(e);
  }

  if(doVERSION) {
    showVersion(program, g_gitHash);
    exit(0);
  }


  signal(SIGPIPE, SIG_IGN);
  NMMSender ns;
  ns.d_debug = true;
  for(const auto& s : destinations) {
    auto res=resolveName(s, true, true);
    if(res.empty()) {
      cerr<<"Unable to resolve '"<<s<<"' as destination for data, exiting"<<endl;
      exit(EXIT_FAILURE);
    }
    ns.addDestination(s); // ComboAddress(s, 29603));
  }
  if(doSTDOUT)
    ns.addDestination(1);

  ns.launch();
  
  RTCMReader rr(0);
  RTCMFrame rf;
  while(rr.get(rf)) {
    //    cerr<<"Got a "<<rf.payload.size()<<" byte frame"<<endl;
    RTCMMessage rm;
    NavMonMessage nmm;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, & ts);
    nmm.set_type(NavMonMessage::RTCMMessageType);
    nmm.set_localutcseconds(ts.tv_sec);
    nmm.set_localutcnanoseconds(ts.tv_nsec);
    nmm.set_sourceid(g_srcid);
    nmm.mutable_rm()->set_contents(rf.payload);
    ns.emitNMM(nmm);

    //    rm.parse(rf.payload);
  }
}
