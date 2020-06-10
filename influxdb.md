InfluxDB schema
---------------

Influxdb knows about *measurements* which live in a database. Each
measurement has multiple values. Such values can be tagged, and each set of
tags forms a series.

Our schema is not yet very consistent, but this documentation is a start.

Measurements
------------
These measurements are tagged by gnssid, sv and sigid. Sigid represents the
band on which this data was received.


 * ephemeris, updated every frame (so, a lot)
   * iod-live: current IOD number
   * eph-age: age of this ephemeris (distance from t0e)
 * sisa, updated every frame
   * value: raw Galileo SISA value
 * gpsura, updated every frame
   * value: raw GPS URA value
 * beidouurai, updated every frame
   * value: raw BeiDou URAI value (more or less same as GPS)
 * FT, GLONASS specific FT value (SISA)
 * clock, clock information, updated every frame
   * offset\_ns: time offset of this clock wrt GST/GPS time/Beidou time
   * t0c: t0 of the clock parameters
   * af0, af1, af2: clock polynomial parameters, in Galileo raw units, even for non-galileo SVs
 * clock\_jump\_ns
   * value: number of nanoseconds jump in clock correction from this
     ephemeris to the previous one
 * iono, ionospheric parameters
   * ai0, ai1, ai2: Galileo NeQuick parameters
   * sf1-sf5: The as yet unused 'storm flags'
 * galbgd, Galileo Broadcast Group Delay
   * BGDE1E5a in raw galileo values
   * BGDE1E5b in raw galileo values
 * galhealth, Galileo-specific health bits, values according to ICD
   * e1bhs
   * e5bhs
   * e1bdvs
   * e5bdvs
 * gpshealth, GPS-specific health bits
   * value
 * beidouhealth, BeiDou-specific health bits
   * sath1
 * glohealth, GLONASS-specific health bits
   * Bn
 * glo\_taun\_ns, GLONASS-specific TauN 
   * value, in nanoseconds
 * FT, GLONASS specific FT value
 * utcoffset, for GPS, Galileo, Beidou
   * a0, in Galileo units
   * a1, in Galileo units
   * delta, in nanoseconds
   * t0t, in seconds
 * gpsoffset, for Galileo, BeiDou does not fill this out. GPS doesn't need to
   * a0g, in Galileo units
   * a1g, in Galileo units
   * delta, in nanoseconds
   * t0g, in seconds
 * eph-disco, statistics about ephemeris transitions
   * x,y,z: ECEF coordinates according to new ephemeris at new t0e
   * oldx,oldy,oldz: ECEF coordinates according to old ephemeris at new t0e
   * iod, oldiod: new and old IOD

RTCM SSR corrections:

These measurements are tagged by gnssid, sv

  * rtcm-eph-correction:
    * iod: iod this correction corresponds to
    * radial: radial error (millimeters)
    * along: error along track
    * cross: error across track
    * dradial: velocity error in millimeters/second
    * dalong: along track velocity error
    * dcross: across track velocity error
    * ssr-iod
    * ssr-provider
    * ssr-solution
    * total-dist
    * tow
    * udi
  * rtcm-clock-correction
    * dclock0
    * dclock1
    * dclock2
    * ssr-iod
    * ssr-provider
    * tow
    * udi

Observer measurements:

 * fix
   * x,y,z: ECEF coordinates of receiver
   * lat, lon: degrees latitude and longitude
   * h: hight above WGS84 ellipsoid
   * acc: accuracy (meters)
   * groundspeed: m/s
 * observer\_details
   * clock\_offset\_ns: receiver reported internal clock offset
   * clock\_drift\_ns: drift rate, ns/s
   * clock\_acc\_ns: clock accuracy (ns)
   * freq\_acc\_ps: picosecond/s frequency accuracy
   * uptime: uptime in seconds


Observer and SV measurements:

 * rfdata
   * carrierphase
   * doppler (Hz)
   * locktime (milliseconds)
   * pseudorange (meters)
   * prstd (pseudorange standard deviation)
   * dostd (doppler standard deviation)
 * correlator
   * delta\_hz\_corr: Doppler residual against active ephemeris, corrected for
     receiver clock drift
   * delta\_hz: Doppler residual, uncorrected
   * elevation: Elevation of SV over horizon
   * prres: pseudorange residual according to receiver
   * qi: 0-7, quality indicator according to receiver
   * hz: Doppler Hz offset reported by receiver (uncorrected)
 * recdata
   * db: receiver reported dB (can be non-sensical)
   * azi: calculated azimuth for SV from this receiver
   * ele: calculated elevation for SV from this receiver
   * prres: pseudorange residual according to receiver
   * qi: 0-7, quality indicator according to receiver
 * ubx\_jamming
   * noise\_per\_ms: the Ublox noisePerMS field
   * agccnt: the Ublox automatic gain correction "count"
   * jamind: The Ublox jamming indicator
   * flag: The Ublox jamming flag field

