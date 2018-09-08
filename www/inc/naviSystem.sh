#!/bin/bash

get_ttys()
{
    m=$(ls /dev/tty* | egrep 'ttyUSB|ttySAC|ttyACM' | awk -F" " '{ printf "%s|", $1 }')
    ls /dev/tty*S* | egrep "${m}none" | awk '{print $NF}'
}

get_netifs()
{
    cat /proc/net/dev | grep ": " | egrep -v 'ppp|sit|ip6' | cut -d: -f1 | awk '{gsub(/ /, "", $0); print}'
}

get_ipaddr()
{
    a=`/sbin/ifconfig $1 | grep 'inet addr:' | cut -d: -f2 | awk '{print $1}'`
    if [ -z "$a" ]; then
        echo -n "127.0.0.1"
    else
        echo -n $a
    fi
}

get_broadcast_addr()
{
    a=`/sbin/ifconfig $1 | grep 'Bcast:' | cut -d: -f3 | awk '{print $1}'`
    if [ -z "$a"  ]; then
        get_ipaddr $1
    else
        echo -n $a
    fi
}

check_local_ipaddr()
{
    if [ -n "$(ip addr show | grep -w $1)" ] || [ -n "$(arp -n | grep -w $1)" ]; then
        echo "0"
    else
        echo "1"
    fi
}

get_nrecordings()
{
 
    ls ./upload | awk '{gsub(/ /, "", $0); print}' | grep ".txt"
}

restart_wsserver()
{
    echo $* > /tmp/wss-reboot
}

$*
