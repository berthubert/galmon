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
struct timespec g_gstutc;
uint16_t g_wn;
using namespace std;

uint16_t g_srcid{2};

#define BAUDRATE B115200

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

t   n   w
0   1:  2                   wn % 30 == 0           
2   2:  4                   wn % 30 == 2           
4   3:  6          WN/TOW              4           -> set startTow, startTowFresh
6   4:  7/9                                        
8   5:  8/10                                       
10  6:  0             TOW                          
12  7:  0          WN/TOW
14  8:  0          WN/TOW
16  9:  0          WN/TOW
18  10: 0          WN/TOW
20  11: 1                                          
22  12: 3
24  13: 5          WN/TOW
26  14: 0          WN/TOW
28  15: 0          WN/TOW
*/

/*
    if(ubxClass == 2 && ubxType == 89) { // SAR
      string hexstring;
      for(int n = 0; n < 15; ++n)
        hexstring+=fmt::format("%x", (int)getbitu(msg.c_str(), 36 + 4*n, 4));
      
      //      int sv = (int)msg[2];
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
  static int logfile;
  if(!logfile && !g_fromFile) {
    logfile = open("./logfile", O_WRONLY|O_CREAT|O_APPEND|O_LARGEFILE, 0600);
    if(!logfile)
      throw std::runtime_error("Failed to open logfile for writing");
  }
  uint8_t marker[2]={0};
  for(;;) {
    marker[0] = marker[1];
    int res = readn2(fd, marker+1, 1);

    if(res < 0)
      throw EofException();
    
    //    cerr<<"marker now: "<< (int)marker[0]<<" " <<(int)marker[1]<<endl;
    if(marker[0]==0xb5 && marker[1]==0x62) { // bingo


      
      struct timeval tv;
      gettimeofday(&tv, 0);
      basic_string<uint8_t> msg;
      msg.append(marker, 2);  // 0,1
      uint8_t b[4];
      readn2(fd, b, 4);
      msg.append(b, 4); // class, type, len1, len2

      uint16_t len = b[2] + 256*b[3];
      //      cerr<<"Got class "<<(int)msg[2]<<" type "<<(int)msg[3]<<", len = "<<len<<endl;
      uint8_t buffer[len+2];
      res=readn2(fd, buffer, len+2);

      msg.append(buffer, len+2); // checksum
      if(!g_fromFile)
        writen2(logfile, msg.c_str(), msg.size());      
      return make_pair(UBXMessage(msg), tv);
    }
  }                                                                       
}

UBXMessage waitForUBX(int fd, int seconds, uint8_t ubxClass, uint8_t ubxType)
{
  for(int n=0; n < seconds*20; ++n) {
    auto [msg, tv] = getUBXMessage(fd);
    (void) tv;

    if(msg.getClass() == ubxClass && msg.getType() == ubxType) {
      return msg;
    }
    else
      cerr<<"Got: "<<(int)msg.getClass() << " " <<(int)msg.getType() <<" while waiting for "<<(int)ubxClass<<" " <<(int)ubxType<<endl;
  }
  throw std::runtime_error("Did not get response on time");
}

struct TimeoutError{};

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
      cerr<<"Got: "<<(int)msg.getClass() << " " <<(int)msg.getType() <<" while waiting for ack/nack of " << ubxClass<<" "<<ubxType<<endl;
      continue;
    }
      
    const auto& payload = msg.getPayload();
    if(payload.size() != 2) {
      cerr << "Wrong payload size for ack/nack: "<<payload.size()<<endl;
      continue;
    }
    cerr<<"Got an " << (msg.getType() ? "ack" : "nack")<<" for "<<(int)payload[0] <<" " << (int)payload[1]<<" while waiting for "<<ubxClass<<" " <<ubxType<<endl;
    
    if(msg.getClass() == 0x05 && msg.getType() == 0x01 && payload[0]==ubxClass && payload[1]==ubxType) {
      return true;
    }
    else if(msg.getClass() == 0x05 && msg.getType() == 0x00 && payload[0]==ubxClass && payload[1]==ubxType) {
      return false;
    }
  }
  throw std::runtime_error("Did not get ACK/NACK response for class "+to_string(ubxClass)+" type "+to_string(ubxType)+" on time");
}

void emitNMM(int fd, const NavMonMessage& nmm)
{
            
  string out;
  nmm.SerializeToString(& out);
  string msg("bert");
  
  uint16_t len = htons(out.size());
  msg.append((char*)&len, 2);
  msg.append(out);
  
  writen2(fd, msg.c_str(), msg.size());
}

void enableUBXMessageUSB(int fd, uint8_t ubxClass, uint8_t ubxType, uint8_t rate=1)
{
  for(int n=0 ; n < 5; ++n) {
    try {
      auto msg = buildUbxMessage(0x06, 0x01, {ubxClass, ubxType, 0, 0, 0, rate, 0, 0});
      writen2(fd, msg.c_str(), msg.size());
      if(waitForUBXAckNack(fd, 2, 0x06, 0x01))
        return;
      else
        throw std::runtime_error("Got NACK enabling UBX message "+to_string((int)ubxClass)+" "+to_string((int)ubxType));
    }
    catch(TimeoutError& te) {
      cerr<<"Had "<<n<<"th timeout in enableUBXMessageUSB"<<endl;
      continue;
    }
  }
  throw TimeoutError();
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
    cerr<<"Read some init: "<<(int)msg.getClass() << " " <<(int)msg.getType() <<endl;
    if(msg.getClass() == 0x4)
      cerr<<string((char*)msg.getPayload().c_str(), msg.getPayload().size()) <<endl;
  }
}

struct termios g_oldtio;

int initFD(const char* fname, bool doRTSCTS)
{
  int fd;
  if(string(fname) != "stdin" && string(fname) != "/dev/stdin" && isCharDevice(fname)) {
    fd = open(fname, O_RDWR | O_NOCTTY );
    if (fd <0 ) {
      throw runtime_error("Opening file "+string(fname));
    }

    struct termios newtio;
    if(tcgetattr(fd, &g_oldtio)) { /* save current port settings */
      perror("tcgetattr");
      exit(-1);
    }

    bzero(&newtio, sizeof(newtio));                                         
    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;             
    if (doRTSCTS)
      newtio.c_cflag |= CRTSCTS;
    newtio.c_iflag = IGNPAR;                                                
    newtio.c_oflag = 0;                                                     
    
    /* set input mode (non-canonical, no echo,...) */                       
    newtio.c_lflag = 0;                                                     
    
    newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */         
    newtio.c_cc[VMIN]     = 5;   /* blocking read until 5 chars received */ 
    
    tcflush(fd, TCIFLUSH);                                                  
    if(tcsetattr(fd,TCSANOW, &newtio)) {
      perror("tcsetattr");
      exit(-1);
    }
  }
  else {
    g_fromFile = true;
    
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
    
  vector<std::string> serial;
  bool doGPS{true}, doGalileo{true}, doGlonass{false}, doBeidou{true}, doReset{false}, doWait{false}, doRTSCTS{true}, doSBAS{false};

#ifdef OpenBSD
  doRTSCTS = false;
#endif

  app.add_option("serial", serial, "Serial");
    
  app.add_flag("--wait", doWait, "Wait a bit, do not try to read init messages");
  app.add_flag("--reset", doReset, "Reset UBX device");
  app.add_flag("--beidou,-c", doBeidou, "Enable BeiDou reception");
  app.add_flag("--gps,-g", doGPS, "Enable GPS reception");
  app.add_flag("--glonass,-r", doGlonass, "Enable Glonass reception");
  app.add_flag("--galileo,-e", doGalileo, "Enable Galileo reception");
  app.add_flag("--sbas,-s", doSBAS, "Enable SBAS (EGNOS/WAAS/etc) reception");
  app.add_option("--rtscts", doRTSCTS, "Set hardware handshaking");

  
  try {
    app.parse(argc, argv);
  } catch(const CLI::Error &e) {
    return app.exit(e);
  }

  if(serial.size() != 2) {
    cout<<app.help()<<endl;
    return EXIT_FAILURE;
  }

  int fd = initFD(serial[0].c_str(), doRTSCTS);
  
  g_srcid = atoi(serial[1].c_str());
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
        cerr<<"Sending a soft reset"<<endl;
        msg = buildUbxMessage(0x06, 0x04, {0x00, 0x00, 0x01, 0x00});
        writen2(fd, msg.c_str(), msg.size());
        usleep(100000);
        close(fd);
        for(int n=0 ; n< 20; ++n) {
          cerr<<"Waiting for device to come back"<<endl;
          try {
            fd = initFD(serial[0].c_str(), doRTSCTS);
            readSome(fd);          
          }
          catch(...)
            {
              cerr<<"Not yet back"<<endl;
              usleep(400000);
              continue;
            }
          break;
        }
      }

      cerr<<"Sending version query"<<endl;
      msg = buildUbxMessage(0x0a, 0x04, {});
      writen2(fd, msg.c_str(), msg.size());      
      UBXMessage um1=waitForUBX(fd, 2, 0x0a, 0x04);
      cerr<<"swVersion: "<<um1.getPayload().c_str()<<endl;
      cerr<<"hwVersion: "<<um1.getPayload().c_str()+30<<endl;

      for(unsigned int n=0; 40+30*n < um1.getPayload().size(); ++n) {
        cerr<<"Extended info: "<<um1.getPayload().c_str() + 40 +30*n<<endl;
        if(um1.getPayload().find((const uint8_t*)"F9") != string::npos)
          version9=true;
      }

      if(version9)
        cerr<<"Detected version U-Blox 9"<<endl;
      
      cerr<<"Sending GNSS query"<<endl;
      msg = buildUbxMessage(0x06, 0x3e, {});
      writen2(fd, msg.c_str(), msg.size());
      um1=waitForUBX(fd, 2, 0x06, 0x3e);
      auto payload = um1.getPayload();
      cerr<<"GNSS status, got " << (int)payload[3]<<" rows:\n";
      for(uint8_t n = 0 ; n < payload[3]; ++n) {
        cerr<<"GNSSID "<<(int)payload[4+8*n]<<" enabled "<<(int)payload[8+8*n]<<" minTrk "<< (int)payload[5+8*n] <<" maxTrk "<<(int)payload[6+8*n]<<" " << (int)payload[8+8*n]<<" " << (int)payload[9+8*n] << " " <<" " << (int)payload[10+8*n]<<" " << (int)payload[11+8*n]<<endl;
      }

      if(waitForUBXAckNack(fd, 2, 0x06, 0x3e)) {
        cerr<<"Got ACK for our poll of GNSS settings"<<endl;
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
              0x02, 0x04, 0x08, 0,  doGalileo, 0x00, 0x01, 0x00,
              //                            GLO   min  max   res   x1   x2    x3,   x4
              0x06, 0x06, 0x08, 0,  doGlonass, 0x00, 0x01, 0x00

              });
      
        cerr<<"Sending GNSS setting, GPS: "<<doGPS<<", Galileo: "<<doGalileo<<", BeiDou: "<<doBeidou<<", GLONASS: "<<doGlonass<<", SBAS: "<<doSBAS<<endl;
        writen2(fd, msg.c_str(), msg.size());
      
        if(waitForUBXAckNack(fd, 2, 0x06, 0x3e))
          cerr<<"Got ack on GNSS setting"<<endl;
        else {
          cerr<<"Got nack on GNSS setting"<<endl;
          exit(-1);
        }
      }

      if(doSBAS) {
        //                                 "on" "*.*"  ign   
        msg = buildUbxMessage(0x06, 0x16, {0x01, 0x07, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00});
        writen2(fd, msg.c_str(), msg.size());
        
        if(waitForUBXAckNack(fd, 2, 0x06, 0x16))
          cerr<<"Got ack on SBAS setting"<<endl;
        else {
          cerr<<"Got nack on SBAS setting"<<endl;
          exit(-1);
        }
      }
       


      
      
      cerr<<"Disabling NMEA"<<endl;
      msg = buildUbxMessage(0x06, 0x00, {0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x01,0x00,0x00,0x00,0x00,0x00});
      writen2(fd, msg.c_str(), msg.size());
      if(waitForUBXAckNack(fd, 10, 0x06, 0x00))
        cerr<<"NMEA disabled"<<endl;
      else
        cerr<<"Got NACK disabling NMEA"<<endl;
      
      cerr<<"Polling USB settings"<<endl; // UBX-CFG-PRT, 0x03 == USB
      msg = buildUbxMessage(0x06, 0x00, {0x03});
      writen2(fd, msg.c_str(), msg.size());
      
      UBXMessage um=waitForUBX(fd, 4, 0x06, 0x00); // UBX-CFG-PRT
      cerr<<"Protocol settings on USB: \n";
      for(const auto& c : um.getPayload())
        cerr<<(int)c<< " ";
      cerr<<endl;
      
      if(waitForUBXAckNack(fd, 10, 0x06, 0x00))
        cerr<<"Got ACK on USB port config"<<endl;
      else
        cerr<<"Got NACK on USB port config"<<endl;
      
      
      cerr<<"Enabling UBX-RXM-RLM"<<endl; // SAR
      enableUBXMessageUSB(fd, 0x02, 0x59);
      
      cerr<<"Enabling UBX-RXM-RAWX"<<endl; // RF doppler
      enableUBXMessageUSB(fd, 0x02, 0x15, 16);
      
      cerr<<"Enabling UBX-RXM-SFRBX"<<endl; // raw navigation frames
      enableUBXMessageUSB(fd, 0x02, 0x13);
      
      cerr<<"Enabling UBX-NAV-POSECEF"<<endl; // position
      enableUBXMessageUSB(fd, 0x01, 0x01, 8);

      if(version9)  {
        cerr<<"Enabling UBX-NAV-SIG"<<endl;  // satellite reception details
        enableUBXMessageUSB(fd, 0x01, 0x43, 8);
      }
      else {
        cerr<<"Enabling UBX-NAV-SAT"<<endl;  // satellite reception details
        enableUBXMessageUSB(fd, 0x01, 0x35, 8);
      }
      
      cerr<<"Enabling UBX-NAV-PVT"<<endl; // position, velocity, time fix
      enableUBXMessageUSB(fd, 0x01, 0x07, 1);
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

  std::map<pair<int,int>, struct timeval> lasttv, tv;
  int curCycleTOW{-1}; // means invalid

  cerr<<"Entering main loop"<<endl;
  for(;;) {
    try {
      auto [msg, timestamp] = getUBXMessage(fd);
      (void)timestamp;
      auto payload = msg.getPayload();
      // should turn this into protobuf
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
        } __attribute__((packed));
        PVT pvt;
        
        memcpy(&pvt, &payload[0], sizeof(pvt));
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

        g_gstutc.tv_sec = satt;
        g_gstutc.tv_nsec = pvt.nano;

        
        //        double seconds= pvt.sec + pvt.nano/1000000000.0;
        //        fmt::fprintf(stderr, "Satellite UTC: %02d:%02d:%06.4f -> %.4f or %d:%f\n", tm.tm_hour, tm.tm_min, seconds, satutc, timegm(&tm), pvt.nano/1000.0);

        if(!g_fromFile) {
          //          struct tm ourtime;
          //          time_t ourt = timestamp.tv_sec;
          //          gmtime_r(&ourt, &ourtime);
          //          
          //double ourutc = ourt + timestamp.tv_usec/1000000.0;
          
          //          seconds = ourtime.tm_sec + timestamp.tv_usec/1000000.0;
          //          fmt::fprintf(stderr, "Our UTC      : %02d:%02d:%06.4f -> %.4f or %d:%f -> delta = %.4fs\n", tm.tm_hour, tm.tm_min, seconds, ourutc, timestamp.tv_sec, 1.0*timestamp.tv_usec, ourutc - satutc);
        }
      }
      else if(msg.getClass() == 0x02 && msg.getType() == 0x15) {  // RAWX, the doppler stuff
        //        cerr<<"Got "<<(int)payload[11] <<" measurements "<<endl;
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
          nmm.set_localutcseconds(g_gstutc.tv_sec);
          nmm.set_localutcnanoseconds(g_gstutc.tv_nsec);
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
          emitNMM(1, nmm);
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
        /*        cerr<<"Position: ("<< p.ecefX / 100000.0<<", "
            << p.ecefY / 100000.0<<", "
            << p.ecefZ / 100000.0<<") +- "<<p.pAcc<<" cm"<<endl;*/

//        g_ourpos = {p.ecefX/100.0, p.ecefY/100.0, p.ecefZ/100.0};
        
        NavMonMessage nmm;
        nmm.set_type(NavMonMessage::ObserverPositionType);
        nmm.set_localutcseconds(g_gstutc.tv_sec);
        nmm.set_localutcnanoseconds(g_gstutc.tv_nsec);
        nmm.set_sourceid(g_srcid); 
        nmm.mutable_op()->set_x(p.ecefX /100.0);
        nmm.mutable_op()->set_y(p.ecefY /100.0);
        nmm.mutable_op()->set_z(p.ecefZ /100.0);
        nmm.mutable_op()->set_acc(p.pAcc /100.0);
        emitNMM(1, nmm);
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
            cerr<<"src "<<g_srcid<< " currently receiving: ";
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
            nmm.set_localutcseconds(g_gstutc.tv_sec);
            nmm.set_localutcnanoseconds(g_gstutc.tv_nsec);
            nmm.set_sourceid(g_srcid);         
            //            cerr<<"GPS frame, numwords: "<<(int)payload[4]<<", version: "<<(int)payload[6]<<endl;
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
            emitNMM(1, nmm);
            continue;
          }
          else if(id.first ==2) {
            //            cerr<<"gal nav sv "<<id.second<<" size "<<payload.size();
            //            cerr<<" res1 "<<(int)payload[2]<<" numwords "<<(int)payload[4] << " channel "<< (int)payload[5] << " version " << (int)payload[6]<<" res2 "<<(int)payload[7]<<endl;
            auto inav = getInavFromSFRBXMsg(payload);
            unsigned int wtype = getbitu(&inav[0], 0, 6);
            tv[id] = timestamp;
            
            //          cerr<<"gnssid "<<id.first<<" sv "<<id.second<<" " << wtype << endl;
            uint32_t satTOW;
            int msgTOW{0};
            if(getTOWFromInav(inav, &satTOW, &g_wn)) { // 0, 6, 5
              //            cerr<<"   "<<wtype<<" sv "<<id.second<<" tow "<<satTOW << " % 30 = "<< satTOW % 30<<", implied start of cycle: "<<(satTOW - (satTOW %30)) <<endl;
              msgTOW = satTOW;
              curCycleTOW = satTOW - (satTOW %30);
            }
            else {
              if(curCycleTOW < 0) // did not yet have a start of cycle
                continue;
              //            cerr<<"   "<<wtype<<" sv "<<id.second<<" tow ";
              if(wtype == 2) {
                //              cerr<<"infered to be 1 "<<curCycleTOW + 31<<endl;
                msgTOW = curCycleTOW + 31;
              }
              else if(wtype == 4) {
                //              cerr<<"infered to be 3 "<<curCycleTOW + 33<<endl;
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
                cerr<<"galileo E"<<id.second<<" what kind of wtype is this: "<<wtype<<endl;
                continue;
              }
            }
            NavMonMessage nmm;
            nmm.set_sourceid(g_srcid);
            nmm.set_type(NavMonMessage::GalileoInavType);
            nmm.set_localutcseconds(g_gstutc.tv_sec);
            nmm.set_localutcnanoseconds(g_gstutc.tv_nsec);
            
            nmm.mutable_gi()->set_gnsswn(g_wn);
            
            nmm.mutable_gi()->set_gnsstow(msgTOW);
            nmm.mutable_gi()->set_gnssid(id.first);
            nmm.mutable_gi()->set_gnsssv(id.second);
            nmm.mutable_gi()->set_sigid(sigid);
            nmm.mutable_gi()->set_contents((const char*)&inav[0], inav.size());
            
            emitNMM(1, nmm);
          }
          else if(id.first==3) {
            auto gstr = getGlonassFromSFRBXMsg(payload);
            auto cond = getCondensedBeidouMessage(gstr);
            static map<int, BeidouMessage> bms;
            auto& bm = bms[id.second];
            
            uint8_t pageno;
            bm.parse(cond, &pageno);
            
            if(bm.wn < 0) {
              cerr<<"BeiDou C"<<id.second<<" WN not yet known, not yet emitting message"<<endl;
              continue;
            }
            NavMonMessage nmm;
            nmm.set_localutcseconds(g_gstutc.tv_sec);
            nmm.set_localutcnanoseconds(g_gstutc.tv_nsec);
            nmm.set_sourceid(g_srcid);
            if(id.second > 5) {
              // this **HARDCODES** that C01,02,03,04,05 emit D2 messages!            
              nmm.set_type(NavMonMessage::BeidouInavTypeD1);
              nmm.mutable_bid1()->set_gnsswn(bm.wn);  
              nmm.mutable_bid1()->set_gnsstow(bm.sow); 
              nmm.mutable_bid1()->set_gnssid(id.first);
              nmm.mutable_bid1()->set_gnsssv(id.second);
              nmm.mutable_bid1()->set_sigid(sigid);              
              nmm.mutable_bid1()->set_contents(string((char*)gstr.c_str(), gstr.size()));
            }
            else {
              nmm.set_type(NavMonMessage::BeidouInavTypeD2);
              nmm.mutable_bid2()->set_gnsswn(bm.wn);  
              nmm.mutable_bid2()->set_gnsstow(bm.sow); 
              nmm.mutable_bid2()->set_gnssid(id.first);
              nmm.mutable_bid2()->set_gnsssv(id.second);
              nmm.mutable_bid2()->set_sigid(sigid);              
              nmm.mutable_bid2()->set_contents(string((char*)gstr.c_str(), gstr.size()));
            }
            emitNMM(1, nmm);
            continue;
          }
          else if(id.first==6) {
            //            cerr<<"SFRBX from GLONASS "<<id.second<<" @ frequency "<<(int)payload[3]<<", msg of "<<(int)payload[4]<< " words"<<endl;
            auto gstr = getGlonassFromSFRBXMsg(payload);
            /*
            static map<int, GlonassMessage> gms;
            GlonassMessage& gm = gms[id.second];
            int strno = gm.parse(gstr);
            */
            if(id.second != 255) {
              NavMonMessage nmm;
              nmm.set_localutcseconds(g_gstutc.tv_sec);
              nmm.set_localutcnanoseconds(g_gstutc.tv_nsec);
              nmm.set_sourceid(g_srcid);
              nmm.set_type(NavMonMessage::GlonassInavType);
              nmm.mutable_gloi()->set_freq(payload[3]);
              nmm.mutable_gloi()->set_gnssid(id.first);
              nmm.mutable_gloi()->set_gnsssv(id.second);
              nmm.mutable_gloi()->set_sigid(sigid);              
              nmm.mutable_gloi()->set_contents(string((char*)gstr.c_str(), gstr.size()));
              
              emitNMM(1, nmm);
            }
          }
          else
            cerr<<"SFRBX from unsupported GNSSID/sigid combination "<<id.first<<", sv "<<id.second<<", sigid "<<sigid<<", "<<payload.size()<<" bytes"<<endl;
        
