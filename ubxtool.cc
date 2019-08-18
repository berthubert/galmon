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
struct timespec g_gstutc;
uint16_t g_wn;
using namespace std;

uint16_t g_srcid{2};

#define BAUDRATE B921600
#define MODEMDEVICE "/dev/ttyACM0"

namespace {
  struct EofException{};
}

size_t readn2(int fd, void* buffer, size_t len)
{
  size_t pos=0;
  ssize_t res;
  for(;;) {
    res = read(fd, (char*)buffer + pos, len - pos);
    if(res == 0)
      throw EofException();
    if(res < 0) {
      throw runtime_error("failed in readn2: "+string(strerror(errno)));
    }

    pos+=(size_t)res;
    if(pos == len)
      break;
  }
  return len;
}




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

std::pair<UBXMessage, struct timeval> getUBXMessage(int fd)
{
  static int logfile;
  if(!logfile && !g_fromFile) {
    logfile = open("./logfile", O_WRONLY|O_CREAT|O_APPEND, 0600);
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
    //    cerr<<"Got: "<<(int)msg.getClass() << " " <<(int)msg.getType() <<endl;
    if(msg.getClass() == ubxClass && msg.getType() == ubxType) {
      return msg;
    }
  }
  throw std::runtime_error("Did not get response on time");
}

bool waitForUBXAckNack(int fd, int seconds)
{
  for(int n=0; n < seconds*4; ++n) {
    auto [msg, tv] = getUBXMessage(fd);
    (void)tv;
    //    cerr<<"Got: "<<(int)msg.getClass() << " " <<(int)msg.getType() <<endl;
    if(msg.getClass() == 0x05 && msg.getType() == 0x01) {
      return true;
    }
    else if(msg.getClass() == 0x05 && msg.getType() == 0x00) {
      return false;
    }
  }
  throw std::runtime_error("Did not get ACK/NACK response on time");
}

void emitNMM(int fd, const NavMonMessage& nmm)
{
            
  string out;
  nmm.SerializeToString(& out);
  writen2(fd, "bert", 4);
  uint16_t len = htons(out.size());
  writen2(fd, &len, 2);
  writen2(fd, out.c_str(), out.size());
}

void enableUBXMessageUSB(int fd, uint8_t ubxClass, uint8_t ubxType)
{
  auto msg = buildUbxMessage(0x06, 0x01, {ubxClass, ubxType, 0, 0, 0, 1, 0, 0});
  writen2(fd, msg.c_str(), msg.size());
  if(waitForUBXAckNack(fd, 2))
    return;
  else
    throw std::runtime_error("Got NACK enabling UBX message "+to_string((int)ubxClass)+" "+to_string((int)ubxType));
}

bool isCharDevice(string_view fname)
{
  struct stat sb;
  if(stat(&fname[0], &sb) < 0)
    return false;
  return (sb.st_mode & S_IFMT) == S_IFCHR;
}


