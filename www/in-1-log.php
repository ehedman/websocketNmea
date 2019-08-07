<?php
    header('Cache-Control: no-cache, no-store, must-revalidate');
    header('Pragma: no-cache');
    header('Expires: 0');
?>
<!DOCTYPE html>
<html>
    <head>
        <meta http-equiv="Content-Type" content="text/html; charset=utf-8">
        <title>Sumlog</title>
        <script src="inc/jquery-2.1.1.min.js"></script>
        <script src="inc/jQueryRotate.js"></script> 
        <script src="inc/pako.js"></script>
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
    background-image: url('img/sumlog.png');
    background-size: 100%;
    background-repeat:no-repeat;
}

#needle
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

#LEDpanel
{
    position:absolute;
    top: 61%;
    left: 30%;
    font-weight: bold;
    font-size: 5.2em;
	letter-spacing: 2px;
    text-align: center;
    width: 40%;
    z-index: 10;
}

#SOGpanel
{
    position:absolute;
    top: 46%;
    left: 18%;
    font-size: 1.6em;
    font-weight: bold;
}

#logpanel
{
    position:relative;
    bottom: 0px;
    left: 30%;
    font-size: 1.3em;
}

    </style>
       
    <script>
       
function resize()	// Set font relative to window width.
{
    var f_Factor=1;
    var W = window.innerWidth || document.body.clientWidth;

    W=W<256?256:W;
    W=W>512?512:W;
    
	P =  Math.floor (f_Factor*(2.5*W/96));
	document.body.style.fontSize=P + 'px';
}

$(document).ready(function() {
    var userAgent = navigator.userAgent.toLowerCase();
    if (userAgent.match(/android/) !=null && userAgent.match(/firefox/) != null) {
        $("#LEDpanel").css("top", "58.5%"); 
    } 
    window.onresize=resize;
	resize();
});

var pt = 0;
var ut = 0;

var target = 0;
var ticks = 800;
var update = 800;
var maxangle = 238;
var offset = 12;
var maxspeed = 10;

var debug = false;
var connection = true;
var toggle = false;

function do_update()
{           
    if (connection) {
        send((toggle ^= true) == true? Cmd.SpeedOverGround:Cmd.SpeedThroughWater);      
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
        document.getElementById("needle").style.visibility="hidden"; 
        document.getElementById("LEDpanel").innerHTML=" -- -- ";
        document.getElementById("SOGpanel").innerHTML="&nbsp;";
        return;
    }
    
    if (valid == Cmd.SpeedOverGround) {
        if (target == "Exp")
             document.getElementById("SOGpanel").innerHTML="&nbsp;";
        else 
            document.getElementById("SOGpanel").innerHTML="SOG: "+ JSON.parse(target).speedog;
        return;
    }

    if (valid != Cmd.SpeedThroughWater) return; 
    
    var val = JSON.parse(target);

    document.getElementById("LEDpanel").innerHTML=val.sppedtw;

    if (val.sppedtw > maxspeed)
        { document.getElementById("needle").style.visibility="hidden"; angle = maxangle;  return; }
    else
        document.getElementById("needle").style.visibility="visible";

    var speed = val.sppedtw * (maxangle/maxspeed);
        
    $("#needle").rotate({animateTo:speed+offset,duration:4000,easing: $.easing.easeInQutSine});
}
    </script>

    </head>
    <body onload="init()">
        <div id="main">
            <div id="LEDpanel" title="Click to shift instrument" onclick="nextinstrument();"></div>
            <div id="instrument"><img src="img/needle1.png" alt="" id="needle"></div>
            <div id="SOGpanel">&nbsp;</div>
        </div>
        <div id="logpanel"></div>
        <script src="inc/common.js.php"></script>
    </body>
</html>
