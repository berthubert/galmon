CFLAGS = -O3 -Wall -ggdb 

CXXFLAGS:= -std=gnu++17 -Wall -O0 -MMD -MP -fno-omit-frame-pointer -Iext/CLI11 \
	 -Iext/fmt-5.2.1/include/ -Iext/powerblog/ext/simplesocket -Iext/powerblog/ext/ \
	 -I/usr/local/opt/openssl/include/  \
	 -Iext/sgp4/libsgp4/ \
	 -I/usr/local/include

# CXXFLAGS += -Wno-delete-non-virtual-dtor

ifneq (,$(wildcard ubxsec.c))
	EXTRADEP = ubxsec.o
else ifneq (,$(wildcard ubxsec.o))
	EXTRADEP = ubxsec.o
endif


CHEAT_ARG := $(shell ./update-git-hash-if-necessary)

PROGRAMS = navparse ubxtool navnexus navcat navrecv navdump testrunner navdisplay tlecatch reporter \
	galmonmon

all: navmon.pb.cc $(PROGRAMS)

-include Makefile.local

-include *.d

H2OPP=ext/powerblog/h2o-pp.o
SIMPLESOCKETS=ext/powerblog/ext/simplesocket/swrappers.o ext/powerblog/ext/simplesocket/sclasses.o  ext/powerblog/ext/simplesocket/comboaddress.o 

clean:
	rm -f *~ *.o *.d ext/*/*.o $(PROGRAMS) navmon.pb.h navmon.pb.cc $(patsubst %.cc,%.o,$(wildcard ext/sgp4/libsgp4/*.cc)) $(H2OPP) $(SIMPLESOCKETS)
	rm -f ext/fmt-5.2.1/src/format.o

decrypt: decrypt.o bits.o ext/fmt-5.2.1/src/format.o
	$(CXX) -std=gnu++17 $^ -o $@ 

navparse: navparse.o ext/fmt-5.2.1/src/format.o $(H2OPP) $(SIMPLESOCKETS) minicurl.o ubx.o bits.o navmon.pb.o gps.o ephemeris.o beidou.o glonass.o $(patsubst %.cc,%.o,$(wildcard ext/sgp4/libsgp4/*.cc)) tle.o navmon.o coverage.o osen.o trkmeas.o influxpush.o ${EXTRADEP} githash.o
	$(CXX) -std=gnu++17 $^ -o $@ -pthread -L/usr/local/lib -L/usr/local/opt/openssl/lib/  -lh2o-evloop -lssl -lcrypto -lz  -lcurl -lprotobuf  $(WSLAY)

reporter: reporter.o ext/fmt-5.2.1/src/format.o $(SIMPLESOCKETS) minicurl.o ubx.o bits.o navmon.pb.o gps.o ephemeris.o beidou.o glonass.o $(patsubst %.cc,%.o,$(wildcard ext/sgp4/libsgp4/*.cc)) tle.o navmon.o coverage.o osen.o githash.o
	$(CXX) -std=gnu++17 $^ -o $@ -pthread -L/usr/local/lib -lprotobuf -lcurl

galmonmon: galmonmon.o ext/fmt-5.2.1/src/format.o $(SIMPLESOCKETS) minicurl.o ubx.o bits.o navmon.pb.o gps.o ephemeris.o beidou.o glonass.o $(patsubst %.cc,%.o,$(wildcard ext/sgp4/libsgp4/*.cc)) tle.o navmon.o coverage.o osen.o githash.o
	$(CXX) -std=gnu++17 $^ -o $@ -pthread -L/usr/local/lib -lprotobuf -lcurl


navdump: navdump.o ext/fmt-5.2.1/src/format.o bits.o navmon.pb.o gps.o ephemeris.o beidou.o glonass.o navmon.o $(patsubst %.cc,%.o,$(wildcard ext/sgp4/libsgp4/*.cc)) tle.o sp3.o osen.o trkmeas.o githash.o ${EXTRADEP}
	$(CXX) -std=gnu++17 $^ -o $@ -L/usr/local/lib -pthread  -lprotobuf

navdisplay: navdisplay.o ext/fmt-5.2.1/src/format.o bits.o navmon.pb.o gps.o ephemeris.o beidou.o glonass.o ephemeris.o navmon.o osen.o githash.o
	$(CXX) -std=gnu++17 $^ -o $@ -L/usr/local/lib -pthread  -lprotobuf -lncurses


navnexus: navnexus.o ext/fmt-5.2.1/src/format.o  $(SIMPLESOCKETS) ubx.o bits.o navmon.pb.o storage.o githash.o
	$(CXX) -std=gnu++17 $^ -o $@ -L/usr/local/lib -pthread -lprotobuf

navcat: navcat.o ext/fmt-5.2.1/src/format.o  $(SIMPLESOCKETS) ubx.o bits.o navmon.pb.o storage.o navmon.o githash.o
	$(CXX) -std=gnu++17 $^ -o $@ -L/usr/local/lib -pthread -lprotobuf


navrecv: navrecv.o ext/fmt-5.2.1/src/format.o $(SIMPLESOCKETS) navmon.pb.o storage.o githash.o
	$(CXX) -std=gnu++17 $^ -o $@ -L/usr/local/lib -pthread -lprotobuf  

tlecatch: tlecatch.o $(patsubst %.cc,%.o,$(wildcard ext/sgp4/libsgp4/*.cc)) githash.o
	$(CXX) -std=gnu++17 $^ -o $@ -L/usr/local/lib -pthread -lprotobuf  

navmon.pb.cc: navmon.proto
	protoc --cpp_out=./ navmon.proto

ubxtool: navmon.pb.o ubxtool.o ubx.o bits.o ext/fmt-5.2.1/src/format.o galileo.o  gps.o beidou.o navmon.o ephemeris.o $(SIMPLESOCKETS) osen.o githash.o
	$(CXX) -std=gnu++17 $^ -o $@ -L/usr/local/lib -lprotobuf -pthread

testrunner: navmon.pb.o testrunner.o ubx.o bits.o ext/fmt-5.2.1/src/format.o galileo.o  gps.o beidou.o ephemeris.o sp3.o osen.o navmon.o
	$(CXX) -std=gnu++17 $^ -o $@ -L/usr/local/lib -lprotobuf

check: testrunner
	./testrunner
