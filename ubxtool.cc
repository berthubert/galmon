#include <sys/types.h>                                                    
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
#include <string.h>
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



static uint8_t getUint8(int fd)
{
  uint8_t c;
  if(readn2(fd, &c, 1) != sizeof(c))
    throw EofException();
  return c;
}

static uint16_t getUint16(int fd)
{
  uint8_t c[2];
  if(readn2(fd, &c, sizeof(c)) != sizeof(c))
    throw EofException();

  return c[0] + 256*c[1];
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



int main(int argc, char** argv)
{                                                                         
  int fd, res;                                                          
  struct termios oldtio,newtio;                                           
                                                                          
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

  usleep(500000);
  
  std::basic_string<uint8_t> msg = buildUbxMessage(0x06, 0x01, {0x02, 89});
  writen2(fd, msg.c_str(), msg.size());

  msg = buildUbxMessage(0x06, 0x01, {0x02, 89, 0, 0, 0, 1, 0, 0});
  writen2(fd, msg.c_str(), msg.size());

  msg = buildUbxMessage(0x06, 0x01, {0x02, 89});
  writen2(fd, msg.c_str(), msg.size());

  /* goal: isolate UBX messages, ignore everyting else.
     The challenge is that we might sometimes hit the 0xb5 0x62 marker
     in the middle of a message, which will cause us to possibly jump over valid messages */
  

  uint8_t marker[2]={0};
  for(;;) {
    marker[0] = marker[1];
    res = readn2(fd, marker+1, 1);
    if(res < 0)
      break;
    //    cerr<<"marker now: "<< (int)marker[0]<<" " <<(int)marker[1]<<endl;
    if(marker[0]==0xb5 && marker[1]==0x62) { // bingo
      basic_string<uint8_t> msg;
      msg.append(marker, 2);
      msg.append(1, getUint8(fd)); // class
      msg.append(1, getUint8(fd)); // type
      uint16_t len = getUint16(fd);

      msg.append((uint8_t*)&len, 2);
      cerr<<"Got class "<<(int)msg[0]<<" type "<<(int)msg[1]<<", len = "<<len<<endl;
      uint8_t buffer[len+2];
      res=readn2(fd, buffer, len+2);
      msg.append(buffer, len+2); // checksum
      writen2(1, msg.c_str(),msg.size());
      marker[0]=marker[1]=0;
    }
  }                                                                       
  tcsetattr(fd,TCSANOW,&oldtio);                                          
}                                                                         

