#!/bin/bash
#SPDX-License-Identifier: BSD-3-Clause
#Copyright 2024 NXP

if [ -z $1 ]; then
        echo "Usage: ./copy_vspa.sh <vspa_name_prefix>"
        exit 0
fi

file=$1"0.eld"
echo $file
cp $file /lib/firmware/geul-vspa0.eld
cp /lib/firmware/geul-vspa0.eld /lib/firmware/geul-vspa1.eld
cp /lib/firmware/geul-vspa0.eld /lib/firmware/geul-vspa2.eld
cp /lib/firmware/geul-vspa0.eld /lib/firmware/geul-vspa3.eld
cp /lib/firmware/geul-vspa0.eld /lib/firmware/geul-vspa4.eld
cp /lib/firmware/geul-vspa0.eld /lib/firmware/geul-vspa5.eld
cp /lib/firmware/geul-vspa0.eld /lib/firmware/geul-vspa6.eld
cp /lib/firmware/geul-vspa0.eld /lib/firmware/geul-vspa7.eld
