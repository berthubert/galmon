#include "SGP4.h"
#include <string>
#include <time.h>
using namespace std;
int main(int argc, char **argv)
{
  string line;
  //  DateTime d(2019, 8, 31, 1, 52, 30);
  time_t now = time(0);
  struct tm tm;
  gmtime_r(&now, &tm);
    
  for(;;) {
    DateTime d(1900 + tm.tm_year, tm.tm_mon+1, tm.tm_mday, 04, 43, 51);
    string name, line1, line2;
    if(!getline(cin, name) || !getline(cin, line1) || !getline(cin, line2))
      break;
    name.resize(name.size()-1);
    line1.resize(line1.size()-1);
    line2.resize(line2.size()-1);
    Tle tle(line1, line2);
    /*
    cout<<"line1: "<<line1<<endl;
    cout<<"line2: "<<line2<<endl;
    cout << tle.ToString() << endl;
    */

    SGP4 sgp4(tle);

    for(int n = 0 ; n < 1; ++n) {
      auto eci = sgp4.FindPosition(d);
      cout << name <<" "<<d <<": "<<eci.Position() << " " <<eci.Velocity() << " " << eci.ToGeodetic()<<endl;
      double theta = -eci.GetDateTime().ToGreenwichSiderealTime();
      Vector rot = eci.Position();
      rot.x = eci.Position().x * cos(theta) - eci.Position().y * sin(theta);
      rot.y = eci.Position().x * sin(theta) + eci.Position().y * cos(theta);
      cout << " " << rot<< endl;
        
      d = d.AddSeconds(30);
    }
  }
  
}
