#include <stdio.h>
#include <string>
#include <iostream>
#include <arpa/inet.h>
#include "fmt/format.h"
#include "fmt/printf.h"
#include <fstream>
#include <map>
#include <bitset>
#include <curses.h>
#include <vector>
#include <boost/algorithm/string.hpp>
using namespace std;


struct EofException{};

uint8_t getUint8()
{
  int c;
  c = fgetc(stdin);
  if(c == -1)
    throw EofException();
  return (uint8_t) c;
}

uint16_t getUint16()
{
  uint16_t ret;
  int res = fread(&ret, 1, sizeof(ret), stdin);
  if(res != sizeof(ret))
    throw EofException();
  //  ret = ntohs(ret);
  return ret;
}

/* lovingly lifted from RTKLIB */
unsigned int getbitu(const unsigned char *buff, int pos, int len)
{
  unsigned int bits=0;
  int i;
  for (i=pos;i<pos+len;i++) bits=(bits<<1)+((buff[i/8]>>(7-i%8))&1u);
  return bits;
}

extern int getbits(const unsigned char *buff, int pos, int len)
{
    unsigned int bits=getbitu(buff,pos,len);
    if (len<=0||32<=len||!(bits&(1u<<(len-1)))) return (int)bits;
    return (int)(bits|(~0u<<len)); /* extend sign */
}


uint16_t calcUbxChecksum(uint8_t ubxClass, uint8_t ubxType, std::basic_string_view<uint8_t> str)
{
  uint8_t CK_A = 0, CK_B = 0;

  auto update = [&CK_A, &CK_B](uint8_t c) {
    CK_A = CK_A + c;
    CK_B = CK_B + CK_A;
  };
  update(ubxClass);
  update(ubxType);
  uint16_t len = str.size();
  update(((uint8_t*)&len)[0]);
  update(((uint8_t*)&len)[1]);
  for(unsigned int I=0; I < str.size(); I++) {
    update(str[I]);
  }
  return (CK_B << 8) + CK_A;
}

struct SVIOD
{
  std::bitset<32> words;
  uint32_t t0e;
  uint8_t sisa;
  bool complete() const
  {
    return words[1] && words[3];
  }
  void addWord(std::basic_string_view<uint8_t> page);
};

void SVIOD::addWord(std::basic_string_view<uint8_t> page)
{
  uint8_t wtype = getbitu(&page[0], 0, 6);
  words[wtype]=true;
  if(wtype == 1) {
    t0e = 60*getbitu(&page[0], 16, 14);
  }
  else if(wtype == 3) {
    sisa = getbitu(&page[0], 120, 8);
  }
}

struct SVStat
{
  uint8_t e5bhs{0}, e1bhs{0};
  bool e5bdvs{false}, e1bdvs{false};
  bool disturb1{false}, disturb2{false}, disturb3{false}, disturb4{false}, disturb5{false};
  uint16_t wn{0};
  uint32_t tow{0}; // "last seen"
  uint32_t a0{0}, a1{0};
  int el{0}, db{0};
  map<int, SVIOD> iods;
  void addWord(std::basic_string_view<uint8_t> page);
  bool completeIOD() const;
  uint16_t getIOD() const;
  SVIOD liveIOD() const;
};

bool SVStat::completeIOD() const
{
  for(const auto& iod : iods)
    if(iod.second.complete())
      return true;
  return false;
}

uint16_t SVStat::getIOD() const
{
  for(const auto& iod : iods)
    if(iod.second.complete())
      return iod.first;
  throw std::runtime_error("Asked for unknown IOD");
}

SVIOD SVStat::liveIOD() const
{
  if(auto iter = iods.find(getIOD()); iter != iods.end())
    return iter->second;
  throw std::runtime_error("Asked for unknown IOD");
}