// ubxtool device srcid
int main(int argc, char** argv)
{
  GOOGLE_PROTOBUF_VERIFY_VERSION;

  
  if(argc != 3) {
    cout<<"syntax: ubxtool /dev/ttyACM0 1\nDevice name, followed by your assigned source id"<<endl;
    return EXIT_FAILURE;
  }
  struct termios oldtio,newtio;                                           
  int fd;

  if(string(argv[1]) != "stdin" && string(argv[1]) != "/dev/stdin" && isCharDevice(argv[1]))
    fd = open(argv[1], O_RDWR | O_NOCTTY );
  else {
    g_fromFile = true;
    
    fd = open(argv[1], O_RDONLY );
  }
  if (fd <0) {perror(argv[1]); exit(-1); }                            
  
  g_srcid = atoi(argv[2]);
  
  if(!g_fromFile) {
    tcgetattr(fd,&oldtio); /* save current port settings */                 
    
    bzero(&newtio, sizeof(newtio));                                         
    newtio.c_cflag = BAUDRATE | CRTSCTS | CS8 | CLOCAL | CREAD;             
    newtio.c_iflag = IGNPAR;                                                
    newtio.c_oflag = 0;                                                     
    
    /* set input mode (non-canonical, no echo,...) */                       
    newtio.c_lflag = 0;                                                     
    
    newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */         
    newtio.c_cc[VMIN]     = 5;   /* blocking read until 5 chars received */ 
    
    tcflush(fd, TCIFLUSH);                                                  
    tcsetattr(fd,TCSANOW,&newtio);                                          

    for(int n=0; n < 5; ++n) {
      auto [msg, timestamp] = getUBXMessage(fd);
      (void)timestamp;
      cerr<<"Read some init: "<<(int)msg.getClass() << " " <<(int)msg.getType() <<endl;
      //    if(msg.getClass() == 0x2)
      //cerr<<string((char*)msg.getPayload().c_str(), msg.getPayload().size()) <<endl;
    }
    
    std::basic_string<uint8_t> msg;
    cerr<<"Asking for rate"<<endl;
    msg = buildUbxMessage(0x06, 0x01, {0x02, 89}); // ask for rates of class 0x02 type 89, RLM
    writen2(fd, msg.c_str(), msg.size());
    
    UBXMessage um=waitForUBX(fd, 2, 0x06, 0x01);
    cerr<<"Message rate: "<<endl;
    for(const auto& c : um.getPayload())
      cerr<<(int)c<< " ";
    cerr<<endl;
    
    msg = buildUbxMessage(0x06, 0x01, {0x02, 89, 0, 0, 0, 1, 0, 0});
    writen2(fd, msg.c_str(), msg.size());
    
    if(waitForUBXAckNack(fd, 2))
      cerr<<"Got ack on rate setting"<<endl;
    else
      cerr<<"Got nack on rate setting"<<endl;
    
    //                                  ver   RO   maxch cfgs
    msg = buildUbxMessage(0x06, 0x3e, {0x00, 0x00, 0xff, 0x02,
          //                            GPS   min  max   res   x1   x2    x3,   x4
                                        0x00, 0x04, 0x08, 0,  0x01, 0x00, 0x01, 0x00,
          //                            GAL   min  max   res   x1   x2    x3,   x4
                                        0x00, 0x04, 0x08, 0,  0x01, 0x00, 0x01, 0x00

          });
    writen2(fd, msg.c_str(), msg.size());
    
    if(waitForUBXAckNack(fd, 2))
      cerr<<"Got ack on GNSS setting"<<endl;
    else
      cerr<<"Got nack on GNSS setting"<<endl;
    
    cerr<<"Disabling NMEA"<<endl;
    msg = buildUbxMessage(0x06, 0x00, {0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x01,0x00,0x00,0x00,0x00,0x00});
    writen2(fd, msg.c_str(), msg.size());
    if(waitForUBXAckNack(fd, 2))
      cerr<<"NMEA disabled"<<endl;
    else
      cerr<<"Got NACK disabling NMEA"<<endl;
    
    msg = buildUbxMessage(0x06, 0x00, {0x03});
    writen2(fd, msg.c_str(), msg.size());
    
    um=waitForUBX(fd, 2, 0x06, 0x00);
    cerr<<"Protocol settings on USB: \n";
    for(const auto& c : um.getPayload())
      cerr<<(int)c<< " ";
    cerr<<endl;
    
    cerr<<"Enabling UBX-RXM-RAWX"<<endl; // RF doppler
    enableUBXMessageUSB(fd, 0x02, 0x15);
    
    cerr<<"Enabling UBX-RXM-SFRBX"<<endl; // raw navigation frames
    enableUBXMessageUSB(fd, 0x02, 0x13);
    
    cerr<<"Enabling UBX-NAV-POSECEF"<<endl; // position
    enableUBXMessageUSB(fd, 0x01, 0x01);
    
    cerr<<"Enabling UBX-NAV-SAT"<<endl;  // satellite reception details
    enableUBXMessageUSB(fd, 0x01, 0x35);
    
    cerr<<"Enabling UBX-NAV-PVT"<<endl; // position, velocity, time fix
    enableUBXMessageUSB(fd, 0x01, 0x07);
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
        double satutc = timegm(&tm) + pvt.nano/1000000000.0; // negative is no problem here
        if(pvt.nano < 0) {
          pvt.sec--;
          satt--;
          pvt.nano += 1000000000;
        }

        g_gstutc.tv_sec = satt;
        g_gstutc.tv_nsec = pvt.nano;

        
        double seconds= pvt.sec + pvt.nano/1000000000.0;
        //        fmt::fprintf(stderr, "Satellite UTC: %02d:%02d:%06.4f -> %.4f or %d:%f\n", tm.tm_hour, tm.tm_min, seconds, satutc, timegm(&tm), pvt.nano/1000.0);

        if(!g_fromFile) {
          struct tm ourtime;
          time_t ourt = timestamp.tv_sec;
          gmtime_r(&ourt, &ourtime);
          
          double ourutc = ourt + timestamp.tv_usec/1000000.0;
          
          seconds = ourtime.tm_sec + timestamp.tv_usec/1000000.0;
          //          fmt::fprintf(stderr, "Our UTC      : %02d:%02d:%06.4f -> %.4f or %d:%f -> delta = %.4fs\n", tm.tm_hour, tm.tm_min, seconds, ourutc, timestamp.tv_sec, 1.0*timestamp.tv_usec, ourutc - satutc);
        }
      }
      else if(msg.getClass() == 0x02 && msg.getType() == 0x15) {  // RAWX
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
          uint16_t locktimems;
          memcpy(&locktimems, &payload[40+32*n], 2);
          uint8_t prStddev = payload[43+23*n] & 0xf;
          uint8_t cpStddev = payload[44+23*n] & 0xf;
          uint8_t doStddev = payload[45+23*n] & 0xf;
          uint8_t trkStat = payload[46+23*n] & 0xf;

          NavMonMessage nmm;
          nmm.set_type(NavMonMessage::RFDataType);
          nmm.set_localutcseconds(g_gstutc.tv_sec);
          nmm.set_localutcnanoseconds(g_gstutc.tv_nsec);
          nmm.set_sourceid(g_srcid); 

          nmm.mutable_rfd()->set_gnssid(gnssid);
          nmm.mutable_rfd()->set_gnsssv(sv);
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
        cerr<<"Position: ("<< p.ecefX / 100000.0<<", "
            << p.ecefY / 100000.0<<", "
            << p.ecefZ / 100000.0<<") +- "<<p.pAcc<<" cm"<<endl;

//        g_ourpos = {p.ecefX/100.0, p.ecefY/100.0, p.ecefZ/100.0};
        
        NavMonMessage nmm;
        nmm.set_type(NavMonMessage::ObserverPositionType);
        nmm.set_localutcseconds(g_gstutc.tv_sec);
        nmm.set_localutcnanoseconds(g_gstutc.tv_nsec);
        nmm.set_sourceid(g_srcid); 
        nmm.mutable_op()->set_x(p.ecefX /100.0);
        nmm.mutable_op()->set_y(p.ecefY /100.0);
        nmm.mutable_op()->set_z(p.ecefZ /100.0);
        nmm.mutable_op()->set_acccm(p.pAcc /100.0);
        emitNMM(1, nmm);
      }
      else if(msg.getClass() == 2 && msg.getType() == 0x13) {  // SFRBX
                  
        // order: 2, 4, 6, 7/9, 8/10, 0, 0, 0, 0, 0, 1, 3, 5, 0, 0
        //              *             *  *  *  *  *        * 
        // tow    
        
        try {
          pair<int,int> id = make_pair(payload[0], payload[1]);
          if(id.first == 0) {
            NavMonMessage nmm;
            nmm.set_type(NavMonMessage::GPSInavType);
            nmm.set_localutcseconds(g_gstutc.tv_sec);
            nmm.set_localutcnanoseconds(g_gstutc.tv_nsec);
            nmm.set_sourceid(g_srcid);         
            //            cerr<<"GPS frame, numwords: "<<(int)payload[4]<<", version: "<<(int)payload[6]<<endl;
            static int wn, tow;
            auto gpsframe = getGPSFromSFRBXMsg(id.second, payload);
            auto cond = getCondensedGPSMessage(gpsframe);
            auto frameno = getbitu(&cond[0], 24+19, 3);
            if(frameno == 1)
              wn = 2048 + getbitu(&cond[0], 2*24, 10);
            if(!wn) 
              continue; // can't file this yet
              
            tow = 1.5*(getbitu(&cond[0], 24, 17)*4);
            nmm.mutable_gpsi()->set_gnsswn(wn);   // XXX this sucks
            nmm.mutable_gpsi()->set_gnsstow(tow); // "with 6 second increments" -- needs to be adjusted
            nmm.mutable_gpsi()->set_gnssid(id.first);
            nmm.mutable_gpsi()->set_gnsssv(id.second);
            nmm.mutable_gpsi()->set_contents(string((char*)gpsframe.c_str(), gpsframe.size()));
            emitNMM(1, nmm);
            continue;
          }
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
              cerr<<"what kind of wtype is this"<<endl;
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
          nmm.mutable_gi()->set_contents((const char*)&inav[0], inav.size());
        
          emitNMM(1, nmm);
        
        
          if(0 && lasttv.count(id)) {
            fmt::fprintf(stderr, "gnssid %d sv %d wtype %d, %d:%d -> %d:%d, delta=%d\n", 
                         payload[0], payload[1], wtype, lasttv[id].tv_sec, lasttv[id].tv_usec, tv[id].tv_sec, tv[id].tv_usec, tv[id].tv_usec - lasttv[id].tv_usec);
          }
          lasttv[id]=tv[id];          
        }
        catch(CRCMismatch& cm) {
          cerr<<"Had CRC mismatch!"<<endl;
        }
      }
      else if(msg.getClass() == 1 && msg.getType() == 0x35) { // UBX-NAV-SAT
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

      //      writen2(1, payload.d_raw.c_str(),msg.d_raw.size());
    }
    catch(UBXMessage::BadChecksum &e) {
      cerr<<"Bad UBX checksum, skipping message"<<endl;
    }
  }
  if(!g_fromFile)
    tcsetattr(fd,TCSANOW,&oldtio);                                          
}                                                                         

