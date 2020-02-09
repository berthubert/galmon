# .deb Package Overview

2020-01-20: Initial Commit

## Important Information

This section is a step-by-step tutorial.

### Basic Installation

Pick either debian (almost everything) or raspbian (special armv6 build for older models) and create the source file.
```sh
echo "deb https://ota.bike/raspbian/ buster main" > /etc/apt/sources.list.d/galmon.list
echo "deb https://ota.bike/debian/ buster main" > /etc/apt/sources.list.d/galmon.list
```

Install the file used to ensure the software is verified.
```sh
apt-key adv --fetch-keys https://ota.bike/public-package-signing-keys/86E7F51C04FBAAB0.asc
```

Update your package list and install galmon. Then create a configuration file and start the daemon.
If you have a typical device using the onboard USB at /dev/ttyACM0, drop the directory element
and refer to ttyACM0 in both the default variable file and the unit name.
```sh
apt-get update && apt-get install -y galmon
cp /etc/default/galmon /etc/default/ubxtool-ttyACM0
systemctl enable --now ubxtool@ttyACM0
```

Alternate or multiple devices just repeats that, updating the device name:
```sh
cp /etc/default/galmon /etc/default/ubxtool-ttyACM3
systemctl enable --now ubxtool@ttyACM3
```
Both the ubxtool-ttyXYZn file and /dev/ttyXYZn device must exist for the unit file conditions to pass.

### Automatic Updates

Armbian and Raspbian have apt-daily timers enabled by default.
However, most configurations for unattended installs require customization.

A simple timer is included that will apply any galmon upgrades every three days:
```sh
systemctl enable --now galmon-upgrade.timer
```

You can perform an immediate update by hand:
```sh
apt-get update && apt-get -y install galmon && systemctl restart ubxtool@*
```

## Reference Information

You can stop reading here if your interest was limited to installing a compiled package.

### One time steps for bootstrapping package build on a fresh git repo

Run debmake in the source directory. It tries to autocreate 90% of everything you need in the debian folder.
Key files: copyright, changelog, control, and anything else that looks interesting to cat. Once they exist, we're done.
Refer to the manual's [tutorial](https://www.debian.org/doc/manuals/debmake-doc/ch04.en.html).

### One time steps for creating package-specific files and scripts

Inside the debian directory are files that begin with galmon, the name of the package as defined in the control file.
- galmon.postinst: this script is run after installation to verify a system account exists.
- galmon.default: this file is installed as /etc/default/galmon
- galmon.ubxtool@.service: this unit file uses %i as a reference to the device for computers with multiple inputs.

### How to build the package locally

In short you need to set some variables, refer to profile-debuild.sh in the debian/ directory.
After that, use debuild to install the package. Signing of the end result may fail and creating
GPG key pairs is beyond the scope of this document but just make sure you match the email in the changelog.
```sh
apt-get install -y build-essential devscripts lintian diffutils patch patchutils
git clone $flags galmon.git ; cd galmon
./make-githash.h
# create and source variables in /etc/profile.d/debuild.sh
debuild
dpkg -i ../*.deb
```

### Future maintenance considerations

The githash.h files cannot change after the debuild process has started.
For now, the Makefile used by debuild does not run that script and
the files must be created before starting the debuild process.

### Real World Build Results in January 2020

Avoid compiling on arm6 computers, it is slow. The arm6 used in cheap Raspberry Pi models is an expensive model to support
relative to the much faster arm7 and arm8 computers available. Compiling approaches 90 minutes at O3. 

The arm7 and arm8 both compile in 20 to 30 minutes at -j1 -O3 but the 64 bit arm8 has approximately
double the RAM requirements during compilation. To avoid swapping, increasing compile time 150%,
use hardware with at least 1GB of RAM. The NanoPi Neo2 and NanoPi ZeroPi models with 512MB of RAM
are perfect clients, but the OrangePi PCs with 1GB of RAM and the Allwinner H3 or H5 are
better suited for smooth building. For comparison, a VM on a low-end AMD Ryzen 3 2200G builds the package at -j1 in about two minutes.

These are fast multi-core computers but we turn off parallel compiles because of limited RAM.
Limiting optimizations to -O0 cuts the compile time in half approximately.

### Why do this?

Convenience, uniformity, and scalability:
Hand-compiling software is fun, but vendor package management solutions
exist to give us reliable unattended installations for free.

### Signing key

GPG Public Key [86E7F51C04FBAAB0](debian/86E7F51C04FBAAB0.asc)
