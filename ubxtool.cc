#define _LARGEFILE64_SOURCE
#include <sys/types.h>                                                    
#include <sys/time.h>
#include <map>
#include <sys/stat.h>                                                     
#include <fcntl.h>                                                        
#include <termios.h>                                                      
#include <stdio.h>                                                        
#include <unistd.h>
#include <strings.h>
#include <stdlib.h>
#include <string>
#include <stdint.h>
#include "ubx.hh"
#include "navmon.hh"
#include <iostream>
#include <fstream>
#include <string.h>
#include "fmt/format.h"
#include "fmt/printf.h"
#include "bits.hh"
#include "galileo.hh"
#include <arpa/inet.h>
#include "navmon.pb.h"
#include "gps.hh"
#include "glonass.hh"
#include "beidou.hh"
#include "CLI/CLI.hpp"
#include <thread>
#include <mutex>
#include "comboaddress.hh"
#include "swrappers.hh"
#include "sclasses.hh"

bool doDEBUG{false};
bool doLOGFILE{false};

struct timespec g_gnssutc;
uint16_t g_galwn;

using namespace std;

uint16_t g_srcid{0};
int g_fixtype{-1};

static int getBaudrate(int baud)
{
  if(baud==115200)
    return B115200;
  else if(baud==57600)
    return B57600;
  else if(baud==38400)
    return B38400;
  else if(baud==19200)
    return B19200;
  else if(baud==9600)
    return B9600;
  else
    throw std::runtime_error("Unknown baudrate "+to_string(baud));
}

static int g_baudval;

size_t writen2(int fd, const void *buf, size_t count)
{
  const char *ptr = (char*)buf;
  const char *eptr = ptr + count;

  ssize_t res;
  while(ptr != eptr) {
    res = ::write(fd, ptr, eptr - ptr);
    if(res < 0) {
      throw runtime_error("failed in writen2: "+string(strerror(errno)));
    }
    else if (res == 0)
      throw EofException();

    ptr += (size_t) res;
  }

  return count;
}

/* inav schedule:
1) Find plausible start time of next cycle
   Current cycle: TOW - (TOW%30)
   Next cycle:    TOW - (TOW%30) + 30

E1:

t   n   w
0   1:  2                   tow % 30 == 1           
2   2:  4                   tow % 30 == 3           
4   3:  6             TOW               5          -> set startTow, startTowFresh
6   4:  7/9                             7           
8   5:  8/10                            9          
10  6:  0          WN/TOW              11            
12  7:  0          WN/TOW              13
14  8:  0          WN/TOW              15
16  9:  0          WN/TOW              17
18  10: 0          WN/TOW              19
20  11: 1                              21          
22  12: 3                              23
24  13: 5          WN/TOW              25
26  14: 0          WN/TOW              27
28  15: 0          WN/TOW              29

E5b-1:
t   n   w
0   1:  1 (2/2)             tow % 30 == 0           
2   2:  3                   tow % 30 == 2           
4   3:  5          WN/TOW               4           -> set startTow, startTowFresh
6   4:  7/9                                       
8   5:  8/10                                       
10  6:  0          WN/TOW                          
12  7:  0          WN/TOW
14  8:  0          WN/TOW
16  9:  0          WN/TOW
18  10: 0          WN/TOW
20  11: 2                                          
22  12: 4
24  13: 6             TOW
26  14: 0          WN/TOW
28  15: 1 (1/2)    WN/TOW

*/

class UBXMessage
{
public:
  struct BadChecksum{};
  explicit UBXMessage(basic_string_view<uint8_t> src)
  {
    d_raw = src;
    if(d_raw.size() < 6)
      throw std::runtime_error("Partial UBX message");

    uint16_t csum = calcUbxChecksum(getClass(), getType(), d_raw.substr(6, d_raw.size()-8));
    if(csum != d_raw.at(d_raw.size()-2) + 256*d_raw.at(d_raw.size()-1))
      throw BadChecksum();
    
  }
  uint8_t getClass() const
  {
    return d_raw.at(2);
  }
  uint8_t getType() const
  {
    return d_raw.at(3);
  }
  std::basic_string<uint8_t> getPayload() const
  {
    return d_raw.substr(6, d_raw.size()-8);
  }
  std::basic_string<uint8_t> d_raw;
};

bool g_fromFile{false};

#if !defined(O_LARGEFILE)
#define O_LARGEFILE	0
#endif

std::pair<UBXMessage, struct timeval> getUBXMessage(int fd)
{
  static int logfile=0;
  if (doLOGFILE) {
    if(!logfile && !g_fromFile) {
    logfile = open("./logfile", O_WRONLY|O_CREAT|O_APPEND|O_LARGEFILE, 0600);
      if(!logfile)
        throw std::runtime_error("Failed to open logfile for writing");
    }
  }
  uint8_t marker[2]={0};
  for(;;) {
    marker[0] = marker[1];
    int res = readn2(fd, marker+1, 1);

    if(res < 0)
      throw EofException();
    
    //    if (doDEBUG) { cerr<<humanTimeNow()<<" marker now: "<< (int)marker[0]<<" " <<(int)marker[1]<<endl; }
    if(marker[0]==0xb5 && marker[1]==0x62) { // bingo
      struct timeval tv;
      gettimeofday(&tv, 0);
      basic_string<uint8_t> msg;
      msg.append(marker, 2);  // 0,1
      uint8_t b[4];
      readn2(fd, b, 4);
      msg.append(b, 4); // class, type, len1, len2

      uint16_t len = b[2] + 256*b[3];
      //      if (doDEBUG) { cerr<<humanTimeNow()<<" Got class "<<(int)msg[2]<<" type "<<(int)msg[3]<<", len = "<<len<<endl; }
      uint8_t buffer[len+2];
      res=readn2(fd, buffer, len+2);

      msg.append(buffer, len+2); // checksum
      if (doLOGFILE) {
        if(!g_fromFile)
          writen2(logfile, msg.c_str(), msg.size());      
      }
      return make_pair(UBXMessage(msg), tv);
    }
  }                                                                       
}

UBXMessage waitForUBX(int fd, int seconds, uint8_t ubxClass, uint8_t ubxType)
{
  time_t now =time(0);
  for(int n=0; n < 20; ++n) {
    if(time(0) - now > seconds)
      break;
    auto [msg, tv] = getUBXMessage(fd);
    (void) tv;

    if(msg.getClass() == ubxClass && msg.getType() == ubxType) {
      return msg;
    }
    else
      if (doDEBUG) { cerr<<humanTimeNow()<<" Got: "<<(int)msg.getClass() << " " <<(int)msg.getType() <<" while waiting for "<<(int)ubxClass<<" " <<(int)ubxType<<endl; }
  }
  throw std::runtime_error("Did not get response on time");
}

UBXMessage sendAndWaitForUBX(int fd, int seconds, basic_string_view<uint8_t> msg, uint8_t ubxClass, uint8_t ubxType)
{
  for(int n=3; n; --n) {
    writen2(fd, &msg[0], msg.size());
    try {
      return waitForUBX(fd, seconds, ubxClass, ubxType);
    }
    catch(...) {
      if(n==1)
        throw;
      cerr<<"Retransmit"<<endl;
          
    }
  }
}


