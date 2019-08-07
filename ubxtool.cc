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
using namespace std;

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


class UBXMessage
{
public:
  explicit UBXMessage(basic_string_view<uint8_t> src)
  {
    d_raw = src;
    if(d_raw.size() < 6)
      throw std::runtime_error("Partial UBX message");

    uint16_t csum = calcUbxChecksum(getClass(), getType(), d_raw.substr(6, d_raw.size()-8));
    if(csum != d_raw.at(d_raw.size()-2) + 256*d_raw.at(d_raw.size()-1))
      throw std::runtime_error("Bad UBX checksum");
    
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

std::pair<struct timeval, UBXMessage> getUBXMessage(int fd)
{
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
      
      return make_pair(tv, UBXMessage(msg));
    }
  }                                                                       
}

UBXMessage waitForUBX(int fd, int seconds, uint8_t ubxClass, uint8_t ubxType)
{
  for(int n=0; n < seconds*20; ++n) {
    auto [timestamp, msg] = getUBXMessage(fd);
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
    auto [timestamp, msg] = getUBXMessage(fd);
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


void enableUBXMessageUSB(int fd, uint8_t ubxClass, uint8_t ubxType)
{
  auto msg = buildUbxMessage(0x06, 0x01, {ubxClass, ubxType, 0, 0, 0, 1, 0, 0});
  writen2(fd, msg.c_str(), msg.size());
  if(waitForUBXAckNack(fd, 2))
    return;
  else
    throw std::runtime_error("Got NACK enabling UBX message "+to_string((int)ubxClass)+" "+to_string((int)ubxType));
}

int main(int argc, char** argv)
{                                                                         
  int fd;
  struct termios oldtio,newtio;                                           
  ofstream orbitcsv("orbit.csv");
  orbitcsv<<"timestamp gnssid sv prmes cpmes doppler"<<endl;
  fd = open(MODEMDEVICE, O_RDWR | O_NOCTTY );                             
  if (fd <0) {perror(MODEMDEVICE); exit(-1); }                            
                                                                          
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
    auto [timestamp, msg] = getUBXMessage(fd);
    cerr<<"Read some init: "<<(int)msg.getClass() << " " <<(int)msg.getType() <<endl;
    //    if(msg.getClass() == 0x2)
    //cerr<<string((char*)msg.getPayload().c_str(), msg.getPayload().size()) <<endl;
  }
  
  std::basic_string<uint8_t> msg;
  if(argc> 1 && string(argv[1])=="cold") {
    cerr<<"Sending cold start!"<<endl;
    msg = buildUbxMessage(0x06, 0x04, {0xff, 0xff, 0x04, 0x00}); // cold start!
    writen2(fd, msg.c_str(), msg.size());
    
    for(int n=0; n < 10; ++n) {
      auto [timestamp, msg] = getUBXMessage(fd);
      cerr<<"Read some init: "<<(int)msg.getClass() << " " <<(int)msg.getType() <<endl;
    }
  }
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
  
  msg = buildUbxMessage(0x06, 0x01, {0x02, 89});
  writen2(fd, msg.c_str(), msg.size());
  um=waitForUBX(fd, 2, 0x06, 0x01); // ignore
  
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

  cerr<<"Enabling UBX-RXM-RAWX"<<endl;
  enableUBXMessageUSB(fd, 0x02, 0x15);

    cerr<<"Enabling UBX-RXM-SFRBX"<<endl;
  enableUBXMessageUSB(fd, 0x02, 0x13);

  cerr<<"Enabling UBX-NAV-POSECEF"<<endl;
  enableUBXMessageUSB(fd, 0x01, 0x01);

  cerr<<"Enabling UBX-NAV-SAT"<<endl;
  enableUBXMessageUSB(fd, 0x01, 0x35);

  
  
  /* goal: isolate UBX messages, ignore everyting else.
     The challenge is that we might sometimes hit the 0xb5 0x62 marker
     in the middle of a message, which will cause us to possibly jump over valid messages */

  std::map<pair<int,int>, struct timeval> lasttv, tv;

  for(;;) {
    auto [timestamp, msg] = getUBXMessage(fd);
    auto payload = msg.getPayload();

    if(msg.getClass() == 0x01 && msg.getType() == 0x01) {  // POSECF
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
    }
    if(msg.getClass() == 2 && msg.getType() == 0x13) {  // SFRBX
      pair<int,int> id = make_pair(payload[0], payload[1]);
      tv[id] = timestamp;
      if(lasttv.count(id)) {
        fmt::fprintf(stderr, "gnssid %d sv %d, %d:%d -> %d:%d, delta=%d\n", 
                     payload[0], payload[1], lasttv[id].tv_sec, lasttv[id].tv_usec, tv[id].tv_sec, tv[id].tv_usec, tv[id].tv_usec - lasttv[id].tv_usec);
      }
      lasttv[id]=tv[id];          
    }
    writen2(1, msg.d_raw.c_str(),msg.d_raw.size());
#if 0
    if(msg.getClass() == 0x02 && msg.getType() == 0x15) {  // RAWX
      cerr<<"Got "<<(int)payload[11] <<" measurements "<<endl;
      double rcvTow;
      memcpy(&rcvTow, &payload[0], 8);        
      for(int n=0 ; n < payload[11]; ++n) {

        double prMes;
        double cpMes;
        float doppler;

        memcpy(&prMes, &payload[16+32*n], 8);
        memcpy(&cpMes, &payload[24+32*n], 8);
        memcpy(&doppler, &payload[32+32*n], 4);
        int gnssid = payload[36+32*n];
        int sv = payload[37+32*n];
        orbitcsv << std::fixed << rcvTow <<" " <<gnssid <<" " <<sv<<" "<<prMes<<" "<<cpMes <<" " << doppler<<endl;
      }
    }
#endif

  }
  tcsetattr(fd,TCSANOW,&oldtio);                                          
}                                                                         