void SVStat::addWord(std::basic_string_view<uint8_t> page)
{
  uint8_t wtype = getbitu(&page[0], 0, 6);
  if(wtype >=1 && wtype <= 4) { // ephemeris 
    uint16_t iod = getbitu(&page[0], 6, 10);
    iods[iod].addWord(page);
    if(iods[iod].complete()) {
      SVIOD latest = iods[iod];
      iods.clear();
      iods[iod] = latest;
    }
  }
  else if(wtype==5) { // disturbance, health, time
    e5bhs = getbitu(&page[0], 67, 2);
    e1bhs = getbitu(&page[0], 69, 2);
    e5bdvs = getbitu(&page[0], 71, 1);
    e1bdvs = getbitu(&page[0], 72, 1);
    wn = getbitu(&page[0], 73, 12);
    tow = getbitu(&page[0], 85, 20);
    //    cout<<"Setting tow to "<<tow<<endl;
  }
    
}

struct WinKeeper
{
  WinKeeper();
  struct Window
  {
    WINDOW *header, *text;
    int sv;
    void setHeader(std::string_view line)
    {
      wclear(header);
      wmove(header, 0, 0);
      wattron(header, A_BOLD | A_UNDERLINE);
      wprintw(header, "%s", &line[0]);
      wattroff(header, A_BOLD | A_UNDERLINE);
      wrefresh(header);
    }
    void setStatus(std::string_view line)
    {
      wmove(header, 1, 0);
      wattron(header, A_BOLD);
      wprintw(header, "%s", &line[0]);
      wattroff(header, A_BOLD);
      wrefresh(header);
    }

    void emitLine(std::string_view line)
    {
      wprintw(text, "%s\n", &line[0]);
      wrefresh(text);
    }
  };
  vector<Window> d_windows;
  static int d_h, d_w;
  void emitLine(int sv, std::string_view line);
  void setStatus(int sv, std::string_view line);
};

int WinKeeper::d_h;
int WinKeeper::d_w;

WinKeeper::WinKeeper()
{
  initscr();
  getmaxyx(stdscr, d_h, d_w);

  int winwidth = d_w / 7;
  for(int n=0; n < 8 ; ++n) {
    d_windows.push_back({
        newwin(3, winwidth, 0, n*(winwidth+2)),
        newwin(d_h-3, winwidth, 3, n*(winwidth+2)),
          0});
    scrollok(d_windows[n].text, 1);
  }
};

void WinKeeper::emitLine(int sv, std::string_view line)
{
  for(auto& w: d_windows) {
    if(w.sv == sv) {
      w.emitLine(line);
      return;
    }
  }
  // nothing matched
  for(auto& w: d_windows) {
    if(!w.sv) {
      w.sv = sv;
      w.setHeader(std::to_string(sv));
      w.emitLine(line);
      return;
    }
  }
  throw std::runtime_error("Ran out of windows searching for sv "+std::to_string(sv));
}

void WinKeeper::setStatus(int sv, std::string_view line)
{
  for(auto& w: d_windows) {
    if(w.sv == sv) {
      w.setStatus(line);
      return;
    }
  }
  // nothing matched
  for(auto& w: d_windows) {
    if(!w.sv) {
      w.sv = sv;
      w.setHeader(std::to_string(sv));
      w.setStatus(line);
      return;
    }
  }
  throw std::runtime_error("Ran out of windows searching for sv "+std::to_string(sv));
}


std::map<int, SVStat> g_svstats;

