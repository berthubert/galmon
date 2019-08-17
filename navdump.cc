#include <stdio.h>
#include <string>
#include <iostream>
#include <arpa/inet.h>
#include "fmt/format.h"
#include "fmt/printf.h"
#include <fstream>
#include <map>
#include <bitset>
#include <vector>
#include <thread>
#include <signal.h>
#include <time.h>
#include "ubx.hh"
#include "bits.hh"
#include "minivec.hh"
#include "navmon.pb.h"
#include "ephemeris.hh"
#include "gps.hh"
#include <unistd.h>
using namespace std;

static std::string humanTime(time_t t)
{
  struct tm tm={0};
  gmtime_r(&t, &tm);

  char buffer[80];
  strftime(buffer, sizeof(buffer), "%a, %d %b %Y %T %z", &tm);
  return buffer;
}


int main(int argc, char** argv)
{
  for(;;) {
    char bert[4];
    if(read(0, bert, 4) != 4 || bert[0]!='b' || bert[1]!='e' || bert[2] !='r' || bert[3]!='t') {
      cerr<<"EOF or bad magic"<<endl;
      break;
    }
    
    uint16_t len;
    if(read(0, &len, 2) != 2)
      break;
    len = htons(len);
    char buffer[len];
    if(read(0, buffer, len) != len)
      break;
    
    NavMonMessage nmm;
    nmm.ParseFromString(string(buffer, len));
    cout<<humanTime(nmm.localutcseconds())<<" "<<nmm.localutcnanoseconds()<<" ";
    cout<<"src "<<nmm.sourceid()<< " ";
    if(nmm.type() == NavMonMessage::ReceptionDataType) {
      cout<<"receptiondata for "<<nmm.rd().gnssid()<<","<<nmm.rd().gnsssv()<<endl;
    }
    else if(nmm.type() == NavMonMessage::GalileoInavType) {
      basic_string<uint8_t> inav((uint8_t*)nmm.gi().contents().c_str(), nmm.gi().contents().size());
      unsigned int wtype = getbitu(&inav[0], 0, 6);
      cout << "galileo inav for "<<nmm.gi().gnssid()<<","<<nmm.gi().gnsssv()<<" wtype "<< wtype << endl;
    }
    else if(nmm.type() == NavMonMessage::GPSInavType) {
      int sv = nmm.gpsi().gnsssv();
      auto cond = getCondensedGPSMessage(std::basic_string<uint8_t>((uint8_t*)nmm.gpsi().contents().c_str(), nmm.gpsi().contents().size()));
      struct GPSState gs;
      uint8_t page;
      int frame=parseGPSMessage(cond, gs, &page);
      cout<<"GPS "<<sv<<": ";
      if(frame == 1) {
        cout << "gpshealth = "<<(int)gs.gpshealth;
      }
      else if(frame == 2) {
        cout << "t0e = "<<gs.iods.begin()->second.t0e;
      }
      cout<<"\n";
    }

    else if(nmm.type() == NavMonMessage::ObserverPositionType) {
      cout<<"ECEF"<<endl;
      // XXX!! this has to deal with source id!
    }
    else if(nmm.type() == NavMonMessage::RFDataType) {
      cout<<"RFdata for "<<nmm.rfd().gnssid()<<","<<nmm.rfd().gnsssv()<<endl;
    }
    else {
      cout<<"Unknown type "<< (int)nmm.type()<<endl;
    }
  }
}