struct TimeoutError{};

// perhaps do retransmit from here?
bool waitForUBXAckNack(int fd, int seconds, int ubxClass, int ubxType)
{
  time_t start = time(0);
  for(int n=0; n < 100; ++n) {
    auto [msg, tv] = getUBXMessage(fd);
    (void)tv;
    if(time(0) - start > seconds) {
      throw TimeoutError();
    }

    if(msg.getClass() != 5 || !(msg.getType() == 0 || msg.getType() == 1)) {
      if (doDEBUG) { cerr<<humanTimeNow()<<" Got: "<<(int)msg.getClass() << " " <<(int)msg.getType() <<" while waiting for ack/nack of " << ubxClass<<" "<<ubxType<<endl; }
      continue;
    }
      
    const auto& payload = msg.getPayload();
    if(payload.size() != 2) {
      if (doDEBUG) { cerr<<"Wrong payload size for ack/nack: "<<payload.size()<<endl; }
      continue;
    }
    if (doDEBUG) { cerr<<humanTimeNow()<<" Got an " << (msg.getType() ? "ack" : "nack")<<" for "<<(int)payload[0] <<" " << (int)payload[1]<<" while waiting for "<<ubxClass<<" " <<ubxType<<endl; }
    
    if(msg.getClass() == 0x05 && msg.getType() == 0x01 && payload[0]==ubxClass && payload[1]==ubxType) {
      return true;
    }
    else if(msg.getClass() == 0x05 && msg.getType() == 0x00 && payload[0]==ubxClass && payload[1]==ubxType) {
      return false;
    }
  }
  throw std::runtime_error("Did not get ACK/NACK response for class "+to_string(ubxClass)+" type "+to_string(ubxType)+" on time");
}

bool sendAndWaitForUBXAckNack(int fd, int seconds, basic_string_view<uint8_t> msg, uint8_t ubxClass, uint8_t ubxType)
{
  for(int n=3; n; --n) {
    writen2(fd, &msg[0], msg.size());
    try {
      return waitForUBXAckNack(fd, seconds, ubxClass, ubxType);
    }
    catch(...) {
      if(n==1)
        throw;
      cerr<<"Retransmit"<<endl;
    }
  }
  return false;
}


class NMMSender
{
  struct Destination
  {
    int fd{-1};
    ComboAddress dst{"0.0.0.0:0"};
    std::string fname;

    deque<string> queue;
    std::mutex mut;
    void emitNMM(const NavMonMessage& nmm);
  };
  
public:
  void addDestination(int fd)
  {
    auto d = std::make_unique<Destination>();
    d->fd = fd;
    d_dests.push_back(std::move(d));
  }
  void addDestination(const ComboAddress& dest)
  {
    auto d = std::make_unique<Destination>();
    d->dst = dest;
    d_dests.push_back(std::move(d));
  }

  void launch()
  {
    for(auto& d : d_dests) {
      if(d->dst.sin4.sin_port != 0) {
        thread t(&NMMSender::sendTCPThread, this, d.get());
        t.detach();
      }
    }
  }
  
  void sendTCPThread(Destination* d)
  {
    for(;;) {
      try {
        Socket s(d->dst.sin4.sin_family, SOCK_STREAM);
        SocketCommunicator sc(s);
        sc.setTimeout(3);
        sc.connect(d->dst);
        if (doDEBUG) { cerr<<humanTimeNow()<<" Connected to "<<d->dst.toStringWithPort()<<endl; }
        for(;;) {
          std::string msg;
          {
            std::lock_guard<std::mutex> mut(d->mut);
            if(!d->queue.empty()) {
              msg = d->queue.front();
            }
          }
          if(!msg.empty()) {
            sc.writen(msg);
            std::lock_guard<std::mutex> mut(d->mut);
            d->queue.pop_front();
          }
          else usleep(100000);
        }
      }
      catch(std::exception& e) {
        if (doDEBUG) { cerr<<humanTimeNow()<<" Sending thread for "<<d->dst.toStringWithPort()<<" had error: "<<e.what()<<endl; }
        {
          std::lock_guard<std::mutex> mut(d->mut);
          if (doDEBUG) { cerr<<humanTimeNow()<<" There are now "<<d->queue.size()<<" messages queued for "<<d->dst.toStringWithPort()<<endl; }
        }
        sleep(1);
      }
      catch(...) {
        if (doDEBUG) { cerr<<humanTimeNow()<<" Sending thread for "<<d->dst.toStringWithPort()<<" had error"; }
        {
          std::lock_guard<std::mutex> mut(d->mut);
          if (doDEBUG) { cerr<<"There are now "<<d->queue.size()<<" messages queued for "<<d->dst.toStringWithPort()<<endl; }
        }
        sleep(1);
      }
    }
    
  }
  
  void emitNMM(const NavMonMessage& nmm);

private:

  vector<unique_ptr<Destination>> d_dests;
};

void NMMSender::emitNMM(const NavMonMessage& nmm)
{
  for(auto& d : d_dests) {
    d->emitNMM(nmm);
  }
}

void NMMSender::Destination::emitNMM(const NavMonMessage& nmm)
{
  string out;
  nmm.SerializeToString(& out);
  string msg("bert");
  
  uint16_t len = htons(out.size());
  msg.append((char*)&len, 2);
  msg.append(out);

  if(dst.sin4.sin_port) {
    std::lock_guard<std::mutex> l(mut);
    queue.push_back(msg);
  }
  else
    writen2(fd, msg.c_str(), msg.size());
}


void enableUBXMessageOnPort(int fd, uint8_t ubxClass, uint8_t ubxType, uint8_t port, uint8_t rate=1)
{
  for(int n=0 ; n < 5; ++n) {
    try {
      basic_string<uint8_t> payload({ubxClass, ubxType, 0, 0, 0, 0, 0, 0});
      if(port > 6)
        throw std::runtime_error("Port number out of range (>6)");
     payload[2+ port]=rate;

     auto msg = buildUbxMessage(0x06, 0x01, payload);
      if(sendAndWaitForUBXAckNack(fd, 2, msg, 0x06, 0x01))
        return;
      else
        throw std::runtime_error("Got NACK enabling UBX message "+to_string((int)ubxClass)+" "+to_string((int)ubxType));
    }
    catch(TimeoutError& te) {
      if (doDEBUG) { cerr<<humanTimeNow()<<" Had "<<n<<"th timeout in enableUBXMessageOnPort"<<endl; }
      continue;
    }
  }
  throw TimeoutError();
}

bool isPresent(string_view fname)
{
  struct stat sb;
  if(stat(&fname[0], &sb) < 0)
    return false;
  return true;
}

bool isCharDevice(string_view fname)
{
  struct stat sb;
  if(stat(&fname[0], &sb) < 0)
    return false;
  return (sb.st_mode & S_IFMT) == S_IFCHR;
}


