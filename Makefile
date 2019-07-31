CXXFLAGS:= -std=gnu++17 -Wall -O3 -MMD -MP -ggdb -fno-omit-frame-pointer  -Iext/fmt-5.2.1/include/

PROGRAMS = ubxparse ubxdisplay minread

all: $(PROGRAMS)

-include *.d

clean:
	rm -f *~ *.o *.d ext/*/*.o $(PROGRAMS)

ubxparse: ubxparse.o ext/fmt-5.2.1/src/format.o
	g++ -std=gnu++17 $^ -o $@ -pthread -lncurses

ubxdisplay: ubxdisplay.o ext/fmt-5.2.1/src/format.o
	g++ -std=gnu++17 $^ -o $@ -pthread -lncurses

