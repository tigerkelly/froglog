#!/bin/bash
# format ssd or thumb drive.

. /home/pi/bin/common.sh

# Get GPIO pins 22 and 23 state for card IP address.
n=$(GetIpNum)
card=`/home/pi/bin/gpioread ${CARDID}`

echo $n > /home/pi/ssd.log

doneOnce=0

if [ "${card}" == "1" ]; then  # if card type is DroneView

	if [ $n -eq 1 ]; then	# if card number is 1
		while [ ! -e /dev/sda1 ]
		do
			if [ -e /dev/sda ]; then
				sudo wipefs -a /dev/sda
				sudo parted --script /dev/sda mklabel gpt mkpart primary ext4 0% 100%
				sudo mkfs.ext4 -F -t ext4 /dev/sda1
				sudo e2label /dev/sda1 DroneData
				doneOnce=1
			else
				sleep 2
				echo no /dev/sda device.
			fi
		done

		if [ "$1" == "force" ] && [ $doneOnce -eq 0 ]; then
			sudo wipefs -a /dev/sda
			sudo parted --script /dev/sda mklabel gpt mkpart primary ext4 0% 100%
			sudo mkfs.ext4 -F -t ext4 /dev/sda1
			sudo e2label /dev/sda1 DroneData
		fi
	fi
fi

exit 0
