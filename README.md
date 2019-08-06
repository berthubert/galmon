# galmon
galileo open source monitoring

Tooling:

 * ubxtool: can configure a u-blox 8 chipset, parses its output & will
   convert it into a protbuf stream of GNSS NAV frames + metadata
   Adds 64-bit timestamps plus origin information to each message
 * xtool: if you have another chipset, build something that extracts NAV
   frames & metadata
 * navtrans: transmits GNSS NAV frames emitted by ubxtool to a collector.
   Performs some best effort buffering & will reconnect if needed.
 * navrecv: receives GNSS NAV frames and stores them on disk, split out per
   sender
 * navstore: tails the file stored by navrecv, puts them in LMDB
 * navstream: produces a stream of NAV updates from all sources, with a few
   seconds delay so all data is in. Does this with queries to LMDB
 * navweb: consumes these ordered nav updates for a nice website
 * navinflux: puts "ready to graph" data in influxdb - this is the first
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

