Architecture
------------

The galmon project consists of several components. Core of what we do are
protobuf messages that are (mostly) device independent and contain satellite
data and metadata.

All protobuf messages are 'NavMonMessages', of which there are 15 types. 
These messages are:

 * Navigation messages from satellites, various types
 * Device / software metadata
 * Reception strength details
 * Doppler, carrier phase data
 * RTCM messages

For every supported receiver type, there is a tool to convert the device
specific protocol to these protobuf messages. Currently we only have
'ubxtool' that does this for many u-blox devices. In addition, 'rtcmtool'
converts a RTCM to protobuf.

Components
----------

Core components:
 * ubxtool/rtcmtool: generate protobuf messages, transmit them over network
 * navrecv: receive protobuf messages over the network and store them on
   disk
 * navnexus: serve protobuf messages from disk over the network
 * navparse: consume protobuf and turn into a webserver with data, plus
   optionally fill an influxdb time-series database for graphing and analysis
   purposes.

This offers a complete suite for:

 * generating protobuf messages and transmitting them
 * reception & long-term storage
 * serving protobuf messages
 * analysing them for display & storing statistics for analysis


Non-core tools:
 * navdump: convert protobuf format data into a textual display of messages
 * navcat: serve protobuf messages from disk directly to stdout
 * reporter: make "the galmon.eu weekly galileo report", based on the
   time-series database filled by navparse
 * rinreport: rinex analysis tooling (not generically useful yet)
 * galmonmon: monitor a navparse instance for changes, tweet them out
 * navdisplay: some eye-candy that converts protobuf into a live display
   (not very good)

Transmission details
--------------------
Messages are sent as protobuf messages with an intervening magic value and a
length field. 

Both rtcmtool and ubxtool can send data over TCP/IP, but also to standard
output. This standard output mode makes it possible to pipe the output of
these tools straight into navdump or navparse. 

There are two transport protocols, one uncompressed, which consists of the 4
byte magic value, a 2 byte length field, and then the message. This protocol
has been deprecated for TCP, but it is still there for --stdout output.

The second protocol is zstd compressed, and features acknowledgements. The
protocol is an initial 4 byte magic value "RNIE", followed by 8 reserved
bytes which are currently ignored. Then zstd compression starts, each
message consists of a 4 byte message number, followed by a two byte length
field, followed by the message. There are no further magic values beyond the
first one.

The receiver is expected to return the 4 byte message number for each
message received. Messages which have not been acknowledged like this will
be retransmitted on reconnect.

Storage details
---------------
Storage is very simplistic but robust. For every receiver, for every hour,
there is a file with protobuf messages. That's it. The goal is for the
receiver to never ever go down.

navnexus and navcat can consume data from this simplistic storage and serve
it over TCP/IP or to standard output.

