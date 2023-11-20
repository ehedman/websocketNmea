#!/bin/bash

get_ttys()
{
    m=$(ls /dev/tty* | egrep 'ttyUSB|ttySAC|ttyACM|ttyAMA|ttyS' | awk -F" " '{ printf "%s|", $1 }')
    ls /dev/tty*S* /dev/ttyA* | egrep "${m}none" | awk '{print $NF}'
}

get_netifs()
{
    cat /proc/net/dev | grep ": " | egrep -v 'ppp|sit|ip6' | cut -d: -f1 | awk '{gsub(/ /, "", $0); print}'
}

get_ipaddr()
{
    if [ -z "$1" ]; then
        echo -n "127.0.0.1"
        return
    fi

    a=$(ip address show $1 2> /dev/null | grep -v ${1}: | grep "inet " | awk -F'[/]' '{ print $1 }' | awk  '{ print $2 }')
    if [ -z "$a" ]; then
        echo -n "127.0.0.1"
    else
        echo -n $a
    fi
}

get_broadcast_addr()
{
    a=$(ip address show $1 2> /dev/null | grep -m1 255 | awk '{ print $4 }')
    if [ -z "$a"  ]; then
        get_ipaddr $1
    else
        echo -n $a
    fi
}

check_local_ipaddr()
{
    if [ -z "$1" ]; then exit 1; fi

    if [ -n "$(ip addr show | grep -w $1)" ] || [ -n "$(arp -n | grep -w $1)" ]; then
        exit 0
    else
        exit 1
    fi

}

check_matching_ipaddr()
{
    if [ -z "$1" ]; then exit 1; fi

    ip a s  | awk -F"[/ ]+" '/inet / {print $3}' | grep -q $1
    exit $?
}

get_nrecordings()
{
 
    ls ./upload | awk '{gsub(/ /, "", $0); print}' | grep ".txt"
}

restart_wsserver()
{
    systemctl restart wsocknmea.service &> /dev/null
    if [ $? -ne 0 ]; then
        # Lost for systemd but alive with new pid
        echo $* > /tmp/wss-reboot
    fi
}

$*
