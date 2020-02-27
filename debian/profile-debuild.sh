# This file of environment variables is sourced via /etc/profile.d/debuild.sh
#
# It sets the email and name of the maintainer first.
# Next we disable parallel builds for ram conservation, normally enabled.
# Finally we add hardening to the build.

DEBEMAIL="debian@ptudor.net"
DEBFULLNAME="Patrick Tudor"

# "nocheck" here because testrunner runs out of ram on most current arm hardware -pht
# manual: dh_auto_test: If the DEB_BUILD_OPTIONS environment variable contains nocheck, no tests will be performed.
DEB_BUILD_OPTIONS='parallel=1 nocheck'

# https://wiki.debian.org/Hardening
DEB_BUILD_MAINT_OPTIONS='hardening=+all'

export DEBEMAIL DEBFULLNAME DEB_BUILD_OPTIONS DEB_BUILD_MAINT_OPTIONS
