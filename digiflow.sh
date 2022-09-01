#!/bin/bash

# dpendent on https://github.com/ehedman/flowSensor

tmpf=$(mktemp /tmp/flow.XXXXXXXXXX)
pidFile=/tmp/digiflow.pid
dataFile="$1"

function doExit
{
    /usr/bin/rm -f "${tmpf}" "${pidFile}" "${dataFile}";  exit 255;
}

trap 'doExit' SIGINT SIGTERM EXIT

echo "$$" > "${pidFile}"

while true

do
    wget --timeout=4 --quiet -O - 'http://digiflow/ssi.shtml' | grep -E 'totv|tnkv|fage' | grep -oP 'value="\K[^"]+' > "${tmpf}"

    if [ "${PIPESTATUS[0]}" -eq 0 ]; then
        cp "${tmpf}" "${dataFile}"
    fi

    sleep 6

done
