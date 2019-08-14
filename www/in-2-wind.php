<?php
    header('Cache-Control: no-cache, no-store, must-revalidate');
    header('Pragma: no-cache');
    header('Expires: 0');
?>
<!DOCTYPE html>
<html lang="en">
    <head>
        <meta http-equiv="Content-Type" content="text/html; charset=utf-8">
        <title>Wind</title>
        <script src="inc/jquery-2.1.1.min.js"></script>
        <script src="inc/jQueryRotate.js"></script>
        <script src="inc/pako.js"></script>
        <script src="inc/common.js.php"></script>
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
    background-image: url('img/wind.png');
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

#needle1
{
    position:absolute;
    top: 0%;
    left: 0%;
    width: 50%;
    margin: 25%;
}

#main
{
    position:relative;
}

#logpanel
{
    position:relative;
    bottom: 0px;
    left: 30%;
    font-size: 1.3em;
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

#TSPpanel
{
    position:absolute;
    top: 76%;
    left: 38%;
    font-size: 1.4em;
    font-weight: bold;
}

#ANGpanel
{
    position:absolute;
    top: 18%;
    left: 43%;
    width: 15%;
    font-size: 1.9em;
    font-weight: bold;
    text-align: center;
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
/*
    var userAgent = navigator.userAgent.toLowerCase();
    if (userAgent.match(/android/) !=null && userAgent.match(/firefox/) != null) {
        $("#LEDpanel").css("top", "60.5%"); 
    } 
*/

    window.onresize=resize;
    resize();
});

var pt = 0;
var ut = 0;

var target = 0;
var target_angle = 0;
var ticks = 800;
var update = 800;
var offset = 131;

var debug = false;
var connection = true;

var rotate = 0;

function do_update()
{        
    if (connection) {
        send(Cmd.WindSpeedAndAngle); 
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
        document.getElementById("needle1").style.visibility="hidden"; 
        document.getElementById("LEDpanel").innerHTML=" -- -- ";
        document.getElementById("ANGpanel").innerHTML="&nbsp;";
        document.getElementById("TSPpanel").innerHTML="&nbsp;";
        return;
    }
    
    if (valid != Cmd.WindSpeedAndAngle) return;
    
    var val = JSON.parse(target);
    
    var angle = round_number(val.angle,0);
    var tangle = round_number(val.tangle,0);
    document.getElementById("ANGpanel").innerHTML=angle+"°";
    document.getElementById("LEDpanel").innerHTML=val.speed;
    if (val.dir == 1) angle = 360 - angle; 
    
    document.getElementById("needle").style.visibility="visible";
    if (tangle)
        document.getElementById("needle1").style.visibility="visible";
    else
        document.getElementById("needle1").style.visibility="hidden";

    if (val.tspeed > 0)
        document.getElementById("TSPpanel").innerHTML="TRUE: "+val.tspeed;
    else
        document.getElementById("TSPpanel").innerHTML="&nbsp;";

    var aR = rotate % 360;
    if ( aR < 0 ) { aR += 360; }
    if ( aR < 180 && (angle > (aR + 180)) ) { rotate -= 360; }
    if ( aR >= 180 && (angle <= (aR - 180)) ) { rotate += 360; }
    rotate += (angle - aR);
    
    $("#needle").rotate({animateTo:rotate+offset,duration:4000,easing: $.easing.easeInQutSine});

}
    </script>

    </head>
    
    <body onload="init()">
        <div id="main">
            <div id="LEDpanel" title="Click to shift instrument" onclick="nextinstrument();"></div>
            <div id="instrument">
                <img src="img/needle1.png" alt="" id="needle">
                <img src="img/needle-black.png" alt="" id="needle1">
            </div>
            <div id="TSPpanel"></div>
            <div id="ANGpanel">&nbsp;</div>
        </div>
        <div id="logpanel"></div>
    </body>  
    
</html>
