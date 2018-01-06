<!DOCTYPE html>
<html>
    <head>
        <meta http-equiv="Content-Type" content="text/html; charset=utf-8">
        <title>Clock</title>
        <script type="text/javascript" src="inc/jquery-2.1.1.min.js"></script>
        <script type="text/javascript" src="inc/jQueryRotate.js"></script>
    <style>

html>body
{
    font-family: Tahoma, Helvetica, Geneva, Arial, sans-serif;
	font-size: 0.9em;
    margin: 0 0 0 0;
    width: 100%;
    min-width: 256px;
    max-width: 512px;
    margin-left:auto;
    margin-right:auto;
    overflow: hidden;
}

#instrument
{
    position:relative;
    border: 0;
    background-image: url('img/watch.png');
    background-size: 100%;
    background-repeat:no-repeat;
    z-index: 0;
}

#minutes
{
    position:relative;
    top: 25%;
    left: 0%;
    width: 50%;
    margin: 25%;
}

#main
{
    position:relative;
}

#seconds
{
    position:absolute;
    top: 0%;
    left: 0%;
    width: 50%;
    margin: 25%;
    z-index: 1;
}

#hours
{
    position:absolute;
    top: 15%;
    left: 15%;
    width: 70%;
    border: 0px;
    z-index: 2;
}

#logpanel
{
    position:relative;
    bottom: 10;
    left: 20%;
    font-size: 1.7em;
}
    </style>
       
    <script type="text/javascript">

var pt = 0;
var ut = 0;

var target = 0;
var ticks = 1000;
var update = 2000;
var seconds = 0;
var minutes = 0;
var hours=0;
var soffset = 131.2;
var moffset = 131.2;
var hoffset = 131.2;

var debug = false;
var connection = true;
var toggle = false;

function do_update()
{        
    if (connection) {
        send("901"); 
        if (seconds >=360) seconds=0;
        if (minutes >=360) minutes=0;
        if (hours >=360) hours=0;
        if (pt == 0)           
            pt = setInterval(function () {do_poll();}, ticks);
    } else {
        window.clearInterval(ut);
        reconnect();
    }
}

function do_poll()
{

    if (valid == Cmd.TimeOfDAy) {
        valid = "";
        window.clearInterval(ut);
        
        // Get the servers' (vessel) idea of time
        var val = JSON.parse(target);
        
        //val.hours=19;
        //val.minutes=05;
        //val.seconds=12;
        
        val.hours > 12? val.hours-12: val.hours;
        
        seconds = (360/60)*val.seconds; 
        minutes = ((360/60)*val.minutes)+seconds/60;
        hours = (val.hours*(360/12))+val.minutes/2+val.seconds/3600;
        
        if (debug)
            document.getElementById("logpanel").innerHTML="h="+val.hours+", min="+val.minutes+", sec="+val.seconds;
       
        ut = setInterval(function () {do_update();}, 60*1000);
    }

    $("#hours").rotate(hours+hoffset);
    $("#minutes").rotate(minutes+moffset);
    $("#seconds").rotate(seconds+soffset);

    seconds+=6;
    minutes+=0.1;
    hours+=0.0080;
}
    </script>

    </head>
    
    <body onload="init()">
        <div id="main">
            <div id="instrument"><img src="img/needle1.png" alt="" id="minutes"></div>
            <img src="img/needle-black.png" id="seconds" title="Click to shift instrument" onclick="nextinstrument();">
            <img src="img/needle.png" id="hours" onclick="nextinstrument();">
        </div>
        <div id="logpanel"></div>
        <script type="text/javascript" src="inc/common.js.php"></script>
    </body>
    
</html>
