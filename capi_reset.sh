#!/bin/bash


if [ -n "$1" ]; then
	echo user > /sys/class/cxl/card$1/load_image_on_perst
	#setpci -s 0000:01:00.0 910.L=b0000000
	echo "Preparing to reset card"
	echo 1 > /sys/class/cxl/card$1/reset
	echo "Sleeping 20 seconds for reset to occur"
	sleep 20

else
	#Find all the CAPI cards in the system
	cardnums=`ls -d /sys/class/cxl/card* | awk -F"/sys/class/cxl/card" '{ print $2 }'`
	for i in $cardnums; do
		echo user > /sys/class/cxl/card$i/load_image_on_perst
		#setpci -s 0000:01:00.0 910.L=b0000000
		echo "Preparing to reset card"
		echo 1 > /sys/class/cxl/card$i/reset
		echo "Sleeping 20 seconds for reset to occur"
		sleep 20
	done
fi
