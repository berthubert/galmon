#include "rinex.hh"
#include <string>
#include <string.h>
#include <stdexcept>
#include <fstream>
#include <iostream>

using namespace std;

/*
Header:
     3.03           N: GNSS NAV DATA    M: MIXED            RINEX VERSION / TYPE
sbf2rin-13.2.1                          20191217 004932 UTC PGM / RUN BY / DATE 
GPSA   8.3819E-09 -1.4901E-08 -5.9605E-08  1.1921E-07       IONOSPHERIC CORR    
GPSB   9.4208E+04 -1.3107E+05 -1.3107E+05  8.5197E+05       IONOSPHERIC CORR    
GAL    3.0500E+01  3.3203E-01  2.9907E-03  0.0000E+00       IONOSPHERIC CORR    
GPUT  5.8207660913E-11 3.996802889E-15 122400 2084          TIME SYSTEM CORR    
GAUT  0.0000000000E+00 0.000000000E+00  86400 2084          TIME SYSTEM CORR    
GPGA  4.0745362639E-09 9.325873407E-15 172800 2084          TIME SYSTEM CORR    
    18                                                      LEAP SECONDS        
                                                            END OF HEADER       
*/

RINEXReader::RINEXReader(std::string_view fname)
{
  d_fname=fname;
  d_fp = gzopen(&fname[0], "r");
  if(!d_fp)
    throw runtime_error("Unable to open "+(string)fname+": "+strerror(errno));

  try {
    skipFileHeader();
  }
  catch(...) {
    gzclose(d_fp);
    throw;
  }
}

void RINEXReader::skipFileHeader()
{
  char line[300];
  bool eoh=false;
  int lines=0;
  while(gzgets(d_fp, line, sizeof(line))) {
    if(strstr(line, "END OF HEADER")) {
      eoh=true;
      break;
    }
    if(lines++ > 250)
      break;
  }
  if(!eoh) {
    throw std::runtime_error("Did not find RINEX END OF HEADER in "+d_fname);
  }
}

RINEXReader::~RINEXReader()
{
  if(d_fp)
    gzclose(d_fp);
}

/* RINEX format.. is very special. This extracts a value from a line
   where it should be noted values can be and often are back to back
*/

static double getRINEXValue(char* line, int offset)
{
  char* ptr=line+offset+19;
  char tmp = *ptr;
  *ptr = 0;
  double ret;
  sscanf(line + offset, "%lf", &ret);
  //  cout<<"'"<<string(line+offset)<<"'\n";
  *ptr = tmp;
  return ret;
}

