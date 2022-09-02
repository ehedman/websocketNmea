#!/bin/bash

# dpendent on https://github.com/ehedman/flowSensor

tmpf=$(mktemp /tmp/flow.XXXXXXXXXX)
dataFile="$1"

if [ -z "${dataFile}" ]; then
    exit 2
fi

wget --timeout=2 -t1 --quiet -O - 'http://digiflow/ssi.shtml' | grep -E 'totv|tnkv|fage' | grep -oP 'value="\K[^"]+' > "${tmpf}"

rval="${PIPESTATUS[0]}"

if [ "${rval}" -eq 0 ]; then
    cp "${tmpf}" "${dataFile}"
fi

rm -f "${tmpf}"

exit "${rval}"