#if 0        
          if(0 && lasttv.count(id)) {
            fmt::fprintf(stderr, "gnssid %d sv %d wtype %d, %d:%d -> %d:%d, delta=%d\n", 
                         payload[0], payload[1], wtype, lasttv[id].tv_sec, lasttv[id].tv_usec, tv[id].tv_sec, tv[id].tv_usec, tv[id].tv_usec - lasttv[id].tv_usec);
          }
#endif
          lasttv[id]=tv[id];

          
        }
        catch(CRCMismatch& cm) {
          cerr<<"Had CRC mismatch!"<<endl;
        }
      }
      else if(msg.getClass() == 1 && msg.getType() == 0x35) { // UBX-NAV-SAT
        //        if(version9) // we have UBX-NAV-SIG
        //          continue;
        //        cerr<< "Info for "<<(int) payload[5]<<" svs: \n";
        for(unsigned int n = 0 ; n < payload[5]; ++n) {
          int gnssid = payload[8+12*n];
          int sv = payload[9+12*n];

          auto el = (int)(char)payload[11+12*n];
          auto azi = ((int)payload[13+12*n]*256 + payload[12+12*n]);
          auto db = (int)payload[10+12*n];
          //          cerr <<"gnssid "<<gnssid<<" sv "<<sv<<" el "<<el<<endl;
          NavMonMessage nmm;
          nmm.set_sourceid(g_srcid);
          nmm.set_localutcseconds(g_gstutc.tv_sec);
          nmm.set_localutcnanoseconds(g_gstutc.tv_nsec);
          
          nmm.set_type(NavMonMessage::ReceptionDataType);
          nmm.mutable_rd()->set_gnssid(gnssid);
          nmm.mutable_rd()->set_gnsssv(sv);
          nmm.mutable_rd()->set_db(db);
          nmm.mutable_rd()->set_el(el);
          nmm.mutable_rd()->set_azi(azi);
          nmm.mutable_rd()->set_prres(*((int16_t*)(payload.c_str()+ 14 +12*n)) *0.1);
          emitNMM(1, nmm);
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
          //          cerr <<"gnssid "<<gnssid<<" sv "<<sv<<" el "<<el<<endl;
          NavMonMessage nmm;
          nmm.set_sourceid(g_srcid);
          nmm.set_localutcseconds(g_gstutc.tv_sec);
          nmm.set_localutcnanoseconds(g_gstutc.tv_nsec);
          
          nmm.set_type(NavMonMessage::ReceptionDataType);
          nmm.mutable_rd()->set_gnssid(gnssid);
          nmm.mutable_rd()->set_gnsssv(sv);
          nmm.mutable_rd()->set_db(db);
          nmm.mutable_rd()->set_prres(*((int16_t*)(payload.c_str()+ 12 +16*n)) *0.1); // ENDIANISM
          nmm.mutable_rd()->set_sigid(sigid);
          nmm.mutable_rd()->set_el(0);
          nmm.mutable_rd()->set_azi(0);
          
          emitNMM(1, nmm);
        }
        
      }

      //      writen2(1, payload.d_raw.c_str(),msg.d_raw.size());
    }
    catch(UBXMessage::BadChecksum &e) {
      cerr<<"Bad UBX checksum, skipping message"<<endl;
    }
  }
  if(!g_fromFile)
    tcsetattr(fd, TCSANOW, &g_oldtio);                                          
}                                                                         