void readSome(int fd)
{
  for(int n=0; n < 5; ++n) {
    auto [msg, timestamp] = getUBXMessage(fd);
    (void)timestamp;
    if (doDEBUG) { cerr<<humanTimeNow()<<" Read some init: "<<(int)msg.getClass() << " " <<(int)msg.getType() <<endl; }
    if(msg.getClass() == 0x4)
      if (doDEBUG) { cerr<<humanTimeNow()<<" "<<string((char*)msg.getPayload().c_str(), msg.getPayload().size()) <<endl; }
  }
}

struct termios g_oldtio;

int initFD(const char* fname, bool doRTSCTS)
{
  int fd;
  if (doDEBUG) { cerr<<humanTimeNow()<<" initFD()"<<endl; }
  if (!isPresent(fname)) {
    if (doDEBUG) { cerr<<humanTimeNow()<<" initFD - "<<fname<<" - not present"<<endl; }
    throw runtime_error("Opening file "+string(fname));
  }
  if(string(fname) != "stdin" && string(fname) != "/dev/stdin" && isCharDevice(fname)) {
    if (doDEBUG) { cerr<<humanTimeNow()<<" initFD - open("<<fname<<")"<<endl; }
    fd = open(fname, O_RDWR | O_NOCTTY );
    if (fd <0 ) {
      throw runtime_error("Opening file "+string(fname));
    }
    if (doDEBUG) { cerr<<humanTimeNow()<<" initFD - open successful"<<endl; }

    struct termios newtio;
    if(tcgetattr(fd, &g_oldtio)) { /* save current port settings */
      perror("tcgetattr");
      exit(-1);
    }

    bzero(&newtio, sizeof(newtio));                                         
    newtio.c_cflag = CS8 | CLOCAL | CREAD;             
    if (doRTSCTS) {
      if(doDEBUG) { cerr<<humanTimeNow()<<" will enable RTSCTS"<<endl; }
      newtio.c_cflag |= CRTSCTS;
    }
    newtio.c_iflag = IGNPAR;                                                
    newtio.c_oflag = 0;                                                     
    
    /* set input mode (non-canonical, no echo,...) */                       
    newtio.c_lflag = 0;                                                     
    
    newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */         
    newtio.c_cc[VMIN]     = 5;   /* blocking read until 5 chars received */ 
    
    cfsetspeed(&newtio, g_baudval);
    if(tcsetattr(fd, TCSANOW, &newtio)) {
      perror("tcsetattr");
      exit(-1);
    }
    if (doDEBUG) { cerr<<humanTimeNow()<<" initFD - tty set"<<endl; }
  }
  else {
    g_fromFile = true;
    
    if (doDEBUG) { cerr<<humanTimeNow()<<" initFD - open("<<fname<<") - from file"<<endl; }
    fd = open(fname, O_RDONLY );
    if(fd < 0)
      throw runtime_error("Opening file "+string(fname));
  }
  return fd;

}

