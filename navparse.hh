#pragma once
#include "navmon.hh"
#include "galileo.hh"
#include "gps.hh"
#include "beidou.hh"
#include "glonass.hh"
#include <map>
#include "tle.hh"
#include "sbas.hh"
#include "ephemeris.hh"
#include "rtcm.hh"

using namespace std; // XXX

struct SVPerRecv
{
  int el{-1}, azi{-1}, db{-1};
  time_t deltaHzTime{-1};
  double deltaHz{-1};
  double prres{-1};
  int used{-1}; // -1 = unknown
  int qi{-1}; // quality indicator, -1 = unknown
  time_t t; // last seen
};

class InfluxPusher;
struct SVStat
{
  int gnss;

  GPSState gpsmsg;     // continuously being updated
  GPSState gpsmsg2, gpsmsg3;     // new ephemeris being assembled here
  GPSState ephgpsmsg, oldephgpsmsg;  // always has a consistent ephemeris
  GPSAlmanac gpsalma;

  int wn() const;      // gets from the 'live' message
  int tow() const;     // same

  TLERepo::Match tleMatch;
  double lastTLELookupX{0};
  
  // live, ephemeris
  BeidouMessage beidoumsg, ephBeidoumsg, oldephBeidoumsg;
  // internal
  BeidouMessage lastBeidouMessage1, lastBeidouMessage2;
  
  // new galileo
  //              consistent, live
  GalileoMessage ephgalmsg, galmsg, oldephgalmsg;
  // internal
  map<int, GalileoMessage> galmsgTyped;
  bool osnma{false};
  time_t osnmaTime{0};
  
  // Glonass
  GlonassMessage ephglomsg, glonassMessage, oldephglomsg;
  pair<uint32_t, GlonassMessage> glonassAlmaEven;
  
  map<uint64_t, SVPerRecv> perrecv;

  double latestDisco{-1}, latestDiscoAge{-1}, timeDisco{-1000};

  map<int, SBASCombo> sbas;

  RTCMMessage::EphemerisDelta rtcmEphDelta;
  RTCMMessage::ClockDelta rtcmClockDelta;
  
  const GPSLikeEphemeris& liveIOD() const;
  const GPSLikeEphemeris& prevIOD() const;

  bool completeIOD() const;
  double getCoordinates(double tow, Point* p, bool quiet=true) const;
  double getOldEphCoordinates(double tow, Point* p, bool quiet=true) const;
  void getSpeed(double tow, Vector* v) const;
  DopplerData doDoppler(double tow, const Point& us, double freq) const;
  void reportNewEphemeris(const SatID& id, InfluxPusher& idb);
};


typedef std::map<SatID, SVStat> svstats_t;

// a vector of pairs of latidude,vector<longitude,numsats>
typedef vector<pair<double,vector<tuple<double, int, int, int, double, double, double, double, double, double, double, double, double> > > > covmap_t;
covmap_t emitCoverage(const vector<Point>& sats);
struct xDOP
{
  double gdop{-1};
  double pdop{-1};
  double tdop{-1};
  double hdop{-1};
  double vdop{-1};
};

xDOP getDOP(Point& us, vector<Point> sats);
