#!/bin/bash

ip=$1
dev=$2
outf=$3
ret=0

if ! ping -c 1 -w 1 "$ip" > /dev/null 2>&1; then
     echo "$dev 0" >> "$outf"
else
    echo -n "$dev " >> "$outf"
    hs100 "$ip" info 2>/dev/null | jshon -e system -e get_sysinfo -e relay_state >> "$outf" 2>/dev/null
    ret="${PIPESTATUS[0]}"
    if [ "$ret" -ne 0 ]; then
        echo 0 >> "$outf"
    fi
fi

exit "$ret"
