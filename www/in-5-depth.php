<?php
    header('Cache-Control: no-cache, no-store, must-revalidate');
    header('Pragma: no-cache');
    header('Expires: 0');
?>
<!DOCTYPE html>
<html lang="en">
    <head>
        <meta http-equiv="Content-Type" content="text/html; charset=utf-8">
        <title>Depth</title>
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
    background-image: url('img/depth.png');
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
    top: 59%;
    left: 30%;
    font-weight: bold;
    font-size: 5.2em;
    letter-spacing: 2px;
    text-align: center;
    width: 40%;
    z-index: 10;
}

#TEMPpanel
{
    position:absolute;
    top: 76%;
    left: 34%;
    font-size: 1.6em;
    font-weight: bold;
}
    </style>
       
    <script>
        
function resize()    // Set font relative to window width.
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
        $("#LEDpanel").css("top", "57%"); 
    } 
*/
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
var maxdepth = 10;

var debug = false;
var connection = true;

function do_update()
{        
    if (connection) {
        send(Cmd.DepthAndTemp);       
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
        document.getElementById("TEMPpanel").innerHTML="&nbsp;";
        return;
    }
    
    if (valid != Cmd.DepthAndTemp) return;
    
    var val = JSON.parse(target);
    var rdepth = val.depth;
    
    if (val.depth > maxdepth)
        val.depth = round_number(val.depth, 1);
        
    if (val.depth >20)
        val.depth = round_number(val.depth, 0);

    document.getElementById("LEDpanel").innerHTML=val.depth;

    if (val.depth <= val.vwrn)
         document.getElementById("instrument").style.backgroundImage = "url('img/depthw.png')";
    else if (val.depth > 10) {
        document.getElementById("instrument").style.backgroundImage = "url('img/depthx10.png')";
        val.depth /= 10;
    } else
        document.getElementById("instrument").style.backgroundImage = "url('img/depth.png')";  

    if (val.temp > 0)
        document.getElementById("TEMPpanel").innerHTML="Temp: "+val.temp+" &deg;C";
    else
        document.getElementById("TEMPpanel").innerHTML="&nbsp;";
  
    if (rdepth > 100)
        { document.getElementById("needle").style.visibility="hidden"; angle = maxangle;  return; }
    else
        document.getElementById("needle").style.visibility="visible";  
   
    var angle = val.depth * (maxangle/maxdepth);  
    
    $("#needle").rotate({animateTo:angle+offset,duration:2000,easing: $.easing.easeInQutSine});

}
    </script>

    </head>
    <body onload="init()">
        <div id="main">
            <div id="LEDpanel" title="Click to shift instrument" onclick="nextinstrument();"></div>
            <div id="instrument"><img src="img/needle1.png" alt="" id="needle"></div>
            <div id="STWpanel">&nbsp;</div>
            <div id="TEMPpanel"></div>
        </div>
        <div id="logpanel"></div>
    </body>
</html>
