# galmon
galileo/GPS/GLONASS/BeiDou open source monitoring. GPL3 licensed. 
(C) AHU Holding BV - bert@hubertnet.nl - https://berthub.eu/

Live website: https://galmon.eu/

Theoretically multi-vendor, although currently only the U-blox 8 and 9
chipsets are supported.  Navilock NL-8012U receiver works really well, as
does the U-blox evaluation kit for the 8MT.  In addition, many stations have
reported success with this very cheap [AliExpress sourced
device](https://www.aliexpress.com/item/32816656706.html).  The best and
most high-end receiver, which does all bands, all the time, is the Ublox
F9P, several of us use the
[ArdusimpleRTK2B](https://www.ardusimple.com/simplertk2b/) board.

Highlights
----------

 * Processes raw frames/strings/words from GPS, GLONASS, BeiDou and Galileo
 * All-band support (E1, E5b, B1I, B2I, Glonass L1, Glonass L2, GPS L1C/A)
   so far, GPS L2C and Galileo E5a pending).
 * Calculate ephemeris positions
 * Comparison of ephemerides to independent SP3 data to determine SISE
   * Globally, locally, worst user location
 * Record discontinuities between subsequent ephemerides (in time and space)
 * Compare doppler shift as reported by receiver with that expected from ephemeris
 * Track atomic clock & report jumps
 * Coverage maps (number of satellites >5, >10, >20 elevation)
 * HDOP/VDOP/PDOP maps
 * Compare orbit to TLE, match up to best matching satellite
 * Tear out every bit that tells us how well an SV is doing
 * Full almanac processing to see what _should_ be transmitting
 * Distributed receivers, combined into a single source of all messages
 * Ready to detect/report spoofing/jamming

Data is made available as JSON, as a user-friendly website and as a
time-series database. This time-series database is easily mated to the
industry standard Matplotlib/Pandas/Jupyter combination. 

There is also tooling to extract raw frames/strings/words from specific
timeframes.

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

Works on Linux (including Raspbian Buster on Pi Zero W), OSX and OpenBSD.

Build locally
-------------

To get started, make sure you have a C++17 compiler (like g++ 8 or higher),
git, protobuf-compiler.  Then run 'make ubxtool navdump' to build the
receiver-only tools.

To build everything, including the webserver, try:

```
apt-get install protobuf-compiler libh2o-dev libcurl4-openssl-dev libssl-dev libprotobuf-dev \ 
libh2o-evloop-dev libwslay-dev libncurses5-dev libeigen3-dev
git clone https://github.com/ahupowerdns/galmon.git --recursive
cd galmon
make
```

If this doesn't succeed with an error about h2o, make sure you have this
library installed. If you get an error about 'wslay', do the following, and run make again:

```
echo WSLAY=-lwslay > Makefile.local
```

Build in Docker
---------------

To build it in Docker:

```
git clone https://github.com/ahupowerdns/galmon.git --recursive
docker build -t galmon --build-arg MAKE_FLAGS=-j2 .
```

To run a container with a shell in there (this will also expose a port so you can view the UI too and assumes a ublox GPS device too - you may need to tweak as necessary):

```
docker run -it --rm --device=/dev/ttyACM0 -p 10000:10000 galmon
```


Running
-------

Once compiled, run for example `./ubxtool --wait --port /dev/ttyACM0
--station 1 --stdout --galileo | ./navparse 127.0.0.1:10000 html null`

Next up, browse to http://[::1]:10000 (or try http://localhost:10000/ and
you should be in business. ubxtool changes (non-permanently) the
configuration of your u-blox receiver so it emits the required frames for
GPS and Galileo. If you have a u-blox timing receiver it will also enable
the doppler frames.

By default the ublox receiver module will be configured to use the USB port,
if you want to use a different interface port on the ublox module then add
the `--ubxport <id>` option using one of the following numeric IDs:

   0 : DDC (aka. I2C)  
   1 : UART[1]  
   2 : UART2  
   3 : USB (default)  
   4 : SPI  

To see what is going on, try:

```
./ubxtool --wait --port /dev/ttyACM0 --station 1 --stdout --galileo | ./navdump
```

To distribute data to a remote `navrecv`, use:

```
./ubxtool --wait --port /dev/ttyACM0 --galileo --station 255 --dest 127.0.0.1
```

This will send protobuf to 127.0.0.1:29603. You can add as many destinations
as you want, they will buffer and automatically reconnect. To also send data
to stdout, add `--stdout`.

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
 * navparse: consumes these ordered nav updates for a nice website
   and puts "ready to graph" data in influxdb - this is the first
   step that breaks "store everything in native format". Also does
   computations on ephemerides. 
 * grafana dashboard: makes pretty graphs

Linux Systemd
-------------

This is very much a first stab. Do the following at root.
```
mkdir /run/ubxtool
mkdir /usr/local/ubxtool
cp ubxtool.sh /usr/local/ubxtool/
chmod +x /usr/local/ubxtool/ubxtool.sh
cp ubxtool /usr/local/ubxtool/
cp ubxtool.service /etc/systemd/system/ubxtool.service
touch /usr/local/ubxtool/destination
touch /usr/local/ubxtool/station
```
Then edit /usr/local/ubxtool/destination with an IP address collected from Bert.
Then edit /usr/local/ubxtool/station with a station number collected from Bert.

The start up the service.
```
sudo systemctl enable ubxtool
sudo systemctl start ubxtool
```
This will be cleaned up and better packaged sometime soon.

Distributed setup
-----------------
Run `navrecv :: ./storage` to receive frames on port 29603 of ::, aka all your IPv6 addresses (and IPv4 too on Linux).
This allows anyone to send you frames, so be aware.

Next up, run `navnexus ./storage ::`, which will serve your recorded data from port 29601. It will merge messages
coming in from all sources and serve them in time order.

Finally, you can do `nc 127.0.0.1 29601 | ./navdump`, which will give you all messages over the past 24 hours, and stream you more.
This also works for `navparse` for the pretty website and influx storage, `nc 127.0.0.1 29601 | ./navparse 127.0.0.0:10000 html galileo`,
if you have an influxdb running on localhost with a galileo database in there.

Internals
---------
The transport format consists of repeats of:

1) Four byte magic value
2) Two-byte frame length
3) A protobuf frame

