#SPDX-License-Identifier: BSD-3-Clause
#Copyright 2023 NXP

#This script starts the system. Takes config for yami parameters and other modules option in a config file.
#This script also checks for HSDCS intermittency SINAD at the Init time

#./scriptname.sh configfilename.dat debug  ## to enable the debug and see the prints 
#./scriptname.sh configfilename.dat        ## which will not print any debug info.

#set -x

print_usage()
{
	echo
	echo " Usage: ./start_la12xx_modem.sh sysconfig.dat [debug] "
	echo
}

echo
if [ $1 ]; then
	#yami configuration file
	yamiconfigfile=$1
else
	echo " Error: config file is not passed !!"
	print_usage
	exit
fi

#pass debug option to print info logs
if [ "$2" = "debug" ]; then
	debug=true
fi

##parse and append only yami params into yamiargs
yamiargs=""
yamidelimit=0

# Extract the key-value pairs from the file
kv_pairs=$(grep -E '^[^#]*=' $yamiconfigfile)

# Split the key-value pairs into separate variables
while read -r line; do
	key=$(echo $line | cut -d '=' -f 1)
	value=$(echo $line | cut -d '=' -f 2)
	if [ $debug ]; then
		echo " "$key"="$value
	fi

	eval "$key=$value"

	if [ "$yamidelimit" == "0" ]; then 
		yamiargs+=" $key=$value"
	fi
done <<< "$kv_pairs"

IFS="," read -r -a array <<< "$pci_addr_array"

if [ $pci_addr_array ]; then
	geul_count=${#array[@]}
else
	geul_count=1
fi

# Storing the values in separate variables
pci_addr1=${array[0]}
pci_addr2=${array[1]}

if [ $yamipath ]; then
	echo "yamipath: $yamipath"
else
	yamipath=$(find /lib/modules/ -name yami.ko)
fi


# Print out the values of the variables if the "debug" option is enabled
if [ $debug ]; then

	echo "yamiargs: $yamiargs"

	if [ $geul_count ]; then
		echo "No of geul devices: $geul_count"
	fi

	if [ $pci_addr1 ]; then
		echo "pci_addr1: $pci_addr1"
	fi

	if [ $pci_addr2 ]; then
		echo "pci_addr2: $pci_addr2"
	fi

fi



if [ $hsdcs_sps ]; then
	if [ $hsdcs_sps == 983 ]; then
		hsdcs_sps_hz=983040000
		hsrxdumpfile=rx_timedomain_64KB_983040ksps_dump_ant4.bin
	elif [ $hsdcs_sps == 1966 ]; then
		hsdcs_sps_hz=1966080000
	else
		echo "Invalid hsdcs_sps value $hsdcs_sps"
		exit
	fi
else
	## if not defined in config file take 1966 as the default
	hsdcs_sps_hz=1966080000
	hsdcs_sps=1966
fi


	if [ $check_sinad ]; then
		# copy fr1_fr2 tool vspa image
		hsdcsvspafile="MEvspa_images_HS.2T2R_400M_120K_"$hsdcs_sps"_"$hsdcs_sps"_TDDFDD"
		single_tone_freq_hz=$single_tone_freq"000000"
		cp $fr1_fr2/$hsdcsvspafile/* /lib/firmware/
	fi


	if [ $debug ]; then
		echo "fr1_fr2 vspafile: $hsdcsvspafile "
		echo "hsdcs_sps_hz: $hsdcs_sps_hz"
		echo "single_tone_freq_hz = $single_tone_freq_hz"
	fi



	module_list=$(lsmod)
	if echo "$module_list" | grep -q "yami"; then
		echo "Yami is already loaded"
	else
		echo 1 > /sys/bus/pci/rescan;
		if [ $debug ]; then
			echo 8 > /proc/sys/kernel/printk;
		fi
		insmod $yamipath $yamiargs
	fi

##
## PLACE_HOLDER:RF loopback needs to be enabled
##


is_warmup_done(){
	cmd=`find /sys -name warmup_status`
	output=

	for i in $cmd
	do
		while [ 1 ];
		do
			output=`cat $i`
			if [ $debug ]; then
				echo "warmup check:$output"
			fi
			if [ "$output" == "1" ]; then
				#echo "output: $i $output"
				break
			fi
			sleep 1
		done
		echo "$i"
	done
}


# Function to perform SINAD check for a given  antenna
perform_sinad_check() {
	local ant=$1
	recalrequired=0
	eval "./send_single_tone.sh $ant $single_tone_freq""000000" "$single_tone_amp > /dev/null 2>&1 "
	./dump_time_domain_rx.sh $ant 2 > /dev/null 2>&1
	eval "python3 /root/scripts/SINAD.py rx_timedomain_64KB_1966080ksps_dump_ant$ant"".bin -s $hsdcs_sps -f $single_tone_freq > .sinadf.log "
	cat .sinadf.log | grep SINAD > .sinad.log 

	echo "ANT $ant SINAD value:"
	cat .sinad.log

	while read line; do
		# Extract the value from the line
		value1=$(echo $line | cut -d ':' -f 2)
		result=$(echo "$value1 < $desired_SINAD" | bc -l)
		recalrequired=$(($recalrequired + $result))
	done < .sinad.log
	return $recalrequired
}



check_sinad_fn(){
	local ant=$1
	local cnt1=0
	local recal=0

	echo
	while [ $cnt1 -le $max_recal_retry ]
	do
		perform_sinad_check $ant		
		recal=$?
		if [ "$recal" != "0" ]; then
			echo " Measured SINAD < desired SINAD($desired_SINAD) "
			cnt1=$((cnt1+1))

			if [ $cnt1 -le $max_recal_retry ]; then
				echo
				echo "Retry($cnt1)"

				if [ $dofullcal ]; then
					echo 1 > /sys/devices/platform/soc/3800000.pcie/pci0002:00/0002:00:00.0/0002:01:00.0/gulsysfs/hsdcs_fullcal
				fi

				if [ $dofastcal ]; then
					if [ $ant == 4 ]; then
						echo 3 > /sys/devices/platform/soc/3800000.pcie/pci0002:00/0002:00:00.0/0002:01:00.0/gulsysfs/hsdcs_recal
					elif [ $ant == 5 ]; then
						echo 15 > /sys/devices/platform/soc/3800000.pcie/pci0002:00/0002:00:00.0/0002:01:00.0/gulsysfs/hsdcs_recal
					fi
				fi
			fi
		else
			echo " Measured SINAD > desiredSINAD($desired_SINAD) "
			break
		fi
	done
}

# Get the IQ dump for HSDCS ANT0/1 using the fr1_fr2 tool
# calculate I and Q SINAD using the SINAD.py script
# if the value is less than desired do full or fast recal

if [ $check_sinad ]; then
	if [ $hsdcs_enable ]; then
		if [ $warmup_temp ] || [ $warmup_timeout ]; then
			is_warmup_done
		fi
	fi

	module_list=$(lsmod)
	if ! echo "$module_list" | grep -q "yami"; then
		echo "Yami is not loaded"
		exit
	fi
	
	#change to fr1_fr2_test_tool dir
	cd $fr1_fr2
	./channels_start.sh > /dev/null 2>&1

	if [ $hsdcs_enable ] && [ $sinad_check_ant0 ]; then
		check_sinad_fn 4
	fi

	if [ $hsdcs_enable ] && [ $sinad_check_ant1 ]; then
		check_sinad_fn 5
	fi

	#change to back to current dir
	cd - > /dev/null 2>&1 
fi

