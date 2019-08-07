<?php
    header('Cache-Control: no-cache, no-store, must-revalidate');
    header('Pragma: no-cache');
    header('Expires: 0');
?>
<!DOCTYPE html>
<html>
    <head>
        <meta http-equiv="Content-Type" content="text/html; charset=utf-8">
        <title>Current</title>
        <script src="inc/jquery-2.1.1.min.js"></script>
        <script src="inc/jQueryRotate.js"></script>
        <script src="inc/pako.js"></script>
    <style>

html>body
{
    margin: 0 0 0 0;
    width: 100%;
    min-width: 250px;
    max-width: 250px;
    margin-left:auto;
    margin-right:auto;
}
#instrument
{
    max-height: 250px;
    min-height: 250px;
    border: 0px;
    background-image: url('img/curr.png');
    background-size: 250px 250px;
    background-repeat:no-repeat;
}

#logpanel
{
    position:relative;
    bottom: 70px;
    left: 36%;
    font-size: 0.4em;
}

#LEDpanel
{
    position:relative;
    bottom: 72px;
    left: 0%;
    font-weight: bold;
    font-size: 1.2em;
	letter-spacing: 2px;
    text-align: center;
}

#needle
{
    max-height: 90px;
    min-height: 90px;
    border: 0px;
    position:relative;
    top: 95px;
    left: 21.5%;
}

    </style>

<script>

var pt = 0;
var ut = 0;

var target = 0;
var ticks = 2000;
var update = 2000;

var maxangle = 120;
var offset = 58;
var maxcurr = 30;
var scaleoffset = 0;

var debug = false;
var connection = true;

function do_update()
{        
    if (connection) {
        send(Cmd.SensorCurr);  
        if (pt == 0)           
            pt = setInterval(function () {do_poll();}, ticks);
    } else {
        window.clearInterval(ut);
        reconnect();
    }
}

function do_poll()
{
   
    if (target == "Exp" || connection == false) {
        document.getElementById("needle").style.opacity = "0.3";
        document.getElementById("LEDpanel").innerHTML="--";
        return;
    }
    
    if (valid != Cmd.SensorCurr) return;
    
    var val = JSON.parse(target);
     
    document.getElementById("needle").style.opacity = "100";

    document.getElementById("LEDpanel").innerHTML=val.curr;

    val.curr *=0.5;

    if (val.curr > maxcurr)
        { document.getElementById("needle").style.visibility="hidden"; angle = maxangle;  return; }
    else
        document.getElementById("needle").style.visibility="visible"; 
    
    var angle = (val.curr-scaleoffset) * (maxangle/maxcurr);
    
    $("#needle").rotate({animateTo:(angle*2)+offset}); 

}
    </script>

    </head>
     <body onload="init()">
        <div id="instrument">
            <img src="img/sneedle.png" id="needle" title="Click to shift instrument" onclick="nextinstrument();">
        </div>
        <div id="LEDpanel"></div>
        <div id="logpanel"></div>
        <script src="inc/common.js.php"></script>
    </body>
</html>
