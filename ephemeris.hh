#pragma once
#include "minivec.hh"
#include <iostream>
#include <tuple>
#include <stdint.h>
struct GPSLikeEphemeris
{
  virtual double getMu() const = 0;
  virtual double getOmegaE()    const = 0; 
  virtual double getE() const = 0;
  virtual uint32_t getT0e() const = 0;

  virtual double getI0() const = 0;

  virtual double getOmegadot() const = 0;

  virtual double getSqrtA() const = 0;
  virtual double getOmega0() const = 0;
  virtual double getOmega() const = 0;

  virtual double getM0() const = 0;
  virtual double getIdot()      const = 0;
  virtual double getCic()       const = 0;
  virtual double getCis()       const = 0;
  virtual double getCuc()   const = 0;
  virtual double getCus()   const = 0;
  virtual double getCrc()   const = 0;
  virtual double getCrs()   const = 0;
  virtual double getDeltan()const = 0;

  virtual int getIOD() const = 0;
  // maybe af0, af1, af2?
  // maybe getUTCOffset? getAtomicOffset etc
  
};

// lat, lon, height (rad, rad, meters)
std::tuple<double, double, double> ecefToWGS84(double x, double y, double z);

// lat, lon, height (deg, deg, meters)
inline std::tuple<double, double, double> ecefToWGS84Deg(double x, double y, double z)
{
  auto ret = ecefToWGS84(x, y, z);
  std::get<0>(ret) /= (M_PI / 180);
  std::get<1>(ret) /= (M_PI / 180);
  return ret;
}


double ephAge(double tow, int t0e);

template<typename T>
double getCoordinates(double tow, const T& iod, Point* p, bool quiet=true)
{
  using namespace std;
  // here goes

  const double mu = iod.getMu(); 
  const double omegaE = iod.getOmegaE();
  
  const double sqrtA = iod.getSqrtA(); 
  const double deltan = iod.getDeltan(); 
  const double t0e = iod.getT0e(); 
  const double m0 = iod.getM0();
  const double e = iod.getE();
  const double omega = iod.getOmega();

  const double cuc = iod.getCuc();
  const double cus = iod.getCus();

  const double crc = iod.getCrc();
  const double crs = iod.getCrs();

  const double cic = iod.getCic();
  const double cis = iod.getCis();

  const double idot = iod.getIdot();
  const double i0 = iod.getI0();

  const double Omegadot = iod.getOmegadot();
  const double Omega0 = iod.getOmega0();
  
  // NO IOD BEYOND THIS POINT!
  if(!quiet) {
    auto todeg = [](double rad)
    {
      return 360 * rad/(2*M_PI);
    };
    
    cerr << "sqrtA = "<< sqrtA << endl;
    cerr << "deltan = "<< deltan << endl;
    cerr << "t0e = "<< t0e << endl;
    cerr << "m0 = "<< m0 << " ("<<todeg(m0)<<")"<<endl;
    cerr << "e = "<< e << endl;
    cerr << "omega = " << omega << " ("<<todeg(omega)<<")"<<endl;
    cerr << "idot = " << idot <<endl;
    cerr << "i0 = " << i0 << " ("<<todeg(i0)<<")"<<endl;
    
    cerr << "cuc = " << cuc << endl;
    cerr << "cus = " << cus << endl;
    
    cerr << "crc = " << crc << endl;
    cerr << "crs = " << crs << endl;
    
    cerr << "cic = " << cic << endl;
    cerr << "cis = " << cis << endl;

    cerr << "Omega0 = " << Omega0 << " ("<<todeg(Omega0)<<")"<<endl;
    cerr << "Omegadot = " << Omegadot << " ("<<todeg(Omegadot)<< ")"<<endl;
    
  }
  
  double A = pow(sqrtA, 2.0);
  double A3 = pow(sqrtA, 6.0);

  double n0 = sqrt(mu/A3);
  double tk = ephAge(tow, t0e); 

  double n = n0 + deltan;
  if(!quiet)
    cerr <<"tk: "<<tk<<", n0: "<<n0<<", deltan: "<<deltan<<", n: "<<n<<endl;
  
  double M = m0 + n * tk;
  if(!quiet)
    cerr << " M = m0 + n * tk = "<<m0 << " + " << n << " * " << tk <<endl;
  double E = M;
  double newE;
  for(int k =0 ; k < 10; ++k) {
    newE = M + e * sin(E);
    if(!quiet)
      cerr<<"k "<<k<<" M = "<<M<<", E = "<< E << ", delta: "<< (E-newE) << endl;

    if(fabs(E-newE) < 0.0000001) {
      E = newE;
      break;
    }
    E = newE;
  }

  // M = E  - e * sin(E)   -> E = M + e * sin(E)
  double nu2 = M + e*2*sin(M) +
    e *e * (5.0/4.0) * sin(2*M) -
    e*e*e * (0.25*sin(M) - (13.0/12.0)*sin(3*M));
  double corr = e*e*e*e * (103*sin(4*M) - 44*sin(2*M)) / 96.0 +
    e*e*e*e*e * (1097*sin(5*M) - 645*sin(3*M) + 50 *sin(M))/960.0 +
    e*e*e*e*e*e * (1223*sin(6*M) - 902*sin(4*M) + 85 *sin(2*M))/960.0;

  if(!quiet) {
    double nu1 = atan(   ((sqrt(1-e*e)  * sin(E)) / (1 - e * cos(E)) ) /
                        ((cos(E) - e)/ (1-e*cos(E)))
                        );
    
    double nu2A = atan(   (sqrt(1-e*e) * sin(E)) /
                       (cos(E) - e)
                     );


    double nu2B = atan2(   (sqrt(1-e*e) * sin(E)) ,
                       (cos(E) - e)
                     );

    
    double nu3 = 2* atan( sqrt((1+e)/(1-e)) * tan(E/2));
    cerr << "e: "<<e<<", M: "<< M<<", E: "<< E<<endl;
    cerr <<"         nu sis: "<<nu1<< " / +pi = " << nu1 +M_PI << endl;
    cerr <<"         nu ?: "<<nu2A<< " / +pi = "  << nu2A +M_PI << endl;
    cerr <<"         nu ?: "<<nu2B<< " / +pi = "  << nu2B +M_PI << endl;
    cerr <<"*        nu fourier/esa: "<<nu2<< " + " << corr <<" = " << nu2 + corr<<" | "<< std::fixed<<nu2+corr-nu1<<endl;
    cerr <<"         nu wikipedia: "<<nu3<< " / +pi = " <<nu3 +M_PI << endl;
  }
  double nu = atan2(   (sqrt(1-e*e) * sin(E)) ,
                         (cos(E) - e)
                     );


  // https://en.wikipedia.org/wiki/True_anomaly is good
  
  double psi = nu + omega;
  if(!quiet) {
    cerr<<"psi = nu + omega = " << nu <<" + "<<omega<< " = " << psi << "\n";
  }


  double deltau = cus * sin(2*psi) + cuc * cos(2*psi);
  double deltar = crs * sin(2*psi) + crc * cos(2*psi);
  double deltai = cis * sin(2*psi) + cic * cos(2*psi);

  double u = psi + deltau;

  double r = A * (1 - e * cos(E)) + deltar;

  double xprime = r*cos(u), yprime = r*sin(u);
  if(!quiet) {
    cerr<<"u = psi + deltau = "<< psi <<" + " << deltau << " = "<<u<<"\n";
    cerr << "calculated r = "<< r << " (" << (r/1000.0) <<"km)"<<endl;
    cerr << "xprime: "<<xprime<<", yprime: "<<yprime<<endl;
  }
  double Omega = Omega0 + (Omegadot - omegaE)*tk - omegaE * t0e;
  double i = i0 + deltai + idot * tk;
  p->x = xprime * cos(Omega) - yprime * cos(i) * sin(Omega);
  p->y = xprime * sin(Omega) + yprime * cos(i) * cos(Omega);
  p->z = yprime * sin(i);

  if(!quiet) {
    Point core(0.0, .0, .0);
    Vector radius(core, *p);
    cerr << radius.length() << " calculated r "<<endl;
  }
  return E;
}

