<?php
    header('Content-Type: text/javascript; charset=UTF-8');
?>
var socket;
var retry = 0;
var valid;
var port = 443;

if (typeof debug == 'undefined') {
    debug = false;
}

if (typeof update == 'undefined') {
    update = 2000;
}

if (typeof do_update == 'undefined') {
    function do_update() { return 0; }
}

var Cmd = {
    SpeedOverGround     : "100",
    SpeedThroughWater   : "101",
    DepthAndTemp        : "110",
    CompassHeading      : "120",
    GPS                 : "121",
    WindSpeedAndAngle   : "130",
    GoogleMapFeed       : "140",
    GoogleAisFeed       : "141",
    GoogleAisBuddy      : "142",
    SensorVolt          : "200",
    SensorCurr          : "201",
    SensorTemp          : "202",
    SensorRelay         : "203",
    AisTrxStatus        : "206",
    WaterMakerData      : "210",
    ServerPing          : "900",
    TimeOfDAy           : "901",
    SaveNMEAstream      : "904",
    StatusReport        : "906",
    Authentication      : "908",
    UpdateSettings      : "910"
};

function init()
{
    var host = "ws://<?php echo preg_split("/:/", $_SERVER['HTTP_HOST'])[0]; ?>:"+port; // SET THIS TO YOUR SERVER
    connection = true;

    try {
        window.WebSocket = window.WebSocket || window.MozWebSocket;

        socket = new WebSocket(host,'nmea-parser-protocol');
        socket.binaryType = 'arraybuffer';
        printlog('WebSocket - status '+socket.readyState);

        socket.onopen = function () {
            printlog("Open: OK");
        };

        socket.onerror = function () {
            printlog("Socket: Error");
            connection = false;
        };

        socket.onmessage = function (msg) {
            var data;
            if (typeof pako == 'undefined')
                return;
            data=pako.inflate(msg.data);
            var strData = String.fromCharCode.apply(null, new Uint16Array(data));
            var n  = strData.lastIndexOf("-");
            valid  = strData.substr(n+1, 3);
            target = strData.substr(0, n);
            printlog("Received: "+strData);
            retry = 0;
        };
    }
    catch(ex) { 
        printlog(ex);
        connection = false; 
    }

    ut = setInterval(function () {do_update();}, update);  
}


function send(msg){
	if(!msg)
		return;
		
	try { 
	    if (retry > 10) {
	        connection = false;
	        target = "";
	        valid = "";
	        retry = 0;
	        return;
	    }
		socket.send(msg); 
		printlog('Sent: '+msg); 
		retry++;
	} catch(ex) { 
		printlog(ex);
		retry++; 
	}
}

function quit(){
	if (socket != null) {
		printlog("Goodbye!");
		socket.close();
		socket=null;
	}
}

function reconnect() {
	quit();
	init();
}


function printlog(msg)
{
    if (debug)
        document.getElementById("logpanel").innerHTML=msg;
}


function round_number(num, dec) {
    return Math.round(num * Math.pow(10, dec)) / Math.pow(10, dec);
}


// Switch to next instrument in the list

var instruments = [<?php
    define('DOCROOT', dirname(__FILE__)); 
    $items = array();
    exec("cd ".DOCROOT."/../; /bin/echo 'for file in $(ls in-*|cut -d\- -f2|sort -n); do ls in-\${file}-*.php; done' > /tmp/in-list; bash /tmp/in-list; rm -f /tmp/in-list", $items);
    $n = count($items);
    for ($i=0; $i<$n; $i++) {
        echo "'".$items[$i]."'";
        if ($i < $n-1) echo ',';
    } ?>];

var loc = window.location.pathname;
var ipath = loc.split("/").slice(0, -1).join("/")+"/";

var next = loc.split("-")[1];
next = next >= instruments.length? 0:next++;

function nextinstrument()
{
    window.location.replace('http://<?php echo $_SERVER['HTTP_HOST']; ?>'+ipath+instruments[next]);
}
