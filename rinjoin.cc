#include <iostream>
#include "rinex.hh"
#include <map>
#include <optional>
using namespace std;

struct Value
{
  optional<int> af0Inav;
  optional<int> af0Fnav;
  int af1;
  int iod;
  optional<int> BGDE1E5a;
  optional<int> BGDE1E5b;
};

map<pair<time_t, int>, Value> satmap;

int main(int argc, char** argv)
{
  for(int n = 1; n < argc; ++n) {
    RINEXReader rr(argv[n]);
    RINEXEntry e;
    while(rr.get(e)) {
      if(e.gnss != 2)
        continue;
      //      cout << e.t <<" " << e.sv <<" " << (int64_t)(rint(ldexp(e.af0,34))) <<" " << (int64_t)(rint(ldexp(e.BGDE1E5a,32)))<<" " << (int64_t)(rint(ldexp(e.BGDE1E5b,32))) <<" "<<e.clkflags <<endl;
      auto& s=satmap[{e.t, e.sv}];
      if(((unsigned int)e.clkflags) & 512) { // I/NAV
        s.af0Inav = rint(ldexp(e.af0,34));
        s.af1 = rint(ldexp(e.af1,46));
        s.BGDE1E5a = rint(ldexp(e.BGDE1E5a,32));
        s.BGDE1E5b = rint(ldexp(e.BGDE1E5b,32));
        s.iod = e.iodnav;
      }
      else {
        s.af0Fnav = rint(ldexp(e.af0,34));
        s.af1 = rint(ldexp(e.af1,46));
        s.BGDE1E5a = rint(ldexp(e.BGDE1E5a,32));
        // E1E5b unreliable on F/NAV somehow
      }
                         
    }
    
  }
  cout<<"timestamp sv iod af0fnav af0inav af1 bgde1e5a bgde1e5b\n";
  for(const auto& s : satmap) {
    if(s.second.af0Fnav.has_value() && s.second.af0Inav.has_value() && s.second.BGDE1E5a.has_value() && s.second.BGDE1E5b.has_value())
      cout << s.first.first<<" " <<s.first.second<<" " << s.second.iod<<" "<<
      *s.second.af0Fnav << " " << *s.second.af0Inav <<" " << s.second.af1<<" " <<*s.second.BGDE1E5a <<" " << *s.second.BGDE1E5b << "\n";
  }
}
