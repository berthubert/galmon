CXXFLAGS:= -std=gnu++17 -Wall -O3 -MMD -MP -ggdb -fno-omit-frame-pointer  -Iext/fmt-5.2.1/include/ -Iext/powerblog/ext/simplesocket -Iext/powerblog/ext/

PROGRAMS = ubxparse ubxdisplay minread ubxtool

all: $(PROGRAMS)

-include *.d

clean:
	rm -f *~ *.o *.d ext/*/*.o $(PROGRAMS)

H2OPP=ext/powerblog/h2o-pp.o
SIMPLESOCKETS=ext/powerblog/ext/simplesocket/swrappers.o ext/powerblog/ext/simplesocket/sclasses.o  ext/powerblog/ext/simplesocket/comboaddress.o 
ubxparse: ubxparse.o ext/fmt-5.2.1/src/format.o $(H2OPP) $(SIMPLESOCKETS) minicurl.o ubx.o bits.o
	g++ -std=gnu++17 $^ -o $@ -pthread -lncurses -L/usr/local/lib -lh2o-evloop -lssl -lcrypto -lz  -lcurl

ubxdisplay: ubxdisplay.o ext/fmt-5.2.1/src/format.o
	g++ -std=gnu++17 $^ -o $@ -pthread -lncurses

ubxtool: ubxtool.o ubx.o ext/fmt-5.2.1/src/format.o
	g++ -std=gnu++17 $^ -o $@ 

