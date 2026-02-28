#!/bin/bash

get_ttys()
{
    m=$(ls /dev/tty* | egrep 'ttyUSB|ttySAC|ttyACM|ttyAMA|ttyS' | awk -F" " '{ printf "%s|", $1 }')
    ls /dev/tty*S* /dev/ttyA* 2>/dev/null | egrep "${m}none" | awk '{print $NF}'
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

    a=$(ifconfig $1 2> /dev/null | grep netmask | awk '{print $2}')
    if [ -z "$a" ]; then
        echo -n "127.0.0.1"
    else
        echo -n $a
    fi
}

get_broadcast_addr()
{
    a=$(ifconfig $1 2> /dev/null | grep broadcast | awk '{print $NF}')
    if [ -z "$a"  ]; then
        get_ipaddr $1
    else
        echo -n $a
    fi
}

check_local_ipaddr()
{
    if [ -z "$1" ]; then exit 1; fi

    if [ -n "$(ifconfig -a | grep -w $1)" ] || [ -n "$(arp -n | grep -w $1)" ]; then
        exit 0
    else
        exit 1
    fi

}

check_matching_ipaddr()
{
    if [ -z "$1" ]; then exit 1; fi

    ifconfig -a | grep netmask | awk '{print $2}' |  grep -q $1
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
