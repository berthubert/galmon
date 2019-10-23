#define _LARGEFILE64_SOURCE

#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <iostream>
#include "ubxos.hh"
#include "navmon.hh"

using namespace std;

extern bool doDEBUG;

#define BAUDRATE B115200

static struct termios oldtio;

static bool isPresent(string_view fname)
{
  struct stat sb;
  if(stat(&fname[0], &sb) < 0)
    return false;
  return true;
}

static bool isCharDevice(string_view fname)
{
  struct stat sb;
  if(stat(&fname[0], &sb) < 0)
    return false;
  return (sb.st_mode & S_IFMT) == S_IFCHR;
}

static void configDevice(int fd, bool doRTSCTS)
{
  struct termios newtio;
  if(tcgetattr(fd, &oldtio)) { /* save current port settings */
    throw runtime_error("tcgetattr failed: "+(string)strerror(errno));
  }

  bzero(&newtio, sizeof(newtio));
  newtio.c_cflag = CS8 | CLOCAL | CREAD;
  if (doRTSCTS)
    newtio.c_cflag |= CRTSCTS;
  newtio.c_iflag = IGNPAR;
  newtio.c_oflag = 0;

  /* set input mode (non-canonical, no echo,...) */
  newtio.c_lflag = 0;

  newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */
  newtio.c_cc[VMIN]     = 1;   /* blocking read until 5 chars received */

  cfsetspeed(&newtio, BAUDRATE);
  if(tcsetattr(fd, TCSAFLUSH, &newtio)) {
    throw runtime_error("tcsetattr failed: "+(string)strerror(errno));
  }
}

int openDevice::initDevice(const char* fname, bool doRTSCTS)
{
  int fd;
  if (doDEBUG) { cerr<<humanTimeNow()<<" initDevice()"<<endl; }
  if (!isPresent(fname)) {
    if (doDEBUG) { cerr<<humanTimeNow()<<" initDevice - "<<fname<<" - not present"<<endl; }
    throw runtime_error("Opening file "+string(fname)+": "+strerror(errno));
  }
  if(string(fname) != "stdin" && string(fname) != "/dev/stdin" && isCharDevice(fname)) {
    if (doDEBUG) { cerr<<humanTimeNow()<<" initDevice - open("<<fname<<")"<<endl; }
    fd = open(fname, O_RDWR | O_NOCTTY );
    if (fd <0 ) {
      throw runtime_error("Opening file "+string(fname)+": "+strerror(errno));
    }
    if (doDEBUG) { cerr<<humanTimeNow()<<" initDevice - open successful"<<endl; }
    configDevice(fd, doRTSCTS);
    if (doDEBUG) { cerr<<humanTimeNow()<<" initDevice - tty set successful"<<endl; }
    plainFile = false;
  }
  else {
    if (doDEBUG) { cerr<<humanTimeNow()<<" initDevice - open("<<fname<<") - from file"<<endl; }
    fd = open(fname, O_RDONLY );
    if(fd < 0)
      throw runtime_error("Opening file "+string(fname)+": "+strerror(errno));
    plainFile = true;
  }
  return fd;
}

void openDevice::closeDevice(int fd)
{
  if (doDEBUG) { cerr<<humanTimeNow()<<" closeDevice()"<<endl; }
  if(!plainFile)
    tcsetattr(fd, TCSANOW, &oldtio);
  openDevice::plainFile = false;
  close(fd);
}

//
// Public stuff
//

openDevice::openDevice(const char* fname, bool doRTSCTS)
{
  device_fd = openDevice::initDevice(fname, doRTSCTS);
}

openDevice::~openDevice()
{
  openDevice::closeDevice(device_fd);
  device_fd = -1;
}

int openDevice::fd()
{
  return device_fd;
}

bool openDevice::isPlainFile()
{
  return plainFile;
}

