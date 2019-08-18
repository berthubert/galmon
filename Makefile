CXXFLAGS:= -std=gnu++17 -Wall -O3 -MMD -MP -ggdb -fno-omit-frame-pointer  -Iext/fmt-5.2.1/include/ -Iext/powerblog/ext/simplesocket -Iext/powerblog/ext/ -I/usr/local/include

PROGRAMS = navparse ubxtool navnexus navrecv navdump

all: navmon.pb.cc $(PROGRAMS)

-include *.d

clean:
	rm -f *~ *.o *.d ext/*/*.o $(PROGRAMS) navmon.pb.h navmon.pb.cc

H2OPP=ext/powerblog/h2o-pp.o
SIMPLESOCKETS=ext/powerblog/ext/simplesocket/swrappers.o ext/powerblog/ext/simplesocket/sclasses.o  ext/powerblog/ext/simplesocket/comboaddress.o 

navparse: navparse.o ext/fmt-5.2.1/src/format.o $(H2OPP) $(SIMPLESOCKETS) minicurl.o ubx.o bits.o navmon.pb.o gps.o ephemeris.o
	$(CXX) -std=gnu++17 $^ -o $@ -pthread -L/usr/local/lib -lh2o-evloop -lssl -lcrypto -lz  -lcurl -lprotobuf  # -lwslay

navdump: navdump.o ext/fmt-5.2.1/src/format.o bits.o navmon.pb.o gps.o ephemeris.o 
	$(CXX) -std=gnu++17 $^ -o $@ -pthread  -L/usr/local/lib -lprotobuf

navnexus: navnexus.o ext/fmt-5.2.1/src/format.o  $(SIMPLESOCKETS) ubx.o bits.o navmon.pb.o storage.o
	$(CXX) -std=gnu++17 $^ -o $@ -pthread -L/usr/local/lib -lprotobuf

navrecv: navrecv.o ext/fmt-5.2.1/src/format.o $(SIMPLESOCKETS) navmon.pb.o storage.o
	$(CXX) -std=gnu++17 $^ -o $@ -pthread -L/usr/local/lib -lprotobuf  


navmon.pb.cc: navmon.proto
	protoc --cpp_out=./ navmon.proto

ubxtool: navmon.pb.o ubxtool.o ubx.o bits.o ext/fmt-5.2.1/src/format.o galileo.o  gps.o
	$(CXX) -std=gnu++17 $^ -o $@ -L/usr/local/lib -lprotobuf

