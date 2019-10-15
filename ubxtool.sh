#!/bin/bash

DEVICE="/dev/ttyACM0"
DESTINATION="`cat /usr/local/ubxtool/destination`"

DEV="/run/ubxtool"

STATION="`/usr/local/ubxtool/station`"
CONSTELLATIONS="--galileo --gps --beidou --glonass"
CONSTELLATIONS="--galileo --gps --beidou"

(
mkdir ${DIR}
cd ${DIR}
mv stdout.log.4 stdout.log.5 ; mv stderr.log.4 stderr.log.5 ; mv logfile.4 logfile.5
mv stdout.log.3 stdout.log.4 ; mv stderr.log.3 stderr.log.4 ; mv logfile.3 logfile.4
mv stdout.log.2 stdout.log.3 ; mv stderr.log.2 stderr.log.3 ; mv logfile.2 logfile.3
mv stdout.log.1 stdout.log.2 ; mv stderr.log.1 stderr.log.2 ; mv logfile.1 logfile.2
mv stdout.log   stdout.log.1 ; mv stderr.log   stderr.log.1 ; mv logfile   logfile.1
) 2> /dev/null

exec /usr/local/ubxtool/ubxtool --wait ${CONSTELLATIONS} --port ${DEVICE} --station ${STATION} --destination ${DESTINATION} >> stdout.log 2>> stderr.log < /dev/null