bool RINEXReader::get(RINEXEntry& entry)
{
  char line[300];
  struct tm tm;
  memset(&tm, 0, sizeof(tm));

  /* 
G02 2019 12 16 00 00 00-3.670863807201E-04-7.389644451905E-12 0.000000000000E+00
     7.400000000000E+01-9.337500000000E+01 4.647693595094E-09-1.766354782990E+00
    -4.515051841736E-06 1.956839906052E-02 2.739951014519E-06 5.153594841003E+03
     8.640000000000E+04 3.296881914139E-07-3.996987526460E-01 2.700835466385E-07
     9.574821167563E-01 3.221250000000E+02-1.689421056425E+00-8.195698526598E-09
    -2.167947446570E-10 1.000000000000E+00 2.084000000000E+03 0.000000000000E+00
     2.000000000000E+00 0.000000000000E+00-1.769512891769E-08 7.400000000000E+01
     7.921800000000E+04 4.000000000000E+00
  */
  

  //  SV   YR  MN DY HR MN SS___________________===================___________________
  //  G02 2019 12 16 00 00 00-3.670863807201E-04-7.389644451905E-12 0.000000000000E+00

  for(;;) {
    if(!(gzgets(d_fp, line, sizeof(line))))
      return false;
    //    cerr<<"Line: "<<line;
    if(strstr(line, "RINEX")) {
      //      cerr<<"skipFileHeader"<<endl;
      skipFileHeader();
      continue; 
    }
    
    //    cout<<"Line: '"<<line<<"'"<<endl;
    bool unknownConstellation=false;

    char constellation = *line;
    if(*line=='G')
      entry.gnss=0;
    else if(*line=='E')
      entry.gnss = 2;
    else if(*line=='C')
      entry.gnss = 3;
    else if(*line=='E')
      entry.gnss = 6;
    else
      unknownConstellation = true;

    if(constellation=='S' || constellation=='R') {
      for(int n=0 ; n < 3; ++n) {
        if(!gzgets(d_fp, line, sizeof(line))) 
          return false;
      }
      continue;

    }

    char tmp=line[24];
    line[24]=0;
    char gnss;
    if(sscanf(line, "%c%02d %d %d %d %d %d %d",
           &gnss, &entry.sv, &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
              &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 8) {
      throw std::runtime_error("Failed to parse '"+string(line)+"'");
    }
    line[24]=tmp;
    bool skip=false;
    tm.tm_mon -= 1;
    tm.tm_year -= 1900;

    entry.t=timegm(&tm);

    // af0, af1, af2
    entry.af0 = getRINEXValue(line, 23);
    entry.af1 = getRINEXValue(line, 42);
    entry.af2 = getRINEXValue(line, 61);
    
    // 5 lines of which we store a bit, store 6th
    for(int n=1 ; n < 7; ++n) {
      if(!gzgets(d_fp, line, sizeof(line))) 
        return false;
      if(n==1) {
        entry.iodnav = getRINEXValue(line, 4);
      }
      if(n==3) { 
        double toe = getRINEXValue(line, 4);
        entry.toe = toe;
      }
      if(n==5) {
        entry.clkflags = getRINEXValue(line, 23);
      }
      if(n==6) {
        entry.BGDE1E5a = getRINEXValue(line, 42);
        entry.BGDE1E5b = getRINEXValue(line, 61);
      }
      //      cerr<<"Line "<<n<<": "<<line;
    }
    // line 6
    entry.sisa = getRINEXValue(line, 4);
    double health = getRINEXValue(line, 23);
    entry.health = health; // yeah..
    
    //last line, number 7
    if(!gzgets(d_fp, line, sizeof(line)))
      return false;

    double tow = getRINEXValue(line, 4);
    entry.tow = tow;
    
    if(skip)
      continue;
    if(unknownConstellation)
      continue;
    return true;
  }
  return false;
}

RINEXNavWriter::RINEXNavWriter(std::string_view fname) : d_ofs((string) fname)
{
  extern const char* g_gitHash;

  d_ofs <<"      3.03          N: GNSS NAV DATA    M: MIXED            RINEX VERSION / TYPE\n";
  // 0                 20                             40                  60
  // sbf2rin-13.2.0    galmon.eu                      20190923 000219 UTC PGM / RUN BY / DATE 

  time_t now=time(0);
  struct tm tm;
  gmtime_r(&now, &tm);

  
  d_ofs << fmt::format("{:<20}{:<20}{:<20}PGM / RUN BY / DATE\n",
                      "galmon-"+string(g_gitHash),
                      "galmon.eu project",
                     fmt::sprintf("%4d%02d%02d %02d%02d%02d UTC",
                                  tm.tm_year +1900,
                                  tm.tm_mon + 1,
                                  tm.tm_mday,
                                  tm.tm_hour,
                                  tm.tm_min,
                                  tm.tm_sec));
  /*
"GAL    2.9500E+01  7.0312E-02  1.0590E-02  0.0000E+00        IONOSPHERIC CORR    \n"
"GAUT -9.3132257462E-10 8.881784197E-16      0 2072           TIME SYSTEM CORR    \n"
  */
  d_ofs<<"   18                                                       LEAP SECONDS        \n";
  d_ofs<<"                                                            END OF HEADER       \n";

}
