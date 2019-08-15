# galmon
galileo/GPS open source monitoring

Theoretically multi-vendor, although currently only the U-blox 8 chipset is
supported.

Goals:

1) Support multiple wildly distributed receivers
2) Combine these into a forensic archive of all Galileo/GPS NAV messages
3) Make this archive available, offline and as a stream
4) Consume this stream and turn it into an attractive live website
   (https://galmon.eu/). As part of this, perform higher-level calculations
   to determine ephemeris discontinuities, live gst/gps/galileo time
   offsets, atomic clock jumps etc.
5) Populate an InfluxDB timeseries database with raw measurements and higher
   order calculations

To get started, make sure you have a C++17 compiler, git, protobuf-compiler,
libh2o-dev.

```
git clone https://github.com/ahupowerdns/galmon.git --recursive
cd galmon
make
```

If this doesn't succeed with an error about h2o, make sure you have this
library installed, and if you have remove the # in front of "# -lwslay".
This may solve the problem.

Once compiled, run for example ./ubxtool /dev/ttyACM0 1 | ./ubxparse 10000 html null

Next up, browse to http://[::1]:10000 (or try http://localhost:10000/ and
you should be in business. ubxtool changes (non-permanently) the
configuration of your u-blox receiver so it emits the required frames for
GPS and Galileo. If you have a u-blox timing receiver it will also enable
the doppler frames.

To see what is going on, try:

```
./ubxtool /dev/ttyACM0 1 | ./navdump
```

Setting up a distributed setup is slightly more complicated & may still
change.


Tooling:

 * ubxtool: can configure a u-blox 8 chipset, parses its output & will
   convert it into a protbuf stream of GNSS NAV frames + metadata
   Adds 64-bit timestamps plus origin information to each message
 * xtool: if you have another chipset, build something that extracts NAV
   frames & metadata. Not done yet.
 * navrecv: receives GNSS NAV frames and stores them on disk, split out per
   sender. 
 * navnexus: tails the files stored by navrecv, makes them available over
   TCP
 * navpartse: consumes these ordered nav updates for a nice website
   and puts "ready to graph" data in influxdb - this is the first
   step that breaks "store everything in native format". Also does
   computations on ephemerides. 
 * grafana dashboard: makes pretty graphs

The transport format consists of repeats of:

1) Four byte magic value
2) Two-byte frame length
3) A protobuf frame

The magic value is there to help us resync from partially written data.

The whole goal is that we can continue to rebuild the database by 
rerunning 'navstore' and 'navinflux'.

Big TODO
--------

 * Dual goals: completeness, liveness, not the same
   For forensics, great if the packet is there
   For display, not that bad if we missed a message
 * In general, consider refeed strategy
     Raw serial
     Protobuf
     Influxdb
     ".csv files"
 * Delivery needs to be bit more stateful (queue)
   
 * Semantics definition for output of Navnexus
   "we'll never surprise you with old data"

 

ubxtool
-------
 * Will also spool raw serial data to disk (in a filename that includes the
   start date)
 * Can also read from disk
 * Careful to add the right timestamps

