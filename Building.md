Raspberry Pi 3B+
---------------

Building more than ubxtool, navdump, and navparse will require quite a bit of available RAM. 
In the Raspbian image, the default swap file size for _/var/swap_ is only 100MB.
More swap space will be needed if you want the compiling to finish without it seeming
to take a geologic age.

The swap file is configured in _/etc/dphys-swapfile_ and managed using the __/sbin/dphys-swapfile__ utility.

Assuming the output of _df -h_ and _free_ indicate you have sufficient free file space and
the swap file is indeed 100MB (respectively), and you do not have Chromium running (it eats
RAM), you edit the config file (suggest changing 100 to 960) and then (as root):
```
/sbin/dphys-swapfile swapoff
echo Be Patient. This could take a while
/sbin/dphys-swapfile setup
/sbin/dphys-swapfile swapon
```
