#include "ephemeris.hh"
/* |            t0e tow     |  - > tow - t0e, <3.5 days, so ok

   | t0e                tow |   -> tow - t0e > 3.5 days, so 
                                   7*86400 - tow + t0e

   |         tow t0e        |   -> 7*86400 - tow + t0e > 3.5 days, so
                                  tow - t0e (negative age)

   | tow               t0e  |   -> 7*86400 - tow + t0e < 3.5 days, ok
*/

int ephAge(int tow, int t0e)
{
  unsigned int diff;
  unsigned int halfweek = 0.5*7*86400;
  if(t0e < tow) {
    diff = tow - t0e;
    if(diff < halfweek)
      return diff;
    else
      return -(7*86400 - tow) - t0e;
  }
  else { // "t0e in future"
    diff = 7*86400 - t0e + tow;
    if(diff < halfweek)
      return diff;
    else
      return tow - t0e; // in the future, negative age
  }
}