int main()
try
{
  WinKeeper wk;
  
  //  unsigned int tow=0, wn=0;
  ofstream csv("iod.csv");
  ofstream csv2("toe.csv");
  csv<<"timestamp sv iod sisa"<<endl;
  csv2<<"timestamp sv tow toe"<<endl;
  int tow=0, wn=0;
  string line;
  for(;;) {
    auto c = getUint8();
    if(c != 0xb5) {
      //      cout << (char)c;
      line.append(1,c);
      if(c=='\n') {
        if(line.rfind("$GAGSV", 0)==0) {
          vector<string> strs;
          boost::split(strs,line,boost::is_any_of(","));
          for(unsigned int n=4; n + 4 < strs.size(); n += 4) {
            int sv = atoi(strs[n].c_str());
            
            g_svstats[sv].el = atoi(strs[n+1].c_str());
            g_svstats[sv].db = atoi(strs[n+3].c_str());
            
          }
        }
        line.clear();
      }
      continue;
    }
    c = getUint8();
    if(c != 0x62) {
      ungetc(c, stdin); // might be 0xb5
      continue;
    }

    // if we are here, just had ubx header

    uint8_t ubxClass = getUint8();
    uint8_t ubxType = getUint8();
    uint16_t msgLen = getUint16();
    
    //    cout <<"Had an ubx message of class "<<(int) ubxClass <<" and type "<< (int) ubxType << " of " << msgLen <<" bytes"<<endl;

    std::basic_string<uint8_t> msg;
    msg.reserve(msgLen);
    for(int n=0; n < msgLen; ++n)
      msg.append(1, getUint8());

    uint16_t ubxChecksum = getUint16();
    if(ubxChecksum != calcUbxChecksum(ubxClass, ubxType, msg)) {
      cout<<"Checksum: "<<ubxChecksum<< ", calculated: "<<
        calcUbxChecksum(ubxClass, ubxType, msg)<<"\n";
    }

    if(ubxClass == 2 && ubxType == 89) { // SAR
      string hexstring;
      for(int n = 0; n < 15; ++n)
        hexstring+=fmt::format("%x", (int)getbitu(msg.c_str(), 36 + 4*n, 4));
      
      int sv = (int)msg[2];
      wk.emitLine(sv, "SAR "+hexstring);
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
    if(ubxClass == 2 && ubxType == 19) { //UBX-RXM-SFRBX
      //      cout<<"SFRBX GNSSID "<< (int)msg[0]<<", SV "<< (int)msg[1];
      //      cout<<" words "<< (int)msg[4]<<", version "<< (int)msg[6];
      //      cout<<"\n";
      if(msg[0] != 2) // version field
        continue;
      // 7 is reserved
      
      //      cout << ((msg[8]&128) ? "Even " : "Odd ");
      //      cout << ((msg[8]&64) ? "Alert " : "Nominal ");
      unsigned int wtype = (int)(msg[11] & (~(64+128)));
      unsigned int sv = (int)msg[1];
      //      cout << "Word type "<< (int)(msg[11] & (~(64+128))) <<" SV " << (int)msg[1]<<"\n";
      //      for(unsigned int n = 8; n < msg.size() ; ++n) {
      //        fmt::printf("%02x ", msg[n]);
      //      }
      //      cout<<"\n";
      std::basic_string<uint8_t> payload;
      for(unsigned int i = 0 ; i < (msg.size() - 8) / 4; ++i)
        for(int j=1; j <= 4; ++j)
          payload.append(1, msg[8 + (i+1) * 4 -j]);
      
      //      for(auto& c : payload)
      //        fmt::printf("%02x ", c);
            
      //      cout<<"\n";

      std::basic_string<uint8_t> inav;
      unsigned int i,j;
      for (i=0,j=2; i<14; i++, j+=8)
        inav.append(1, (unsigned char)getbitu(payload.c_str()   ,j,8));
      for (i=0,j=2; i< 2; i++, j+=8)
        inav.append(1, (unsigned char)getbitu(payload.c_str()+16,j,8));

      //      cout<<"inav for "<<wtype<<" for sv "<<sv<<": ";
      //      for(auto& c : inav)
      //        fmt::printf("%02x ", c);

      g_svstats[sv].addWord(inav);
      if(wtype >=1 && wtype <= 4) { // ephemeris 
        uint16_t iod = getbitu(&inav[0], 6, 10);
        if(wtype == 1 && tow) {
          int t0e = 60*getbitu(&inav[0], 16, 14);
          int age = (tow - t0e)/60;
          uint32_t e = getbitu(&inav[0], 6+10+14+32, 32);
          wk.emitLine(sv, "Eph" +std::to_string(wtype)+", e=" + to_string(e)+", age="+to_string(age)+"m" );
        }
        else if(wtype == 3) {
          unsigned int sisa = getbitu(&inav[0], 120, 8);
          wk.emitLine(sv, "Eph3, iod="+to_string(iod)+", sisa="+to_string(sisa));
        }
        else
          wk.emitLine(sv, "Eph" +std::to_string(wtype)+", iod=" + to_string(iod));
      }
      else if(!wtype) {
        wk.emitLine(sv, ".");
        if(getbitu(&inav[0], 6,2) == 2) {
          wn = getbitu(&inav[0], 96, 12);
          tow = getbitu(&inav[0], 108, 20);
        }
      }
      else if(wtype == 5) {
        string out="IonoBGDHealth ";
        for(int n=0; n <5; ++n)
          out.append(1, getbitu(&inav[0], 42+n, 1) ? '1' : '0');
        out+=" ";
        out += to_string(getbitu(&inav[0], 67,2))+to_string(getbitu(&inav[0], 69,2))+to_string(getbitu(&inav[0], 70,1))+to_string(getbitu(&inav[0], 71,1));
        wk.emitLine(sv, out);
        wk.setStatus(sv, "Hlth: "+std::to_string(getbitu(&inav[0], 67, 2)) +", el="+to_string(g_svstats[sv].el)+", db="+to_string(g_svstats[sv].db) );
        tow = getbitu(&inav[0], 85, 20);
        wn = getbitu(&inav[0], 73, 12);
      }
      else if(wtype == 6) {
        int a0 = getbits(&inav[0], 6, 32);
        int a1 = getbits(&inav[0], 38, 24);
        int t0t = getbitu(&inav[0], 70, 8);
        uint8_t wn0t = getbits(&inav[0], 78, 8);
        int dw = (uint8_t)wn - wn0t;
        if(tow && wn)
          wk.emitLine(sv, "GST-UTC6, a0="+to_string(a0)+", a1="+to_string(a1)+", age="+to_string(tow/3600-t0t)+"h, dw="+to_string(dw)
                      +", wn0t="+to_string(wn0t)+", wn8="+to_string(wn&0xff));
        else
          wk.emitLine(sv, "GST-UTC6, a0="+to_string(a0)+", a1="+to_string(a1));

      }
      else if(wtype >= 7 && wtype <= 9) {
        uint16_t ioda = getbitu(&inav[0], 6, 4);
        wk.emitLine(sv, "Alm"+to_string(wtype)+" IODa="+to_string(ioda));
      }
      else if(wtype == 10) {
        int a0g = getbits(&inav[0], 86, 16);
        int a1g = getbits(&inav[0], 102, 12);
        int t0g = getbitu(&inav[0], 114, 8);
        uint8_t wn0g = getbitu(&inav[0], 122, 6);
        int dw = (((uint8_t)wn)&(1+2+4+8+16+32)) - wn0g;
        
        if(tow && wn) {
          time_t t = 935280000 + wn * 7*86400 + tow;
          struct tm tm;
          gmtime_r(&t, &tm);
          
          int age = tow - t0g * 3600;
          // a0g = 2^-32 s, a1 = 2^-50 s/s
          
          //          int shift = a0g * (1U<<16) + a1g * age; // in 2^-51 seconds units
          
          wk.emitLine(sv, "GST-GPS, a0g="+to_string(a0g)+", a1g="+to_string(a1g)+", t0g="+to_string(t0g)+", age="+to_string(tow/3600-t0g)+"h, dw="+to_string(dw)
                      +", wn0g="+to_string(wn0g)+", wn6="+to_string(wn&(1+2+4+8+16+32)));
        }

      }
      
      else
        wk.emitLine(sv, "Word "+std::to_string(wtype));

      //        time_t t = 935280000 + wn * 7*86400 + tow;
      /*
      for(const auto& ent : g_svstats) {
        // 12  iod   t0e
        fmt::printf("%2d\t", ent.first);
        if(ent.second.completeIOD()) {
          cout << ent.second.getIOD() << "\t" << ( ent.second.tow - ent.second.liveIOD().t0e ) << "\t" << (unsigned int)ent.second.liveIOD().sisa;
        }
        cout<<"\n";
      }
      cout<<"\n";
      */
    }
  }
 }
 catch(EofException& e)
   {}