struct DopplerData
{
  double preddop;
  double radvel;
  Vector speed;
  Point sat;
  double ephage;
  time_t t;
};

template<typename T>
void getSpeed(double tow, const T& eph, Vector* v)
{
  Point a, b;
  getCoordinates(tow-0.5, eph, &a);
  getCoordinates(tow+0.5, eph, &b);
  *v = Vector(a, b);
}

template<typename T>
DopplerData doDoppler(double tow, const Point& us, const T& eph, double freq)
{
  DopplerData ret;
  
  // be careful with time here - we need to evaluate at the timestamp of this RFDataType update
  // which might be newer than .tow in g_svstats
  getCoordinates(tow, eph, &ret.sat);
  Point core;
  Vector us2sat(us, ret.sat);
  getSpeed(tow, eph, &ret.speed);
  Vector core2us(core, us);
  Vector dx(us, ret.sat); //  = x-ourx, dy = y-oury, dz = z-ourz;
        
  us2sat.norm();
  ret.radvel=us2sat.inner(ret.speed);
  double c=299792458;
  ret.preddop = -freq * ret.radvel/c;
        
  // be careful with time here - 
  ret.ephage = ephAge(tow, eph.getT0e());
        //        cout<<"Radial velocity: "<< radvel<<", predicted doppler: "<< preddop << ", measured doppler: "<<nmm.rfd().doppler()<<endl;


  return ret;
}

std::pair<double,double> getLongLat(double x, double y, double z);
double getElevationDeg(const Point& sat, const Point& our);
double getAzimuthDeg(const Point& sat, const Point& our);
