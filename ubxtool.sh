#!/bin/bash
runDir="/run/ubxtool"

CONSTELLATIONS="--galileo --gps --glonass"
# CONSTELLATIONS="--galileo --gps --beidou"
# CONSTELLATIONS="--galileo --gps --glonass --beidou"  # only on the F9P

# DEVICE="/dev/ttyACM0"		# comment out or leave blank to auto-search

#########################################################################
rotate() {
	logt=$1
	cd ${runDir}
	if [ -r ${logt}.log ];	# only rotate if there's a current logfile
	then
		if [ -r ${logt}.log.4 ]; then mv ${logt}.log.4 ${logt}.log.5 ; fi;
		if [ -r ${logt}.log.3 ]; then mv ${logt}.log.3 ${logt}.log.4 ; fi;
		if [ -r ${logt}.log.2 ]; then mv ${logt}.log.2 ${logt}.log.3 ; fi;
		if [ -r ${logt}.log.1 ]; then mv ${logt}.log.1 ${logt}.log.2 ; fi;
		if [ -r ${logt}.log   ]; then mv ${logt}.log   ${logt}.log.1 ; fi;
	fi
}

if [ -z "${DEVICE}" ];
then
	# programmatically find the interface
	SYSD=$(grep -il u-blox /sys/bus/usb/devices/*/manufacturer)
	SYSD=${SYSD//manufacturer/}
	DEVD=$(find ${SYSD} -type d -iname 'ttyACM*')
	DEVICE="/dev/${DEVD##*/}"
fi

DESTINATION=$(cat /usr/local/ubxtool/destination)
STATION=$(cat /usr/local/ubxtool/station)

# systemctl script will do this, but if you don't use systemctl, we need to take care of it
[[ -d ${runDir} ]] || mkdir -p ${runDir}
[[ -e ${runDir}/gps.sock ]] || mkfifo ${runDir}/gps.sock

for logFile in stdout stderr logfile
do
	rotate ${logFile}
done


exec /usr/local/ubxtool/ubxtool --wait ${CONSTELLATIONS} --port ${DEVICE} --station ${STATION} --destination ${DESTINATION} >> ${runDir}/stdout.log 2>> ${runDir}/stderr.log < /dev/null 
