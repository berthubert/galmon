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
#include "galileo.hh"
#include "navmon.pb.h"
#include <unistd.h>
#include "navmon.hh" 
using namespace std;


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

    void clear()
    {
      wclear(header);
      wrefresh(header);
      wclear(text);
      wrefresh(text);
    }
    time_t lastTime{0};
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
  // nothing matched, need to find a new window
  for(auto& w: d_windows) {
    if(!w.sv) {
      w.sv = sv;
      w.setHeader(std::to_string(sv));
      w.emitLine(line);
      return;
    }
  }
  // all windows are in use, take over oldest one
  int age=0;
  time_t now = time(0);
  auto *wptr = &*d_windows.begin();
  for(auto& w: d_windows) {
    if(now - w.lastTime > age) {
      age = now - w.lastTime;
      wptr = &w;
    }
  }

  wptr->sv = sv;
  wptr->setHeader(std::to_string(sv));
  wptr->emitLine(line);
  wptr->lastTime = now;
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


int main()
{
  WinKeeper wk;
  

  for(;;) {
    char bert[4];
    if(readn2(0, bert, 4) != 4 || bert[0]!='b' || bert[1]!='e' || bert[2] !='r' || bert[3]!='t') {
      cerr<<"EOF or bad magic"<<endl;
      break;
    }
    
    uint16_t len;
    if(readn2(0, &len, 2) != 2)
      break;
    len = htons(len);
    char buffer[len];
    if(readn2(0, buffer, len) != len)
      break;
    
    NavMonMessage nmm;
    nmm.ParseFromString(string(buffer, len));
    int sv = nmm.gi().gnsssv();
    time_t now = time(0);
    if(nmm.localutcseconds() < now - 300)
      continue;
    if(nmm.type() == NavMonMessage::GalileoInavType) {
      static map<int, GalileoMessage> gms;
      GalileoMessage& gm = gms[nmm.gi().gnsssv()];
      
      basic_string<uint8_t> inav((uint8_t*)nmm.gi().contents().c_str(), nmm.gi().contents().size());
      int wtype =gm.parse(inav);
      wk.emitLine(sv, "src "+to_string(nmm.sourceid())+" wtype " + to_string(wtype));
      //        wk.setStatus(sv, "Hlth: "+std::to_string(getbitu(&inav[0], 67, 2)) +", el="+to_string(g_svstats[sv].el)+", db="+to_string(g_svstats[sv].db) );
      //          wk.emitLine(sv, "GST-UTC6, a0="+to_string(a0)+", a1="+to_string(a1)+", age="+to_string(tow/3600-t0t)+"h, dw="+to_string(dw)
      //                      +", wn0t="+to_string(wn0t)+", wn8="+to_string(wn&0xff));
      //          wk.emitLine(sv, "GST-UTC6, a0="+to_string(a0)+", a1="+to_string(a1));

      //        wk.emitLine(sv, "Alm"+to_string(wtype)+" IODa="+to_string(ioda));
      //          wk.emitLine(sv, "GST-GPS, a0g="+to_string(a0g)+", a1g="+to_string(a1g)+", t0g="+to_string(t0g)+", age="+to_string(tow/3600-t0g)+"h, dw="+to_string(dw)
      //                      +", wn0g="+to_string(wn0g)+", wn6="+to_string(wn&(1+2+4+8+16+32)));
    }
  }
 }
