CXXFLAGS:= -std=gnu++17 -Wall -O3 -MMD -MP -ggdb -fno-omit-frame-pointer  -Iext/fmt-5.2.1/include/ -Iext/powerblog/ext/simplesocket -Iext/powerblog/ext/

PROGRAMS = navparse ubxtool navnexus navrecv

all: $(PROGRAMS)

-include *.d

clean:
	rm -f *~ *.o *.d ext/*/*.o $(PROGRAMS)

H2OPP=ext/powerblog/h2o-pp.o
SIMPLESOCKETS=ext/powerblog/ext/simplesocket/swrappers.o ext/powerblog/ext/simplesocket/sclasses.o  ext/powerblog/ext/simplesocket/comboaddress.o 

navparse: navparse.o ext/fmt-5.2.1/src/format.o $(H2OPP) $(SIMPLESOCKETS) minicurl.o ubx.o bits.o navmon.pb.o
	g++ -std=gnu++17 $^ -o $@ -pthread -L/usr/local/lib -lh2o-evloop -lssl -lcrypto -lz  -lcurl -lprotobuf  # -lwslay

navnexus: navnexus.o ext/fmt-5.2.1/src/format.o  $(SIMPLESOCKETS) ubx.o bits.o navmon.pb.o storage.o
	g++ -std=gnu++17 $^ -o $@ -pthread -lprotobuf

navrecv: navrecv.o ext/fmt-5.2.1/src/format.o $(SIMPLESOCKETS) navmon.pb.o storage.o
	g++ -std=gnu++17 $^ -o $@ -pthread -lprotobuf  




navmon.pb.h: navmon.proto
	protoc --cpp_out=./ navmon.proto

ubxtool: ubxtool.o ubx.o bits.o ext/fmt-5.2.1/src/format.o galileo.o navmon.pb.o 
	g++ -std=gnu++17 $^ -o $@ -lprotobuf

