#include "sbas.hh"
#include <iostream>
using namespace std;
#include "bits.hh"
#include <math.h>

void SBASState::parse0(const vector<uint8_t>& sbas, time_t now)
{
  d_lastDNU = now;
  d_lastSeen = now;
}


void SBASState::parse1(const vector<uint8_t>& sbas, time_t now)
{
  d_lastSeen = now;
  int slot=1;
  d_slot2prn.clear();
  for(int prn = 0; prn < 210; ++prn) {
    if(getbitu(&sbas[0], 14+ prn, 1)) {
      d_slot2prn[slot]=prn+1;
      //      cout<<slot<<"=G"<<prn+1<<" ";
      slot++;
    }
  }
}

vector<SBASState::FastCorrection> SBASState::parse2_5(const vector<uint8_t>&sbas, time_t now)
{
  d_lastSeen = now;
  int type = getbitu(&sbas[0], 8, 6);
  vector<SBASState::FastCorrection> ret;
  // IODFi, IODP, 13*12 bits fast correction, 13*4 bits UDREI
  for(int pos = 0; pos < 13; ++pos) {
    int slot = 1+13*(type-2)+pos;

    if(d_slot2prn.count(slot)) {
      FastCorrection fc;
      fc.id = getSBASSatID(slot);
      fc.udrei =      getbitu(&sbas[0], 14 + 4 + 12*13 + 4*pos, 4);      
      fc.correction = getbits(&sbas[0], 14 + 4 + 12*pos, 12)*0.125;
      fc.lastUpdate=now;
      ret.push_back(fc);
      d_fast[fc.id] = fc;
    }
  }
  return ret;
}

vector<SBASState::FastCorrection> SBASState::parse6(const vector<uint8_t>&sbas, time_t now)
{
  d_lastSeen = now;
  vector<SBASState::FastCorrection> ret;
  
  for(int slot = 0; slot < 51; ++slot) {
    SatID sid = getSBASSatID(slot + 1);
    if(sid.gnss == 255)
      continue;
    if(!d_fast.count(sid))
      continue;
    
    FastCorrection& fc = d_fast[sid];
    fc.id = sid;
    fc.udrei = getbitu(&sbas[0], 14 + 8 + 4* slot, 4);
    fc.lastUpdate = now;
    ret.push_back(fc);
  }
  return ret;
}

void SBASState::parse7(const vector<uint8_t>&sbas, time_t now)
{
  d_lastSeen = now;
  d_latency = getbitu(&sbas[0], 14+4, 4);
}

int SBASState::getSBASNumber(int slot) const
{
  auto iter = d_slot2prn.find(slot);
  if(iter != d_slot2prn.end()) {
    int prn = iter->second;
    if(prn < 37)
      return prn;
  }
  return -1;
}

SatID SBASState::getSBASSatID(int slot) const
{
  SatID ret;
  auto iter = d_slot2prn.find(slot);
  if(iter != d_slot2prn.end()) {
    int prn = iter->second;
    if(prn < 37) {
      ret.gnss = 0;
      ret.sv = prn;
      ret.sigid = 0;
      return ret;
    }
  }
  return ret;
}

vector<SBASState::LongTermCorrection> SBASState::parse25(const vector<uint8_t>& sbas, time_t t)
{
  d_lastSeen = t;
  vector<LongTermCorrection> ret;
  for(int n=0; n < 2; ++n) {
    parse25H(sbas, t, 14 +106 *n, ret);         
  }
  return ret;
}

pair<vector<SBASState::FastCorrection>, vector<SBASState::LongTermCorrection>> SBASState::parse24(const vector<uint8_t>& sbas, time_t t)
{
  d_lastSeen = t;
  pair<vector<FastCorrection>, vector<LongTermCorrection>> ret;
  int fcid = getbitu(&sbas[0], 14+98, 2);

  for(int pos = 0; pos < 6; ++pos) {
    FastCorrection fc;
    int slot = 13*(fcid)+pos+1;
    fc.id = getSBASSatID(slot);
    
    fc.correction = getbits(&sbas[0], 14 + 12*pos, 12)*0.125;
    fc.udrei = getbitu(&sbas[0], 14 + 72 + 4*pos, 4);
    fc.lastUpdate = t;
    if(d_slot2prn.count(slot)) {
      
      d_fast[fc.id] = fc;
      ret.first.push_back(fc);
    }
  }
  parse25H(sbas, t, 120, ret.second);
  return ret;
}

