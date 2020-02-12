#include "sp3.hh"
#include <string>
#include <string.h>
#include <stdexcept>
#include <sstream>
#include <iostream>

using namespace std;

SP3Reader::SP3Reader(std::string_view fname)
{
  d_fp = fopen(&fname[0], "r");
  if(!d_fp)
    throw runtime_error("Unable to open "+(string)fname+": "+strerror(errno));
                               
}

SP3Reader::~SP3Reader()
{
  if(d_fp)
    fclose(d_fp);
}

bool SP3Reader::get(SP3Entry& entry)
{
  char line[80];
  struct tm tm;

  while(fgets(line, sizeof(line), d_fp)) {
    if(line[0]=='*') {
      //0   1    2  3  4  5  6
      //*  2019  9 17  1  0  0.00000000

      std::stringstream ss(line);
      std::string token;
      int num = 0;
      memset(&tm, 0, sizeof(tm));
      while (std::getline(ss, token, ' ')) {
        if(token.empty())
          continue;
        int val = atoi(token.c_str());
        if(num == 1)
          tm.tm_year = val - 1900;
        else if(num==2)
          tm.tm_mon = val - 1;
        else if(num == 3)
          tm.tm_mday = val;
        else if(num == 4)
          tm.tm_hour = val;
        else if(num == 5)
          tm.tm_min = val;
        else if(num==6)
          tm.tm_sec = val;

        num++;

        //        cout<<"Token: "<<token<<endl;
      }
      d_time = timegm(&tm) - 18; // XXX leap second
    }
    else if(line[0]=='P') {
      // 0       1                2             3             4
      // PG01 -18824.158694  -8701.019206  16573.078969   -131.247183
      std::stringstream ss(line);
      std::string token;
      int num = 0;

      while (std::getline(ss, token, ' ')) {
        if(token.empty())
          continue;
        if(!num) {
          if(token[1]=='G')
            entry.gnss = 0;
          else if(token[1]=='E')
            entry.gnss = 2;
          else if(token[1]=='C')
            entry.gnss = 3;
          else
            continue;
          entry.sv = atoi(token.c_str()+2);
        }
        double val = atof(token.c_str());
        if(num == 1)
          entry.x = 1000.0*val;
        if(num == 2)
          entry.y = 1000.0*val;
        if(num == 3)
          entry.z = 1000.0*val;
        if(num == 4)
          entry.clockBias = 1000.0*val; // want nanoseconds
        num++;
      }
      entry.t = d_time;
      return true;
    }
            
  }
  return false;
  
}
