#pragma once
#include "minivec.hh"

void getCoordinates(int wn, double tow, const auto& iod, Point* p, bool quiet=true)
{
  using namespace std;
  // here goes

  constexpr double mu = 3.986004418 * pow(10.0, 14.0);
  constexpr double omegaE = 7.2921151467 * pow(10.0, -5);
  
  double sqrtA = 1.0*iod.sqrtA / (1ULL<<19);
  double deltan = M_PI * 1.0*iod.deltan / (1LL<<43);
  double t0e = iod.t0e; // t0e is PRE-SCALED
  double m0 = M_PI * 1.0*iod.m0 / (1LL<<31);
  double e = 1.0*iod.e / (1ULL<<33);
  double omega = M_PI * 1.0*iod.omega / (1LL<<31);

  double cuc = 1.0*iod.cuc / (1LL<<29);
  double cus = 1.0*iod.cus / (1LL<<29);

  double crc = 1.0*iod.crc / (1LL<<5);
  double crs = 1.0*iod.crs / (1LL<<5);

  double cic = 1.0*iod.cic / (1LL<<29);
  double cis = 1.0*iod.cis / (1LL<<29);

  double idot = M_PI * 1.0*iod.idot / (1LL<<43);
  double i0 = M_PI * 1.0*iod.i0 / (1LL << 31);

  double Omegadot = M_PI * 1.0*iod.omegadot / (1LL << 43);
  double Omega0 = M_PI * 1.0*iod.omega0 / (1LL << 31);
  
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
  double tk = tow - t0e; // in seconds, should do ephAge

  double n = n0 + deltan;
  if(!quiet)
    cerr <<"tk: "<<tk<<", n0: "<<n0<<", deltan: "<<deltan<<", n: "<<n<<endl;
  
  double M = m0 + n * tk;
  if(!quiet)
    cerr << " M = m0 + n * tk = "<<m0 << " + " << n << " * " << tk <<endl;
  double E = M;
  for(int k =0 ; k < 10; ++k) {
    if(!quiet)
      cerr<<"M = "<<M<<", E = "<< E << endl;
    E = M + e * sin(E);
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
    
    double nu2 = atan(   (sqrt(1-e*e) * sin(E)) /
                       (cos(E) - e)
                     );

    double nu3 = 2* atan( sqrt((1+e)/(1-e)) * tan(E/2));
    cerr << "e: "<<e<<", M: "<< M<<endl;
    cerr <<"         nu sis: "<<nu1<< " / +pi = " << nu1 +M_PI << endl;
    cerr <<"         nu ?: "<<nu2<< " / +pi = "  << nu2 +M_PI << endl;
    cerr <<"         nu fourier/esa: "<<nu2<< " + " << corr <<" = " << nu2 + corr<<endl;
    cerr <<"         nu wikipedia: "<<nu3<< " / +pi = " <<nu3 +M_PI << endl;
  }
  
  double nu = nu2 + corr;

  // https://en.wikipedia.org/wiki/True_anomaly is good
  
  double psi = nu + omega;
  if(!quiet) {
    cerr<<"psi = nu + omega = " << nu <<" + "<<omega<< " = " << psi << "\n";
  }


  double deltau = cus * sin(2*psi) + cuc * cos(2*psi);
  double deltar = crs * sin(2*psi) + crc * cos(2*psi);
  double deltai = cis * sin(2*psi) + cic * cos(2*psi);

  double u = psi + deltau;

  double r = A * (1- e * cos(E)) + deltar;

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
  
}

