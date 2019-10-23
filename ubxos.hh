#pragma once
#include <sys/stat.h>                                                     
#include <fcntl.h>                                                        
#include <termios.h>                                                      
#include <iostream>

class openDevice
{
public:
  openDevice(const char* fname, bool doRTSCTS);
  ~openDevice();
  int fd();
  bool isPlainFile();
private:
  int device_fd;
  bool plainFile;
  int initDevice(const char* fname, bool doRTSCTS);
  void closeDevice(int fd);
};

