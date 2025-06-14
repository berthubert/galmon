CFLAGS = -O3 -Wall -ggdb 

CXXFLAGS:= -std=gnu++17 -Wall -O3 -ggdb -MMD -MP -fno-omit-frame-pointer -Iext/CLI11 \
	 -Iext/powerblog/ext/simplesocket -Iext/powerblog/ext/ \
	 -I/usr/local/opt/openssl/include/  \
	 -Iext/sgp4/libsgp4/ \
	 -I/usr/local/include

# CXXFLAGS += -Wno-delete-non-virtual-dtor

# If unset, create a variable for the path or binary to use as "install" for debuild.
INSTALL ?= install
# If unset, create a variable with the path used by "make install"
prefix ?= /usr/local/ubxtool
# If unset, create a variable for a path underneath $prefix that stores html files
htdocs ?= /share/package

ifneq (,$(wildcard ubxsec.c))
	EXTRADEP = ubxsec.o
else ifneq (,$(wildcard ubxsec.o))
	EXTRADEP = ubxsec.o
endif

FMT=ext/fmt-9.1.0

CHEAT_ARG := $(shell ./update-git-hash-if-necessary)

PROGRAMS = navparse ubxtool navnexus navcat navrecv navdump testrunner navdisplay tlecatch reporter sp3feed \
	galmonmon rinreport rinjoin rtcmtool gndate septool navmerge

all: navmon.pb.cc $(PROGRAMS)

-include Makefile.local

-include *.d

navmon.pb.h: navmon.proto
	protoc --cpp_out=./ navmon.proto

navmon.pb.cc: navmon.proto
	protoc --cpp_out=./ navmon.proto


H2OPP=ext/powerblog/h2o-pp.o
SIMPLESOCKETS=ext/powerblog/ext/simplesocket/swrappers.o ext/powerblog/ext/simplesocket/sclasses.o  ext/powerblog/ext/simplesocket/comboaddress.o 