pair<vector<SBASState::FastCorrection>, vector<SBASState::LongTermCorrection>> SBASState::parse(const std::vector<uint8_t>& sbas, time_t now)
{
  pair<vector<SBASState::FastCorrection>, vector<SBASState::LongTermCorrection>> ret;
  int type = getbitu(&sbas[0], 8, 6);
  if(type == 0) {
    parse0(sbas, now);
  }
  else if(type == 1) {
    parse1(sbas, now);
  }
  else if(type >= 2 && type <= 5) {
    ret.first = parse2_5(sbas, now);
  }
  
  else if(type == 6) {
    ret.first = parse6(sbas, now);
  }
  else if(type ==7) {
    parse7(sbas, now);
  }
  else if(type == 24) {
    ret = parse24(sbas, now);
  }
  else if(type == 25) {
    ret.second = parse25(sbas, now);
  }
  return ret;
}

void SBASState::parse25H(const vector<uint8_t>& sbas, time_t t, int offset, vector<LongTermCorrection>& ret)
{
  LongTermCorrection ltc;
  ltc.velocity = getbitu(&sbas[0], offset, 1);
        
  if(ltc.velocity) { // 1 SV
    int slot = getbitu(&sbas[0], offset + 1, 6);
    ltc.id = getSBASSatID(slot);
    ltc.iod8 = getbitu(&sbas[0], offset + 7, 8);

    ltc.dx = 0.125*getbits(&sbas[0], offset + 15, 11);
    ltc.dy = 0.125*getbits(&sbas[0], offset + 26, 11);
    ltc.dz = 0.125*getbits(&sbas[0], offset + 37, 11);
    ltc.dai = 1000000000.0*ldexp(getbits(&sbas[0], offset + 48, 11), -31);
            
    ltc.ddx = ldexp(getbits(&sbas[0], offset + 59, 8), -11);
    ltc.ddy = ldexp(getbits(&sbas[0], offset  + 67, 8), -11);
    ltc.ddz = ldexp(getbits(&sbas[0], offset  + 75, 8), -11);
    ltc.ddai = 1000000000.0*ldexp(getbits(&sbas[0], offset + 83, 8), -39);
    
    ltc.toa = 16 * getbitu(&sbas[0], offset+91, 13);
    ltc.iodp = getbitu(&sbas[0], offset+104, 2);
    // 105
    ltc.lastUpdate = t;
    if(ltc.toa) {
      ret.push_back(ltc);
      d_longterm[ltc.id]=ltc;
    }
  }
  else {
    for(int n = 0 ; n < 2; ++n) {
      int slot = getbitu(&sbas[0], offset + 1, 6);
      ltc.id = getSBASSatID(slot);
      ltc.iod8 = getbitu(&sbas[0], offset + 7, 8);
      
      ltc.dx = 0.125*getbits(&sbas[0], offset + 15, 9);
      ltc.dy = 0.125*getbits(&sbas[0], offset + 24, 9);
      ltc.dz = 0.125*getbits(&sbas[0], offset + 33, 9);
      
      ltc.dai = 1000000000.0*ldexp(getbits(&sbas[0], offset + 42, 10), -31);
      
      ltc.ddx = ltc.ddy = ltc. ddz = ltc.ddai = 0;
      ltc.lastUpdate = t;
      ret.push_back(ltc);
      d_longterm[ltc.id]=ltc;
      offset += 51;
    }
  }
}