// ubxtool device srcid
int main(int argc, char** argv)
{
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  CLI::App app("ubxtool");
    

  bool doGPS{true}, doGalileo{true}, doGlonass{false}, doBeidou{true}, doReset{false}, doWait{true}, doRTSCTS{true}, doSBAS{false};
  bool doKeepNMEA{false};
  bool doSTDOUT=false;
#ifdef OpenBSD
  doRTSCTS = false;
#endif

  vector<string> destinations;
  string portName;
  int ubxport=3;
  int baudrate=115200;

  app.add_option("--destination,-d", destinations, "Send output to this IPv4/v6 address");
  app.add_flag("--wait", doWait, "Wait a bit, do not try to read init messages");
  app.add_flag("--reset", doReset, "Reset UBX device");
  app.add_flag("--beidou,-c", doBeidou, "Enable BeiDou reception");
  app.add_flag("--gps,-g", doGPS, "Enable GPS reception");
  app.add_flag("--glonass,-r", doGlonass, "Enable Glonass reception");
  app.add_flag("--galileo,-e", doGalileo, "Enable Galileo reception");
  app.add_flag("--sbas,-s", doSBAS, "Enable SBAS (EGNOS/WAAS/etc) reception");
  app.add_option("--rtscts", doRTSCTS, "Set hardware handshaking");
  app.add_flag("--stdout", doSTDOUT, "Emit output to stdout");
  app.add_option("--port,-p", portName, "Device or file to read serial from")->required();
  app.add_option("--station", g_srcid, "Station id");
  app.add_option("--ubxport,-u", ubxport, "UBX port to enable messages on (usb=3)");
  app.add_option("--baud,-b", baudrate, "Baudrate for serial connection");
  app.add_flag("--keep-nmea,-k", doKeepNMEA, "Don't disable NMEA");
  
  app.add_flag("--debug", doDEBUG, "Display debug information");  
  app.add_flag("--logfile", doLOGFILE, "Create logfile");  
  try {
    app.parse(argc, argv);
  } catch(const CLI::Error &e) {
    return app.exit(e);
  }

  g_baudval = getBaudrate(baudrate);

  if(!(doGPS || doGalileo || doGlonass || doBeidou)) {
    cerr<<"Enable at least one of --gps, --galileo, --glonass, --beidou"<<endl;
    return EXIT_FAILURE;
  }
  
  int fd = initFD(portName.c_str(), doRTSCTS);
  
  bool version9 = false;  
  if(!g_fromFile) {
    bool doInit = true;
    if(doInit) {
      if(doWait)
        sleep(2);
      else
        readSome(fd);
      
      std::basic_string<uint8_t> msg;
      if(doReset) {
        if (doDEBUG) { cerr<<humanTimeNow()<<" Sending a soft reset"<<endl; }
        msg = buildUbxMessage(0x06, 0x04, {0x00, 0x00, 0x01, 0x00}); // soft reset
        writen2(fd, msg.c_str(), msg.size());
        usleep(100000);
        close(fd);
        for(int n=0 ; n< 20; ++n) {
          if (doDEBUG) { cerr<<humanTimeNow()<<" Waiting for device to come back"<<endl; }
          try {
            fd = initFD(portName.c_str(), doRTSCTS);
            readSome(fd);          
          }
          catch(...)
            {
              if (doDEBUG) { cerr<<humanTimeNow()<<" Not yet back"<<endl; }
              usleep(400000);
              continue;
            }
          break;
        }
      }


      msg = buildUbxMessage(0x0a, 0x04, {});

      if (doDEBUG) { cerr<<humanTimeNow()<<" Sending version query"<<endl; }
      UBXMessage um1=sendAndWaitForUBX(fd, 1, msg, 0x0a, 0x04); // ask for version
      cerr<<humanTimeNow()<<" swVersion: "<<um1.getPayload().c_str()<<endl;
      cerr<<humanTimeNow()<<" hwVersion: "<<um1.getPayload().c_str()+30<<endl;
      
      for(unsigned int n=0; 40+30*n < um1.getPayload().size(); ++n) {
        cerr<<humanTimeNow()<<" Extended info: "<<um1.getPayload().c_str() + 40 +30*n<<endl;
        if(um1.getPayload().find((const uint8_t*)"F9") != string::npos)
          version9=true;
      }


      if(version9)
        cerr<<humanTimeNow()<<" Detected version U-Blox 9"<<endl;
      usleep(50000);
      if (doDEBUG) { cerr<<humanTimeNow()<<" Sending GNSS query"<<endl; }
      msg = buildUbxMessage(0x06, 0x3e, {});

      um1=sendAndWaitForUBX(fd, 1, msg, 0x06, 0x3e); // query GNSS
      auto payload = um1.getPayload();
      if (doDEBUG) {
        cerr<<humanTimeNow()<<" GNSS status, got " << (int)payload[3]<<" rows:"<<endl;
        for(uint8_t n = 0 ; n < payload[3]; ++n) {
          cerr<<humanTimeNow()<<" GNSSID "<<(int)payload[4+8*n]<<" enabled "<<(int)payload[8+8*n]<<" minTrk "<< (int)payload[5+8*n] <<" maxTrk "<<(int)payload[6+8*n]<<" " << (int)payload[8+8*n]<<" " << (int)payload[9+8*n] << " " <<" " << (int)payload[10+8*n]<<" " << (int)payload[11+8*n]<<endl;
        }
      }

      try {
      if(waitForUBXAckNack(fd, 2, 0x06, 0x3e)) {
        if (doDEBUG) { cerr<<humanTimeNow()<<" Got ACK for our poll of GNSS settings"<<endl; }
      }
      }catch(...) {
        cerr<<"Got timeout waiting for ack of poll, no problem"<<endl;
      }
      if(!version9) {
        //                                  ver   RO   maxch cfgs
        msg = buildUbxMessage(0x06, 0x3e, {0x00, 0x00, 0xff, 0x06,
              //                            GPS   min  max   res   x1         x2    x3,   x4
              0x00, 0x04, 0x08, 0,  doGPS,    0x00, 0x01, 0x00,
              //                            SBAS  min  max   rex   x1       x2    x3    x4
              0x01, 0x03, 0x04, 0,   doSBAS,  0x00, 0x01, 0x00,
              //                            BEI   min  max   res   x1       x2    x3,   x4
              0x03, 0x04, 0x08, 0,  doBeidou, 0x00, 0x01, 0x00,
              //                            ???   min  max   res   x1   x2    x3,   x4
              0x05, 0x04, 0x08, 0,  0, 0x00, 0x01, 0x00,
            
              //                            GAL   min  max   res   x1   x2    x3,   x4
              0x02, 0x08, 0x0A, 0,  doGalileo, 0x00, 0x01, 0x00,
              //                            GLO   min  max   res   x1   x2    x3,   x4
              0x06, 0x06, 0x08, 0,  doGlonass, 0x00, 0x01, 0x00

              });
      
        if (doDEBUG) { cerr<<humanTimeNow()<<" Sending GNSS setting, GPS: "<<doGPS<<", Galileo: "<<doGalileo<<", BeiDou: "<<doBeidou<<", GLONASS: "<<doGlonass<<", SBAS: "<<doSBAS<<endl; }
      
        if(sendAndWaitForUBXAckNack(fd, 2, msg, 0x06, 0x3e)) { // GNSS setting
          if (doDEBUG) { cerr<<humanTimeNow()<<" Got ack on GNSS setting"<<endl; }
        }
        else {
          cerr<<humanTimeNow()<<" Got nack on GNSS setting"<<endl;
          exit(-1);
        }
      }
      else {

        msg = buildUbxMessage(0x06, 0x8a, {0x00, 0x01, 0x00, 0x00,
              0x1f,0x00,0x31,0x10, doGPS,
              0x01,0x00,0x31,0x10, doGPS,
              0x03,0x00,0x31,0x10, doGPS,

              0x21,0x00,0x31,0x10, doGalileo,
              0x07,0x00,0x31,0x10, doGalileo,
              0x0a,0x00,0x31,0x10, doGalileo,

              0x22,0x00,0x31,0x10, doBeidou,
              0x0d,0x00,0x31,0x10, doBeidou,
              0x0e,0x00,0x31,0x10, doBeidou,

              0x25,0x00,0x31,0x10, doGlonass,
              0x18,0x00,0x31,0x10, doGlonass,
              0x1a,0x00,0x31,0x10, doGlonass
              


              });

        
        if (doDEBUG) { cerr<<humanTimeNow()<<" Sending F9P GNSS setting, GPS: "<<doGPS<<", Galileo: "<<doGalileo<<", BeiDou: "<<doBeidou<<", GLONASS: "<<doGlonass<<", SBAS: "<<doSBAS<<endl; }
        
        if(sendAndWaitForUBXAckNack(fd, 2, msg, 0x06, 0x8a)) { // GNSS setting, F9 stylee
          if (doDEBUG) { cerr<<humanTimeNow()<<" Got ack on F9P GNSS setting"<<endl; }
        }
        else {
          cerr<<humanTimeNow()<<" Got nack on F9P GNSS setting"<<endl;
          exit(-1);
        }
      }

      if(doSBAS) {
        //                                 "on" "*.*"  ign   
        msg = buildUbxMessage(0x06, 0x16, {0x01, 0x07, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}); // enable SBAS

        if(sendAndWaitForUBXAckNack(fd, 10, msg, 0x06, 0x16)) { // enable SBAS
          if (doDEBUG) { cerr<<humanTimeNow()<<" Got ack on SBAS setting"<<endl; }
        }
        else {
          if (doDEBUG) { cerr<<humanTimeNow()<<" Got nack on SBAS setting"<<endl; }
          exit(-1);
        }
      }
       
      if(!doKeepNMEA) {
        if (doDEBUG) { cerr<<humanTimeNow()<<" Disabling NMEA"<<endl; }
        
        msg = buildUbxMessage(0x06, 0x00, {(unsigned char)(ubxport),0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x01,0x00,0x00,0x00,0x00,0x00});
        if(sendAndWaitForUBXAckNack(fd, 10, msg, 0x06, 0x00)) { // disable NMEA
          if (doDEBUG) { cerr<<humanTimeNow()<<" NMEA disabled"<<endl; }
        }
        else {
          if (doDEBUG) { cerr<<humanTimeNow()<<" Got NACK disabling NMEA"<<endl; }
        }
      }
      if (doDEBUG) { cerr<<humanTimeNow()<<" Polling port settings"<<endl; } // UBX-CFG-PRT, 0x03 == USB
      msg = buildUbxMessage(0x06, 0x00, {(unsigned char)(ubxport)});
      
      UBXMessage um=sendAndWaitForUBX(fd, 4, msg, 0x06, 0x00); // UBX-CFG-PRT
      if (doDEBUG) {
        cerr<<humanTimeNow()<<" Protocol settings on port: "<<endl;
        for(const auto& c : um.getPayload())
          cerr<<(int)c<< " ";
        cerr<<endl;
      }
      try {
        if(waitForUBXAckNack(fd, 2, 0x06, 0x00)) {
          if (doDEBUG) { cerr<<humanTimeNow()<<" Got ACK for our poll of port protocol settings"<<endl; }
        }
      }catch(...) {
        cerr<<"Got timeout waiting for ack of port protocol poll, no problem"<<endl;
      }

      
      if (doDEBUG) { cerr<<humanTimeNow()<<" Enabling UBX-RXM-RLM"<<endl; } // SAR
      enableUBXMessageOnPort(fd, 0x02, 0x59, ubxport); // UBX-RXM-RLM
      
      if (doDEBUG) { cerr<<humanTimeNow()<<" Enabling UBX-RXM-RAWX"<<endl; } // RF doppler
      enableUBXMessageOnPort(fd, 0x02, 0x15, ubxport, 16); // RXM-RAWX
      
      if (doDEBUG) { cerr<<humanTimeNow()<<" Enabling UBX-RXM-SFRBX"<<endl; } // raw navigation frames
      enableUBXMessageOnPort(fd, 0x02, 0x13, ubxport); // SFRBX
      
      if (doDEBUG) { cerr<<humanTimeNow()<<" Enabling UBX-NAV-POSECEF"<<endl; } // position
      enableUBXMessageOnPort(fd, 0x01, 0x01, ubxport, 8); // POSECEF

      if(version9)  {
        if (doDEBUG) { cerr<<humanTimeNow()<<" Enabling UBX-NAV-SIG"<<endl; }  // satellite reception details
        enableUBXMessageOnPort(fd, 0x01, 0x43, ubxport, 8); // NAV-SIG

        if (doDEBUG) { cerr<<humanTimeNow()<<" Enabling UBX-RXM-MEASX"<<endl; }  // satellite reception details
        enableUBXMessageOnPort(fd, 0x02, 0x14, ubxport, 1); // RXM-MEASX

      }
      else {
        if (doDEBUG) { cerr<<humanTimeNow()<<" Enabling UBX-NAV-SAT"<<endl; }  // satellite reception details
        enableUBXMessageOnPort(fd, 0x01, 0x35, ubxport, 8); // NAV-SAT
      }
      
      if (doDEBUG) { cerr<<humanTimeNow()<<" Enabling UBX-NAV-PVT"<<endl; } // position, velocity, time fix
      enableUBXMessageOnPort(fd, 0x01, 0x07, ubxport, 1); // NAV-PVT we use this to get timing
    }
  }
  
  /* goal: isolate UBX messages, ignore everyting else.
     The challenge is that we might sometimes hit the 0xb5 0x62 marker
     in the middle of a message, which will cause us to possibly jump over valid messages 

     This program sits on the serial link and therefore has the best timestamps.
     At least Galileo messages sometimes rely on the timestamp-of-receipt, but the promise
     is that such messages are sent at whole second intervals.

     We therefore perform a layering violation and peer into the message to see
     what timestamp it claims to have, so that we can set subsequent timestamps correctly.
  */

  int curCycleTOW{-1}; // means invalid
  signal(SIGPIPE, SIG_IGN);
  NMMSender ns;
  for(const auto& s : destinations)
    ns.addDestination(ComboAddress(s, 29603));
  if(doSTDOUT)
    ns.addDestination(1);
  ns.launch();
  
  
  cerr<<humanTimeNow()<<" Entering main loop"<<endl;
  for(;;) {
    try {
      auto [msg, timestamp] = getUBXMessage(fd);
      (void)timestamp;
      auto payload = msg.getPayload();

      if(msg.getClass() == 0x01 && msg.getType() == 0x07) {  // UBX-NAV-PVT
        struct PVT
        {
          uint32_t itow;
          uint16_t year;
          uint8_t month; // jan = 1
          uint8_t day;
          uint8_t hour; // 24
          uint8_t min;
          uint8_t sec;
          uint8_t valid;
          uint32_t tAcc;
          int32_t nano;
          uint8_t fixtype;
          uint8_t flags;
          uint8_t flags2;
          uint8_t numsv;
        } __attribute__((packed));
        PVT pvt;
        
        memcpy(&pvt, &payload[0], sizeof(pvt));

        g_fixtype = pvt.fixtype;
        
        struct tm tm;
        memset(&tm, 0, sizeof(tm));
        tm.tm_year = pvt.year - 1900;
        tm.tm_mon = pvt.month - 1;
        tm.tm_mday = pvt.day;
        tm.tm_hour = pvt.hour;
        tm.tm_min = pvt.min;
        tm.tm_sec = pvt.sec;

        uint32_t satt = timegm(&tm);
        //        double satutc = timegm(&tm) + pvt.nano/1000000000.0; // negative is no problem here
        if(pvt.nano < 0) {
          pvt.sec--;
          satt--;
          pvt.nano += 1000000000;
        }
        if(!g_gnssutc.tv_sec) {

          if (doDEBUG) { cerr<<humanTimeNow()<<" Got initial timestamp: "<<humanTime(satt)<<endl; }
        }
        g_gnssutc.tv_sec = satt;
        g_gnssutc.tv_nsec = pvt.nano;
        continue;
      }
      if(!g_gnssutc.tv_sec) {

        if (doDEBUG) { cerr<<humanTimeNow()<<" Ignoring message with class "<<(int)msg.getClass()<< " and type "<< (int)msg.getType()<<": have not yet received a timestamp"<<endl; }
        continue;
      }
      
      
      if(msg.getClass() == 0x02 && msg.getType() == 0x15) {  // RAWX, the doppler stuff
        //        if (doDEBUG) { cerr<<humanTimeNow()<<" Got "<<(int)payload[11] <<" measurements "<<endl; }
        double rcvTow;
        memcpy(&rcvTow, &payload[0], 8);
        uint16_t rcvWn = payload[8] + 256*payload[9];
        for(int n=0 ; n < payload[11]; ++n) {
          double prMes;
          double cpMes;
          float doppler;
          
          memcpy(&prMes, &payload[16+32*n], 8);
          memcpy(&cpMes, &payload[24+32*n], 8);
          memcpy(&doppler, &payload[32+32*n], 4);
          int gnssid = payload[36+32*n];
          int sv = payload[37+32*n];
          int sigid=0;
          if(version9) {
            sigid = payload[38+32*n];
            if(gnssid == 2 && sigid ==6)  // they separate out I and Q, but the rest of UBX doesn't
              sigid = 5;                  // so map it back
            if(gnssid == 2 && sigid ==0)  // they separate out I and Q, but the rest of UBX doesn't
              sigid = 1;                  // so map it back
          }
          else if(gnssid==2) { // version 8 defaults galileo to E1B
            sigid = 1;
          }

          
          uint16_t locktimems;
          memcpy(&locktimems, &payload[40+32*n], 2);
          uint8_t prStddev = payload[43+23*n] & 0xf;
          uint8_t cpStddev = payload[44+23*n] & 0xf;
          uint8_t doStddev = payload[45+23*n] & 0xf;
          //          uint8_t trkStat = payload[46+23*n] & 0xf;

          NavMonMessage nmm;
          nmm.set_type(NavMonMessage::RFDataType);
          nmm.set_localutcseconds(g_gnssutc.tv_sec);
          nmm.set_localutcnanoseconds(g_gnssutc.tv_nsec);
          nmm.set_sourceid(g_srcid); 

          nmm.mutable_rfd()->set_gnssid(gnssid);
          nmm.mutable_rfd()->set_gnsssv(sv);
          nmm.mutable_rfd()->set_sigid(sigid);
          nmm.mutable_rfd()->set_rcvtow(rcvTow);
          nmm.mutable_rfd()->set_rcvwn(rcvWn);
          nmm.mutable_rfd()->set_doppler(doppler);
          nmm.mutable_rfd()->set_carrierphase(cpMes);
          nmm.mutable_rfd()->set_pseudorange(prMes);

          nmm.mutable_rfd()->set_prstd(ldexp(0.01, prStddev));
          nmm.mutable_rfd()->set_dostd(ldexp(0.002, doStddev));
          nmm.mutable_rfd()->set_cpstd(cpStddev*0.4);
          nmm.mutable_rfd()->set_locktimems(locktimems);
          ns.emitNMM( nmm);
        }
      }
      else if(msg.getClass() == 0x01 && msg.getType() == 0x01) {  // POSECF
        struct pos
        {
          uint32_t iTOW;
          int32_t ecefX;
          int32_t ecefY;
          int32_t ecefZ;
          uint32_t pAcc;
        };
        pos p;
        memcpy(&p, payload.c_str(), sizeof(pos));
        /*
        if (doDEBUG) {
          cerr<<humanTimeNow()<<" Position: ("<< p.ecefX / 100000.0<<", "
            << p.ecefY / 100000.0<<", "
            << p.ecefZ / 100000.0<<") +- "<<p.pAcc<<" cm"<<endl;
        }
        */

//        g_ourpos = {p.ecefX/100.0, p.ecefY/100.0, p.ecefZ/100.0};
        
        NavMonMessage nmm;
        nmm.set_type(NavMonMessage::ObserverPositionType);
        nmm.set_localutcseconds(g_gnssutc.tv_sec);
        nmm.set_localutcnanoseconds(g_gnssutc.tv_nsec);
        nmm.set_sourceid(g_srcid); 
        nmm.mutable_op()->set_x(p.ecefX /100.0);
        nmm.mutable_op()->set_y(p.ecefY /100.0);
        nmm.mutable_op()->set_z(p.ecefZ /100.0);
        nmm.mutable_op()->set_acc(p.pAcc /100.0);
        ns.emitNMM( nmm);
      }
      else if(msg.getClass() == 2 && msg.getType() == 0x13) {  // SFRBX
                  
        // order: 2, 4, 6, 7/9, 8/10, 0, 0, 0, 0, 0, 1, 3, 5, 0, 0
        //              *             *  *  *  *  *        * 
        // tow    
        
        try {
          pair<int,int> id = make_pair(payload[0], payload[1]);
          int sigid = payload[2];
          static set<tuple<int,int,int>> svseen;
          static time_t lastStat;
          svseen.insert({id.first, id.second, payload[2]});

          if(time(0)- lastStat > 30) {
            cerr<<humanTimeNow()<<" src "<<g_srcid<< " (fix: "<<g_fixtype<<") currently receiving: ";
            for(auto& s : svseen) {
              cerr<<get<0>(s)<<","<<get<1>(s)<<"@"<<get<2>(s)<<" ";
            }
            cerr<<endl;
            lastStat = time(0);
            svseen.clear();
          }
          
          if(id.first == 0 && !sigid) { // can only parse the old stuff
            NavMonMessage nmm;
            nmm.set_type(NavMonMessage::GPSInavType);
            nmm.set_localutcseconds(g_gnssutc.tv_sec);
            nmm.set_localutcnanoseconds(g_gnssutc.tv_nsec);
            nmm.set_sourceid(g_srcid);         
            //            if (doDEBUG) { cerr<<humanTimeNow()<<" GPS frame, numwords: "<<(int)payload[4]<<", version: "<<(int)payload[6]<<endl; }
            static int wn, tow;
            auto gpsframe = getGPSFromSFRBXMsg(payload);
            auto cond = getCondensedGPSMessage(gpsframe);
            auto frameno = getbitu(&cond[0], 24+19, 3);
            if(frameno == 1 && sigid==0)
              wn = 2048 + getbitu(&cond[0], 2*24, 10);
            if(!wn) 
              continue; // can't file this yet
              
            tow = 1.5*(getbitu(&cond[0], 24, 17)*4);
            nmm.mutable_gpsi()->set_gnsswn(wn);   // XXX this sucks
            nmm.mutable_gpsi()->set_sigid(sigid);
            nmm.mutable_gpsi()->set_gnsstow(tow); // "with 6 second increments" -- needs to be adjusted
            nmm.mutable_gpsi()->set_gnssid(id.first);
            nmm.mutable_gpsi()->set_gnsssv(id.second);
            nmm.mutable_gpsi()->set_contents(string((char*)gpsframe.c_str(), gpsframe.size()));
            ns.emitNMM( nmm);
            continue;
          }
          else if(id.first ==2) { // GALILEO
            auto inav = getInavFromSFRBXMsg(payload);
            unsigned int wtype = getbitu(&inav[0], 0, 6);

            uint32_t satTOW;
            int msgTOW{0};
            if(getTOWFromInav(inav, &satTOW, &g_galwn)) { // 0, 6, 5
              //            if (doDEBUG) { cerr<<humanTimeNow()<<"    "<<wtype<<" sv "<<id.second<<" tow "<<satTOW << " % 30 = "<< satTOW % 30<<", implied start of cycle: "<<(satTOW - (satTOW %30)) <<endl; }
              msgTOW = satTOW;
              curCycleTOW = satTOW - (satTOW %30);
            }
            else {
              if(curCycleTOW < 0) // did not yet have a start of cycle
                continue;
              //            if (doDEBUG) { cerr<<humanTimeNow()<<"    "<<wtype<<" sv "<<id.second<<" tow "; }
              if(sigid == 5) {
                if(wtype == 2) {
                  msgTOW = curCycleTOW + 20;
                }
                else if(wtype == 4) {
                  msgTOW = curCycleTOW + 22;
                } // next have '6' which sets TOW
                else if(wtype==7 || wtype == 9) {
                  msgTOW = curCycleTOW + 6;
                }
                else if(wtype==8 || wtype == 10) {
                  msgTOW = curCycleTOW + 8;
                }
                else if(wtype==1) {
                  msgTOW = curCycleTOW+30;
                }
                else if(wtype==3) {
                  msgTOW = curCycleTOW + 32; 
                }
                else { // dummy
                  if(id.second != 20) // known broken XXX
                    if (doDEBUG) { cerr<<humanTimeNow()<<" galileo E"<<id.second<<" what kind of wtype is this: "<<wtype<<endl; }
                  continue;
                }
              }
              else {
                if(wtype == 2) {
                  //              if (doDEBUG) { cerr<<humanTimeNow()<<" infered to be 1 "<<curCycleTOW + 31<<endl; }
                  msgTOW = curCycleTOW + 31;
                }
                else if(wtype == 4) {
                  //              if (doDEBUG) { cerr<<humanTimeNow()<<" infered to be 3 "<<curCycleTOW + 33<<endl; }
                  msgTOW = curCycleTOW + 33;
                } // next have '6' which sets TOW
                else if(wtype==7 || wtype == 9) {
                  msgTOW = curCycleTOW + 7;
                }
                else if(wtype==8 || wtype == 10) {
                  msgTOW = curCycleTOW + 9;
                }
                else if(wtype==1) {
                  msgTOW = curCycleTOW + 21;
                }
                else if(wtype==3) {
                  msgTOW = curCycleTOW + 23; 
                }
                else { // dummy
                  if(id.second != 20) // known broken XXX
                    if (doDEBUG) { cerr<<humanTimeNow()<<" galileo E"<<id.second<<" what kind of wtype is this: "<<wtype<<endl; }
                  continue;
                }

              }
            }
            NavMonMessage nmm;
            nmm.set_sourceid(g_srcid);
            nmm.set_type(NavMonMessage::GalileoInavType);
            nmm.set_localutcseconds(g_gnssutc.tv_sec);
            nmm.set_localutcnanoseconds(g_gnssutc.tv_nsec);
            
            nmm.mutable_gi()->set_gnsswn(g_galwn);
            
            nmm.mutable_gi()->set_gnsstow(msgTOW);
            nmm.mutable_gi()->set_gnssid(id.first);
            nmm.mutable_gi()->set_gnsssv(id.second);
            nmm.mutable_gi()->set_sigid(sigid);
            nmm.mutable_gi()->set_contents((const char*)&inav[0], inav.size());
            
            ns.emitNMM( nmm);
          }
          else if(id.first==3) {
            auto gstr = getGlonassFromSFRBXMsg(payload);
            auto cond = getCondensedBeidouMessage(gstr);
            static map<int, BeidouMessage> bms;
            auto& bm = bms[id.second];
            
            uint8_t pageno;
            bm.parse(cond, &pageno);
            
            if(bm.wn < 0) {
              if (doDEBUG) { cerr<<humanTimeNow()<<" BeiDou C"<<id.second<<" WN not yet known, not yet emitting message"<<endl; }
              continue;
            }
            NavMonMessage nmm;
            nmm.set_localutcseconds(g_gnssutc.tv_sec);
            nmm.set_localutcnanoseconds(g_gnssutc.tv_nsec);
            nmm.set_sourceid(g_srcid);
            if(id.second > 5) {
              // this **HARDCODES** that C01,02,03,04,05 emit D2 messages!            
              nmm.set_type(NavMonMessage::BeidouInavTypeD1);
              nmm.mutable_bid1()->set_gnsswn(bm.wn);  // only sent in word 1!!
              nmm.mutable_bid1()->set_gnsstow(bm.sow); 
              nmm.mutable_bid1()->set_gnssid(id.first);
              nmm.mutable_bid1()->set_gnsssv(id.second);
              nmm.mutable_bid1()->set_sigid(sigid);              
              nmm.mutable_bid1()->set_contents(string((char*)gstr.c_str(), gstr.size()));
              ns.emitNMM( nmm);
            }
            else {
              // not sending this: we can't even get the week number right!
              /*
              nmm.set_type(NavMonMessage::BeidouInavTypeD2);
              nmm.mutable_bid2()->set_gnsswn(bm.wn);  
              nmm.mutable_bid2()->set_gnsstow(bm.sow); 
              nmm.mutable_bid2()->set_gnssid(id.first);
              nmm.mutable_bid2()->set_gnsssv(id.second);
              nmm.mutable_bid2()->set_sigid(sigid);              
              nmm.mutable_bid2()->set_contents(string((char*)gstr.c_str(), gstr.size()));
              */
              
            }

            continue;
          }
          else if(id.first==6) {
            //            if (doDEBUG) { cerr<<humanTimeNow()<<" SFRBX from GLONASS "<<id.second<<" @ frequency "<<(int)payload[3]<<", msg of "<<(int)payload[4]<< " words"<<endl; }
            auto gstr = getGlonassFromSFRBXMsg(payload);
            /*
            static map<int, GlonassMessage> gms;
            GlonassMessage& gm = gms[id.second];
            int strno = gm.parse(gstr);
            */
            if(id.second != 255) {
              NavMonMessage nmm;
              nmm.set_localutcseconds(g_gnssutc.tv_sec);  
              nmm.set_localutcnanoseconds(g_gnssutc.tv_nsec);
              nmm.set_sourceid(g_srcid);
              nmm.set_type(NavMonMessage::GlonassInavType);
              nmm.mutable_gloi()->set_freq(payload[3]);
              nmm.mutable_gloi()->set_gnssid(id.first);
              nmm.mutable_gloi()->set_gnsssv(id.second);
              nmm.mutable_gloi()->set_sigid(sigid);              
              nmm.mutable_gloi()->set_contents(string((char*)gstr.c_str(), gstr.size()));
              
              ns.emitNMM( nmm);
            }
          }
          else if(id.first == 1) {// SBAS
            if (doDEBUG) { cerr<<humanTimeNow()<<" SBAS "<<id.second<<" frame, numwords: "<<(int)payload[4]<<", version: "<<(int)payload[6]<<", totsize "<<payload.size()<<endl; }

            auto sbas = getSBASFromSFRBXMsg(payload);
            cerr<<"Preamble: "<<(int)sbas[0]<<", type "<<getbitu(&sbas[0], 8, 6)<<endl;

          }
          else
            ; //            if (doDEBUG) { cerr<<humanTimeNow()<<" SFRBX from unsupported GNSSID/sigid combination "<<id.first<<", sv "<<id.second<<", sigid "<<sigid<<", "<<payload.size()<<" bytes"<<endl; }
       
        }
        catch(CRCMismatch& cm) {
          if (doDEBUG) { cerr<<humanTimeNow()<<" Had CRC mismatch!"<<endl; }
        }
      }
      else if(msg.getClass() == 1 && msg.getType() == 0x35) { // UBX-NAV-SAT
        //        if(version9) // we have UBX-NAV-SIG
        //          continue;
        //        if (doDEBUG) { cerr<<humanTimeNow()<<" Info for "<<(int) payload[5]<<" svs: \n"; }
        for(unsigned int n = 0 ; n < payload[5]; ++n) {
          int gnssid = payload[8+12*n];
          int sv = payload[9+12*n];

          auto el = (int)(char)payload[11+12*n];
          auto azi = ((int)payload[13+12*n]*256 + payload[12+12*n]);
          auto db = (int)payload[10+12*n];
          //          if (doDEBUG) { cerr<<"gnssid "<<gnssid<<" sv "<<sv<<" el "<<el<<endl; }
          NavMonMessage nmm;
          nmm.set_sourceid(g_srcid);
          nmm.set_localutcseconds(g_gnssutc.tv_sec);
          nmm.set_localutcnanoseconds(g_gnssutc.tv_nsec);
          
          nmm.set_type(NavMonMessage::ReceptionDataType);
          nmm.mutable_rd()->set_gnssid(gnssid);
          nmm.mutable_rd()->set_gnsssv(sv);
          nmm.mutable_rd()->set_db(db);
          nmm.mutable_rd()->set_el(el);
          nmm.mutable_rd()->set_azi(azi);
          nmm.mutable_rd()->set_prres(*((int16_t*)(payload.c_str()+ 14 +12*n)) *0.1);
          /*
          uint32_t status;
          memcpy(&status, &payload[16+12*n], 4);
          if (doDEBUG) {
            cerr<<humanTimeNow()<<" "<<gnssid<<","<<sv<<":";
            if(status & (1<<3))
              cerr<<" used";
            cerr<< " qualityind "<<(status & 7);
            cerr<<" db "<<db<<" el "<< el;
            cerr<<" health " << (status & (1<<5));
            cerr<<" sbasCorr " << (status & (1<<16));
            cerr<<" prRes "<<nmm.rd().prres();
            cerr<<" orbsrc " << ((status >> 8)&7);
            cerr<<" eph-avail " << !!(status & (1<<11));
            cerr<<" alm-avail " << !!(status & (1<<12)); 
            cerr<<endl;
          }
          */
          ns.emitNMM( nmm);
        }
      }
      else if(msg.getClass() == 1 && msg.getType() == 0x43) { // UBX-NAV-SIG
        for(unsigned int n = 0 ; n < payload[5]; ++n) {
          int gnssid = payload[8+16*n];
          int sv = payload[9+16*n];
          int sigid = 0;

          if(version9) {
            sigid = payload[10+16*n];
            if(gnssid == 2 && sigid ==6)  // they separate out I and Q, but the rest of UBX doesn't
              sigid = 5;                  // so map it back
            if(gnssid == 2 && sigid ==0)  // they separate out I and Q, but the rest of UBX doesn't
              sigid = 1;                  // so map it back
          }
          else if(gnssid==2) { // version 8 defaults galileo to E1B
            sigid = 1;
          }

          auto db = (int)payload[14+16*n];
          //          if (doDEBUG) { cerr<<"gnssid "<<gnssid<<" sv "<<sv<<" el "<<el<<endl; }
          NavMonMessage nmm;
          nmm.set_sourceid(g_srcid);
          nmm.set_localutcseconds(g_gnssutc.tv_sec);
          nmm.set_localutcnanoseconds(g_gnssutc.tv_nsec);
          
          nmm.set_type(NavMonMessage::ReceptionDataType);
          nmm.mutable_rd()->set_gnssid(gnssid);
          nmm.mutable_rd()->set_gnsssv(sv);
          nmm.mutable_rd()->set_db(db);
          nmm.mutable_rd()->set_prres(*((int16_t*)(payload.c_str()+ 12 +16*n)) *0.1); // ENDIANISM
          nmm.mutable_rd()->set_sigid(sigid);
          nmm.mutable_rd()->set_el(0);
          nmm.mutable_rd()->set_azi(0);
          
          ns.emitNMM( nmm);
        }
      }
      else if(msg.getClass() == 1 && msg.getType() == 0x30) { // UBX-NAV-SVINFO
        
      }
      else if(msg.getClass() == 0x02 && msg.getType() == 0x14) { // UBX-RXM-MEASX

        //        if (doDEBUG) { cerr<<humanTimeNow()<<" Got RXM-MEASX for "<<(int)payload[34]<<" satellites, r0 "<< (int)payload[30]<<" r1 " <<(int)payload[31]<<endl; }
        for(unsigned int n = 0 ; n < payload[34] ; ++n) {
          uint16_t wholeChips;
          uint16_t fracChips;
          //          uint8_t intCodePhase = payload[64+24*n];
          uint32_t codePhase;

          memcpy(&wholeChips, &payload[56+24*n], 2);
          memcpy(&fracChips, &payload[58+24*n], 2);
          memcpy(&codePhase, &payload[60+24*n], 4);
          
          //          if (doDEBUG) { cerr<<humanTimeNow()<<" "<<(int)payload[44+24*n]<<","<<(int)payload[45+24*n]<<" whole-chips "<<wholeChips<<" frac-chips "<<fracChips<<" int-code-phase " <<(int)intCodePhase <<" frac-code-phase "<<ldexp(codePhase, -21) << " mpath " << (int)payload[47+24*n] << " r1 " << (int)payload[66+24*n] << " r2 " <<(int)payload[67+24*n]<<endl; }
        }
      }
      else if(msg.getClass() == 0x02 && msg.getType() == 0x59) { // UBX-RXM-RLM
        int type = (int)payload[1];      
        int sv = (int)payload[2];

        NavMonMessage nmm;
        nmm.set_sourceid(g_srcid);
        nmm.set_localutcseconds(g_gnssutc.tv_sec);
        nmm.set_localutcnanoseconds(g_gnssutc.tv_nsec);
        
        nmm.set_type(NavMonMessage::SARResponseType);
        nmm.mutable_sr()->set_gnssid(2); // Galileo only for now
        nmm.mutable_sr()->set_gnsssv(sv);
        nmm.mutable_sr()->set_sigid(0); // we should fill this in later
        nmm.mutable_sr()->set_type(payload[1]);
        nmm.mutable_sr()->set_identifier(string((char*)payload.c_str()+4, 8));
        nmm.mutable_sr()->set_code(payload[12]);
        nmm.mutable_sr()->set_params(string((char*)payload.c_str()+13, payload.size()-14));
        
        string hexstring;
        for(int n = 0; n < 15; ++n)
          hexstring+=fmt::sprintf("%x", (int)getbitu(payload.c_str(), 36 + 4*n, 4));

        if (doDEBUG) { cerr<<humanTimeNow()<<" "<<humanTime(g_gnssutc.tv_sec)<<" SAR RLM type "<<type<<" from gal sv " << sv << " beacon "<<hexstring <<" code "<<(int)payload[12]<<" params "<<payload[12] + 256*payload[13]<<endl; }

      //      wk.emitLine(sv, "SAR "+hexstring);
      //      cout<<"SAR: sv = "<< (int)msg[2] <<" ";
      //      for(int n=4; n < 12; ++n)
      //        fmt::printf("%02x", (int)msg[n]);

      //      for(int n = 0; n < 15; ++n)
      //        fmt::printf("%x", (int)getbitu(msg.c_str(), 36 + 4*n, 4));
      
      //      cout << " Type: "<< (int) msg[12] <<"\n";
      //      cout<<"Parameter: (len = "<<msg.length()<<") ";
      //      for(unsigned int n = 13; n < msg.length(); ++n)
      //        fmt::printf("%02x ", (int)msg[n]);
      //      cout<<"\n";

      }

      else 
        if (doDEBUG) { cerr<<humanTimeNow()<<" Uknown UBX message of class "<<(int) msg.getClass() <<" and type "<< (int) msg.getType()<<endl; }

      //      writen2(1, payload.d_raw.c_str(),msg.d_raw.size());
    }
    catch(UBXMessage::BadChecksum &e) {
      if (doDEBUG) { cerr<<humanTimeNow()<<" Bad UBX checksum, skipping message"<<endl; }
    }
  }
  if(!g_fromFile)
    tcsetattr(fd, TCSANOW, &g_oldtio);                                          
}                                                                         