clean:
	rm -f *~ *.o *.d ext/*/*.o ext/*/*.d $(PROGRAMS) navmon.pb.h navmon.pb.cc $(patsubst %.cc,%.o,$(wildcard ext/sgp4/libsgp4/*.cc)) $(H2OPP) $(SIMPLESOCKETS)
	rm -f ${FMT}/src/format.[do] ext/sgp4/libsgp4/*.d ext/powerblog/ext/simplesocket/*.d

help2man:
	$(INSTALL) -m 755 -d $(DESTDIR)$(prefix)/share/man/man1
	HELP2MAN_DESCRIPTION="Open-source GNSS Monitoring Project"
	$(foreach binaryfile,$(PROGRAMS),help2man -N -n "$(HELP2MAN_DESCRIPTION)" ./$(binaryfile) | gzip > $(DESTDIR)$(prefix)/share/man/man1/$(binaryfile).1.gz;)
	@echo until these binaries support --help and --version remove the broken output
	rm -f $(DESTDIR)$(prefix)/share/man/man1/rinreport.1.gz
	rm -f $(DESTDIR)$(prefix)/share/man/man1/rtcmtool.1.gz
	rm -f $(DESTDIR)$(prefix)/share/man/man1/testrunner.1.gz

install: $(PROGRAMS) help2man
	$(INSTALL) -m 755 -d $(DESTDIR)$(prefix)/bin
	$(foreach binaryfile,$(PROGRAMS),$(INSTALL) -s -m 755 -D ./$(binaryfile) $(DESTDIR)$(prefix)/bin/$(binaryfile);)
	@echo "using cp instead of install because recursive directories of ascii"
	mkdir -p $(DESTDIR)$(prefix)$(htdocs)/galmon
	cp -a html $(DESTDIR)$(prefix)$(htdocs)/galmon/

download-debian-package:
	apt-key adv --fetch-keys https://ota.bike/public-package-signing-keys/86E7F51C04FBAAB0.asc
	echo "deb https://ota.bike/debian/ buster main" > /etc/apt/sources.list.d/galmon.list
	apt-get update && apt-get install -y galmon

download-raspbian-package:
	apt-key adv --fetch-keys https://ota.bike/public-package-signing-keys/86E7F51C04FBAAB0.asc
	echo "deb https://ota.bike/raspbian/ buster main" > /etc/apt/sources.list.d/galmon.list
	apt-get update && apt-get install -y galmon

decrypt: decrypt.o bits.o ${FMT}/src/format.o
	$(CXX) -std=gnu++17 $^ -o $@ 

navparse: navparse.o $(H2OPP) $(SIMPLESOCKETS) minicurl.o ubx.o bits.o navmon.pb.o gps.o ephemeris.o beidou.o glonass.o $(patsubst %.cc,%.o,$(wildcard ext/sgp4/libsgp4/*.cc)) tle.o navmon.o coverage.o osen.o trkmeas.o influxpush.o ${EXTRADEP} githash.o sbas.o rtcm.o galileo.o
	$(CXX) -std=gnu++17 $^ -o $@ -pthread -L/usr/local/lib -L/usr/local/opt/openssl/lib/  -lh2o-evloop -lssl -lcrypto -lz  -lcurl -lprotobuf  $(WSLAY) -lfmt

reporter: reporter.o  $(SIMPLESOCKETS) minicurl.o ubx.o bits.o navmon.pb.o gps.o ephemeris.o beidou.o glonass.o $(patsubst %.cc,%.o,$(wildcard ext/sgp4/libsgp4/*.cc)) tle.o navmon.o coverage.o osen.o githash.o influxpush.o 
	$(CXX) -std=gnu++17 $^ -o $@ -pthread -L/usr/local/lib -lprotobuf -lcurl -lfmt

sp3feed: sp3feed.o  $(SIMPLESOCKETS) minicurl.o ubx.o bits.o navmon.pb.o gps.o ephemeris.o beidou.o glonass.o $(patsubst %.cc,%.o,$(wildcard ext/sgp4/libsgp4/*.cc)) tle.o navmon.o coverage.o osen.o influxpush.o githash.o sp3.o
	$(CXX) -std=gnu++17 $^ -o $@ -pthread -L/usr/local/lib -lprotobuf -lcurl -lfmt


tracker: tracker.o  $(SIMPLESOCKETS) minicurl.o ubx.o bits.o navmon.pb.o gps.o ephemeris.o beidou.o glonass.o $(patsubst %.cc,%.o,$(wildcard ext/sgp4/libsgp4/*.cc)) tle.o navmon.o coverage.o osen.o githash.o
	$(CXX) -std=gnu++17 $^ -o $@ -pthread -L/usr/local/lib -lprotobuf -lcurl -lfmt


galmonmon: galmonmon.o  $(SIMPLESOCKETS) minicurl.o ubx.o bits.o navmon.pb.o gps.o ephemeris.o beidou.o glonass.o $(patsubst %.cc,%.o,$(wildcard ext/sgp4/libsgp4/*.cc)) tle.o navmon.o coverage.o osen.o githash.o
	$(CXX) -std=gnu++17 $^ -o $@ -pthread -L/usr/local/lib -lprotobuf -lcurl -lfmt


# rs.o fixhunter.o
navdump: navdump.o  ${FMT}/src/os.o bits.o navmon.pb.o gps.o ephemeris.o beidou.o glonass.o navmon.o $(patsubst %.cc,%.o,$(wildcard ext/sgp4/libsgp4/*.cc)) tle.o sp3.o osen.o trkmeas.o githash.o rinex.o sbas.o rtcm.o galileo.o  ${EXTRADEP}
	$(CXX) -std=gnu++17 $^ -o $@ -L/usr/local/lib -pthread  -lprotobuf -lz  -lfmt
# -lfec

navdisplay: navdisplay.o  bits.o navmon.pb.o gps.o ephemeris.o beidou.o glonass.o ephemeris.o navmon.o osen.o githash.o
	$(CXX) -std=gnu++17 $^ -o $@ -L/usr/local/lib -pthread  -lprotobuf -lncurses -lfmt


navnexus: navnexus.o   $(SIMPLESOCKETS) bits.o navmon.pb.o storage.o githash.o
	$(CXX) -std=gnu++17 $^ -o $@ -L/usr/local/lib -pthread -lprotobuf -lfmt

navcat: navcat.o   $(SIMPLESOCKETS) ubx.o bits.o navmon.pb.o storage.o navmon.o githash.o
	$(CXX) -std=gnu++17 $^ -o $@ -L/usr/local/lib -pthread -lprotobuf -lfmt


navrecv: navrecv.o  $(SIMPLESOCKETS) navmon.pb.o storage.o githash.o zstdwrap.o navmon.o
	$(CXX) -std=gnu++17 $^ -o $@ -L/usr/local/lib -pthread -lprotobuf -lzstd -lfmt 

navmerge: navmerge.o  $(SIMPLESOCKETS) navmon.pb.o storage.o githash.o zstdwrap.o navmon.o nmmsender.o
	$(CXX) -std=gnu++17 $^ -o $@ -L/usr/local/lib -pthread -lprotobuf -lzstd -lfmt 


tlecatch: tlecatch.o $(patsubst %.cc,%.o,$(wildcard ext/sgp4/libsgp4/*.cc)) githash.o
	$(CXX) -std=gnu++17 $^ -o $@ -L/usr/local/lib -pthread -lprotobuf  

rinreport: rinreport.o rinex.o githash.o navmon.o   ephemeris.o osen.o
	$(CXX) -std=gnu++17 $^ -o $@ -lz -pthread -lfmt

rinjoin: rinjoin.o rinex.o githash.o navmon.o   ephemeris.o osen.o
	$(CXX) -std=gnu++17 $^ -o $@ -lz -pthread -lfmt


rtcmtool: rtcmtool.o navmon.pb.o githash.o   bits.o nmmsender.o $(SIMPLESOCKETS)  navmon.o rtcm.o zstdwrap.o
	$(CXX) -std=gnu++17 $^ -o $@ -L/usr/local/lib -lz -pthread -lprotobuf -lzstd -lfmt


ubxtool: navmon.pb.o ubxtool.o ubx.o bits.o  galileo.o  gps.o beidou.o navmon.o ephemeris.o $(SIMPLESOCKETS) osen.o githash.o nmmsender.o zstdwrap.o 
	$(CXX) -std=gnu++17 $^ -o $@ -L/usr/local/lib -lprotobuf -pthread -lzstd -lfmt 

septool: navmon.pb.o septool.o bits.o  galileo.o  gps.o beidou.o navmon.o ephemeris.o $(SIMPLESOCKETS) osen.o githash.o nmmsender.o zstdwrap.o 
	$(CXX) -std=gnu++17 $^ -o $@ -L/usr/local/lib -lprotobuf -pthread -lzstd -lfmt	


testrunner: navmon.pb.o testrunner.o ubx.o bits.o  galileo.o  gps.o beidou.o ephemeris.o sp3.o osen.o navmon.o rinex.o githash.o
	$(CXX) -std=gnu++17 $^ -o $@ -L/usr/local/lib -lprotobuf -lz  -pthread -lfmt

gndate: gndate.o githash.o  navmon.o
	$(CXX) -std=gnu++17 $^ -o $@ -L/usr/local/lib -lfmt

check: testrunner
	./testrunner
