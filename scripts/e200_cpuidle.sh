#!/bin/bash
#SPDX-License-Identifier: BSD-3-Clause
#Copyright 2022 NXP

if [ -z $1 ]; then
	echo "Usage: ./e200_cpuidle.sh <duration in sec> [gul id]"
	exit 0
elif [ $1 -le 0 ]; then
	echo "duration cannot be 0 or negative"
	echo "Usage: ./e200_cpuidle.sh <duration in sec> [gul id]"
	exit 0
fi

if [ "$#" -eq 2 ]; then
	gul_id=$2
else
	gul_id=-1
fi

#echo $S_CNT_L0 $S_CNT_H0
#echo $S_CNT_L1 $S_CNT_H1
#echo $S_CNT_L2 $S_CNT_H2
#echo $S_CNT_L3 $S_CNT_H3

do_calc()
{
	LO=$1
	HI=$2
	LO1=$3
	HI1=$4
	#echo $LO $HI $LO1 $HI1
	FIRST=$((($HI<<0x20) | $LO ))
	SECOND=$((($HI1<<0x20) | $LO1 ))
	#echo "FIRST: $FIRST  SECOND: $SECOND"
	DIFF=$(echo `expr $SECOND - $FIRST`)
	#echo "DIFF: $DIFF"
	#printf "\n"
	CAL_DATA_INTR=$(echo `expr $CAL_DATA \* $TIME`)
	#echo "CAL_DATA_INTR: $CAL_DATA_INTR"
	[[ $DIFF -ge $CAL_DATA_INTR ]] && DIFF=$CAL_DATA_INTR || DIFF=$DIFF
	#printf "DIFF: $DIFF"
	#printf "\n"
	MULT=$(echo `expr $DIFF \* 100`)
	#echo "MULT: $MULT"
	#printf "`expr $MULT / $CAL_DATA `"
	IDLE=$(echo "$MULT / $CAL_DATA_INTR" | bc -l)
	printf "%6.2f%s" $(echo "100 - $IDLE" | bc -l) "%"
	printf " %6.2f%s\n" "$IDLE" "%"
}

TIME=$1
CAL_DATA=2680200
while true
do
	printf "Time-"
	date +"%T"

	for file in $(find /sys -name "e200_cpu_usage")
	do
		e200_version=`cat ${file%e200_cpu_usage}la12xx_version`

		current_gul=`cat ${file%e200_cpu_usage}gul_id`
		[ ${gul_id} != -1 ] && [ ${gul_id} != ${current_gul} ] && continue

		echo -n "GUL ID:"
		cat ${file%e200_cpu_usage}gul_id
		echo -n "PCI DEV:"
		cat ${file%e200_cpu_usage}pci_dev_name

		ADDR_BASE=$( cat $file )
		S_CNT_L0=$(printf "0x%X\n" $(($ADDR_BASE + 0x10)))
		S_CNT_H0=$(printf "0x%X\n" $(($S_CNT_L0 + 0x4)))

		S_CNT_L1=$(printf "0x%X\n" $(($S_CNT_L0 + 0x1C)))
		S_CNT_H1=$(printf "0x%X\n" $(($S_CNT_L1 + 0x4)))

		S_CNT_L2=$(printf "0x%X\n" $(($S_CNT_L1 + 0x1C)))
		S_CNT_H2=$(printf "0x%X\n" $(($S_CNT_L2 + 0x4)))

		S_CNT_L3=$(printf "0x%X\n" $(($S_CNT_L2 + 0x1C)))
		S_CNT_H3=$(printf "0x%X\n" $(($S_CNT_L3 + 0x4)))

		if [ $e200_version == "B0" ]; then
			S_CNT_L4=$(printf "0x%X\n" $(($S_CNT_L3 + 0x1C)))
			S_CNT_H4=$(printf "0x%X\n" $(($S_CNT_L4 + 0x4)))

			S_CNT_L5=$(printf "0x%X\n" $(($S_CNT_L4 + 0x1C)))
			S_CNT_H5=$(printf "0x%X\n" $(($S_CNT_L5 + 0x4)))
		fi

		LO_0=$(devmem $S_CNT_L0)
		HI_0=$(devmem $S_CNT_H0)
		LO_1=$(devmem $S_CNT_L1)
		HI_1=$(devmem $S_CNT_H1)
		LO_2=$(devmem $S_CNT_L2)
		HI_2=$(devmem $S_CNT_H2)
		LO_3=$(devmem $S_CNT_L3)
		HI_3=$(devmem $S_CNT_H3)
		if [ $e200_version == "B0" ]; then
			LO_4=$(devmem $S_CNT_L4)
			HI_4=$(devmem $S_CNT_H4)
			LO_5=$(devmem $S_CNT_L5)
			HI_5=$(devmem $S_CNT_H5)
		fi
		#echo $LO_0 $LO_1 $LO_2 $LO_3 $LO_4 $LO_5
		sleep $TIME;
		LO1_0=$(devmem $S_CNT_L0)
		HI1_0=$(devmem $S_CNT_H0)
		LO1_1=$(devmem $S_CNT_L1)
		HI1_1=$(devmem $S_CNT_H1)
		LO1_2=$(devmem $S_CNT_L2)
		HI1_2=$(devmem $S_CNT_H2)
		LO1_3=$(devmem $S_CNT_L3)
		HI1_3=$(devmem $S_CNT_H3)
		if [ $e200_version == "B0" ]; then
			LO1_4=$(devmem $S_CNT_L4)
			HI1_4=$(devmem $S_CNT_H4)
			LO1_5=$(devmem $S_CNT_L5)
			HI1_5=$(devmem $S_CNT_H5)
		fi

		#clear >$(tty)
		printf "%-8s %-6s %s\n" " CORE:" "USE %" "IDLE %"
		printf "%s" "======================"
		printf '\n CPU0: '
		do_calc $LO_0 $HI_0 $LO1_0 $HI1_0
		printf ' CPU1: '
		do_calc $LO_1 $HI_1 $LO1_1 $HI1_1
		printf ' CPU2: '
		do_calc $LO_2 $HI_2 $LO1_2 $HI1_2
		printf ' CPU3: '
		do_calc $LO_3 $HI_3 $LO1_3 $HI1_3
		if [ $e200_version == "B0" ]; then
			printf ' CPU4: '
			do_calc $LO_4 $HI_4 $LO1_4 $HI1_4
			printf ' CPU5: '
			do_calc $LO_5 $HI_5 $LO1_5 $HI1_5
		fi
		printf "\n"
	done
	sleep $TIME;
done
