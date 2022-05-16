#!/bin/sh

ip=$1
dev=$2
outf=$3

if ! ping -c 1 -w 1 "$ip" > /dev/null 2>&1; then
    echo "$dev 0" >> "$outf"
else
   echo "$dev" "$(hs100 "$ip" info | jshon -e system -e get_sysinfo -e relay_state)" >> "$outf"
fi
