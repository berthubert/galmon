#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "ext/doctest.h"
#include "ephemeris.hh"

TEST_CASE("testing ephemeris age") {
    CHECK(ephAge(0,0) == 0);
    CHECK(ephAge(100,0) == 100);
    CHECK(ephAge(0,100) == -100);
    CHECK(ephAge(0,2*86400) == -2*86400);
    CHECK(ephAge(0,3*86400) == -3*86400);
    CHECK(ephAge(0, 3.49*86400) == -3.49*86400);    

    CHECK(ephAge(0, 3.51*86400) != -3.51*86400);
    CHECK(ephAge(0, 3.6*86400) != -3.6*86400);        


    CHECK(ephAge(2*86400, 0) == 2*86400);
    CHECK(ephAge(3*86400, 0) == 3*86400);    
    
    CHECK(ephAge(3.49*86400, 0) == 3.49*86400);        
}

#include "sp3.hh"
TEST_CASE("sp3") {
  SP3Reader sp3("./sp3/WUM0MGXULA_20192610100_01D_05M_ORB.SP3");
  SP3Entry e;
  CHECK(sp3.get(e));
  CHECK(e.gnss == 0);
  CHECK(e.sv == 1);
  CHECK(e.x ==-18824158.694000002  ) ;

  CHECK(sp3.get(e));
  CHECK(e.gnss == 0);
  CHECK(e.sv == 2);
  CHECK(e.clockBias == 1000.0 * -306.607761);
  
}
