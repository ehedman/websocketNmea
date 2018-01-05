<!DOCTYPE html>
<html>
    <head>
        <meta http-equiv="Content-Type" content="text/html; charset=utf-8">
        <title>Temp</title>
        <script type="text/javascript" src="inc/jquery-2.1.1.min.js"></script>
        <script type="text/javascript" src="inc/jQueryRotate.js"></script>
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
    background-image: url('img/indoor-temp.png');
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
    top: 102px;
    left: 22%;
}

    </style>

<script type="text/javascript">

var pt = 0;
var ut = 0;

var target = 0;
var ticks = 2000;
var update = 2000;

var maxangle = 136;
var offset = 43;
var maxtemp = 50;
var scaleoffset = 8;

var debug = false;
var connection = true;

function do_update()
{        
    if (connection) {
        send(Cmd.SensorTemp);  
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
    
    if (valid != Cmd.SensorTemp) return;
    
    var val = JSON.parse(target);

    val.temp -= 25;
 
    document.getElementById("needle").style.opacity = "100";

    document.getElementById("LEDpanel").innerHTML=val.temp;


    if (val.temp > maxtemp)
        { document.getElementById("needle").style.visibility="hidden"; angle = maxangle;  return; }
    else
        document.getElementById("needle").style.visibility="visible"; 
    
    var angle = (val.temp-scaleoffset) * (maxangle/maxtemp);
    
    $("#needle").rotate({animateTo:(angle*1.2)+offset});

}
    </script>

    </head>
     <body onload="init()">
        <div id="instrument">
            <img src="img/sneedle.png" id="needle" onclick="nextinstrument();">
        </div>
        <div id="LEDpanel"></div>
        <div id="logpanel"></div>
        <script type="text/javascript" src="inc/common.js.php"></script>
    </body>
</html>