The magic value is there to help us resync from partially written data.

The whole goal is that we can continue to rebuild the database by 
rerunning 'navstore' and 'navinflux'.

Documents
---------

 * [BeiDou](http://m.beidou.gov.cn/xt/gfxz/201902/P020190227593621142475.pdf)
 * [Galileo](https://www.gsc-europa.eu/sites/default/files/sites/all/files/Galileo-OS-SIS-ICD.pdf)
 * [GLONASS](https://www.unavco.org/help/glossary/docs/ICD_GLONASS_4.0_(1998)_en.pdf),
    old 1998 version, but unlike newer versions, this one is not full of
    mistakes. [New version](http://gauss.gge.unb.ca/GLONASS.ICD.pdf) is more complete but is worryingly messy.
 * [GLONASS CDMA](http://russianspacesystems.ru/wp-content/uploads/2016/08/ICD-GLONASS-CDMA-General.-Edition-1.0-2016.pdf)
   not actually relevant for the CDMA aspects, but has appendices on more
   precise orbit determinations.
 * [GPS](https://www.gps.gov/technical/icwg/IS-GPS-200K.pdf)
 * [U-blox 8 interface specification](https://www.u-blox.com/sites/default/files/products/documents/u-blox8-M8_ReceiverDescrProtSpec_%28UBX-13003221%29_Public.pdf)
 * [U-blox 9 interface specification](https://www.u-blox.com/sites/default/files/u-blox_ZED-F9P_InterfaceDescription_%28UBX-18010854%29.pdf)
 * [U-blox 9 integration manual](https://www.u-blox.com/sites/default/files/ZED-F9P_IntegrationManual_%28UBX-18010802%29.pdf)

Data sources
------------
The software can interpret SP3 files, good sources:

 * ESA/ESOC: http://navigation-office.esa.int/products/gnss-products/ - pick the
   relevant GPS week number, and then a series (.sp3 extension):
   * ESU = ultra rapid, 2-8h delay, only GPS and GLONASS
   * ESR = rapid, 2-26h delay, only GPS and GLONASS
   * ESM = finals, 6-13d delay, GPS, GLONASS, Galileo, BeiDou, QZSS
   * File format is esXWWWWD.sp3 - where X is U, R or M, WWWW is the
   (non-wrapping) GPS week number and D is day of week, Sunday is 0.
   * Further description: http://navigation-office.esa.int/GNSS_based_products.html
 * GFZ Potsdam: ftp://ftp.gfz-potsdam.de/GNSS/products/mgnss
   * The GBM series covers GPS, GLONASS, Galileo, BeiDou, QZSS and appears
     to have less of a delay than the ESA ESM series.
   * GBU = ultra rapid, still a few days delay, but much more recent.

Uncompress and concatenate all downloaded files into 'all.sp3' and run
'navdump' on collected protobuf, and it will output 'sp3.csv' with fit data.

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

Global coverage (via volunteers)
--------------------------------

In alphabetical order:

 * Austria (Vienna area)
 * Brazil
 * Holland (Nootdorp, Hilversum, etc)
 * India (New Delhi area)
 * Israel (Jerusalem)
 * New Zealand (Auckland area)
 * Rusia (Moscow area)
 * Singapore
 * South Africa (Cape Town area)
 * Spain
 * Tonga 
 * USA
   * Alaska (Anchorage)
   * California (Santa Cruz, Los Angeles area, etc)
   * Massachusetts (Boston area)
 * Uruguay
 
Additional sites are welcome (and encouraged) as the more data receiving sites that exist, then more accurate data and absolute coverage of each constellation can be had.

The galmon project is very grateful to all its volunteering receiving stations.

ubxtool
-------
 * Will also spool raw serial data to disk (in a filename that includes the
   start date)
 * Can also read from disk
 * Careful to add the right timestamps