// old version with ephemeris parsing
#if 0
void parseSBAS25H(int sv, const vector<uint8_t>& sbas, time_t t, ofstream& sbascsv, int offset, map<int, GPSState>* gpseph, const Point& src)
{
  bool velocity = getbitu(&sbas[0], offset, 1);
        
  if(velocity) { // 1 SV
    int slot = getbitu(&sbas[0], offset + 1, 6);
    int iod = getbitu(&sbas[0], offset + 7, 8);

    double dx = 0.125*getbits(&sbas[0], offset + 15, 11);
    double dy = 0.125*getbits(&sbas[0], offset + 26, 11);
    double dz = 0.125*getbits(&sbas[0], offset + 37, 11);
    double dai = 1000000000.0*ldexp(getbits(&sbas[0], offset + 48, 11), -31);
            
    double ddx = ldexp(getbits(&sbas[0], offset + 53, 8), -11);
    double ddy = ldexp(getbits(&sbas[0], offset  + 61, 8), -11);
    double ddz = ldexp(getbits(&sbas[0], offset  + 69, 8), -11);
    double ddai = 1000000000.0*ldexp(getbits(&sbas[0], offset + 77, 8), -39);
    
    int tvalid = 16 * getbitu(&sbas[0], offset+85, 13);
    if(tvalid) {
      sbascsv << std::fixed<< t <<" " << sv << " ";
      sbascsv << getSBASSV(sv, slot);
      cout << "("<<dx<<", "<<dy<<", "<<dz<<", "<<dai<<") -> ";
      cout << "("<<ddx<<", "<<ddy<<", "<<ddz<<", "<<1000000000*ddai<<") tvalid "<<tvalid<<"\n";

      
      sbascsv <<" " << iod<<" " << dx << " " << dy << " " << dz<<" " << dai;
      sbascsv << " " << ddx <<" " <<ddy <<" " << ddz<<" " << ddai;
      if(gpseph && gpseph->count(getSBASNumber(sv, slot))) {
        auto& gs = (*gpseph)[getSBASNumber(sv, slot)];
        if(gs.isComplete(gs.gpsiod)) {
          Point sat;
          getCoordinates(gs.tow, gs.iods[gs.gpsiod], &sat);
          sbascsv <<" " << sat.x<<" "<<sat.y<<" " << sat.z;
          double prerange = Vector(sat, src).length();
          sat.x += dx; sat.y += dy; sat.z += dz;
          double postrange = Vector(sat, src).length();
          sbascsv<<" " <<postrange - prerange;
        }
      }

      sbascsv<<"\n";
    }
  }
  else {
    int slot = getbitu(&sbas[0], offset + 1, 6);
    int iod = getbitu(&sbas[0], offset + 7, 8);
    
    sbascsv << std::fixed<< t <<" " << sv << " ";
    sbascsv << getSBASSV(sv, slot);
    cout<< getSBASSV(sv, slot);
    cout<<" IOD8 " <<iod<<" ";

    
    double dx = 0.125*getbits(&sbas[0], offset + 15, 9);
    double dy = 0.125*getbits(&sbas[0], offset + 24, 9);
    double dz = 0.125*getbits(&sbas[0], offset + 33, 9);
    double dai = 1000000000.0*ldexp(getbits(&sbas[0], offset + 42, 10), -31);
    cout << "("<<dx<<", "<<dy<<", "<<dz<<", "<< dai<<" ns)\n";
    sbascsv <<" " << iod<<" " << dx << " " << dy << " " << dz<<" " << dai;
    sbascsv <<" 0 0 0 0"; // no delta
    if(gpseph && gpseph->count(getSBASNumber(sv, slot))) {
      auto& gs = (*gpseph)[getSBASNumber(sv, slot)];
      if(gs.isComplete(gs.gpsiod)) {
        Point sat;
        getCoordinates(gs.tow, gs.iods[gs.gpsiod], &sat);
        sbascsv <<" " << sat.x<<" "<<sat.y<<" " << sat.z;
        double prerange = Vector(sat, src).length();
        sat.x += dx; sat.y += dy; sat.z += dz;
        double postrange = Vector(sat, src).length();
        sbascsv<<" " <<postrange - prerange;

      }
    }
    sbascsv <<'\n';
    
    offset += 51;

    slot = getbitu(&sbas[0], offset + 1, 6);
    iod = getbitu(&sbas[0], offset + 7, 8);
    
    sbascsv << t <<" " << sv << " ";
    sbascsv << getSBASSV(sv, slot);
    cout<< getSBASSV(sv, slot);
    cout<<" IOD8 " <<iod<<" ";

    
    dx = 0.125*getbits(&sbas[0], offset + 15, 9);
    dy = 0.125*getbits(&sbas[0], offset + 24, 9);
    dz = 0.125*getbits(&sbas[0], offset + 33, 9);
    dai = 1000000000.0*ldexp(getbits(&sbas[0], offset + 42, 10), -31);
    cout << "("<<dx<<", "<<dy<<", "<<dz<<", "<< dai<<" ns)\n";
    sbascsv << std::fixed <<" " << iod<<" " << dx << " " << dy << " " << dz<<" " << dai <<" 0 0 0 0";
    if(gpseph && gpseph->count(getSBASNumber(sv, slot))) {
      auto& gs = (*gpseph)[getSBASNumber(sv, slot)];
      if(gs.isComplete(gs.gpsiod)) {
        Point sat;
        getCoordinates(gs.tow, gs.iods[gs.gpsiod], &sat);
        sbascsv <<" " << sat.x<<" "<<sat.y<<" " << sat.z;
        double prerange = Vector(sat, src).length();
        sat.x += dx; sat.y += dy; sat.z += dz;
        double postrange = Vector(sat, src).length();
        sbascsv<<" " <<postrange - prerange;
      }
    }
    sbascsv <<'\n';
  }
}
#endif
