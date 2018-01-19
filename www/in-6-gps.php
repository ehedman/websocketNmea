<!DOCTYPE html>
<html>
    <head>
        <meta http-equiv="Content-Type" content="text/html; charset=utf-8">
        <title>GPS</title>
        <script type="text/javascript" src="inc/jquery-2.1.1.min.js"></script>
        <script type="text/javascript" src="inc/pako.js"></script>
        
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
   
    width: 100%;
   
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

#LEDpanel1
{
    position:absolute;
    top: 25%;
    left: 35%;
    font-weight: bold;
    font-size: 5.2em;
	letter-spacing: 2px;
    text-align: center;
    width: 30%;
}
#LEDpanel2
{
    position:absolute;
    top: 44%;
    left: 23%;
    font-weight: bold;
    font-size: 2.8em;
	letter-spacing: 2px;
    text-align: center;
    min-width: 55%;
}
#LEDpanel3
{
    position:absolute;
    top: 60%;
    left: 23%;
    font-weight: bold;
    font-size: 2.8em;
	letter-spacing: 2px;
    text-align: center;
    min-width: 55%;
}

    </style>
       
    <script type="text/javascript">
        
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
        $("#LEDpanel1").css("top", "24%");
        $("#LEDpanel2").css("top", "44%"); 
        $("#LEDpanel3").css("top", "60%"); 
    } 
    window.onresize=resize;
	resize();
});

var pt = 0;
var ut = 0;

var target = 0;
var ticks = 800;
var update = 800;

var debug = false;
var connection = true;

function do_update()
{
    if (connection) {
        send(Cmd.GPS);
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
        document.getElementById("LEDpanel1").innerHTML=" -- -- ";
        document.getElementById("LEDpanel2").innerHTML=" -- -- ";
        document.getElementById("LEDpanel3").innerHTML=" -- -- ";
        return;
    }
    
    if (valid != Cmd.GPS) return;
   
    var val = JSON.parse(target);
     
    document.getElementById("LEDpanel1").innerHTML=round_number(val.A,1);
    document.getElementById("LEDpanel2").innerHTML=round_number(val.la,6)+val.N;
    document.getElementById("LEDpanel3").innerHTML=round_number(val.lo,6)+val.E;

}
    </script>

    </head>
    <body onload="init()">
        <div id="main">
            <div id="LEDpanel1"></div>
            <div id="LEDpanel2"></div>
            <div id="LEDpanel3" title="Click to shift instrument" onclick="nextinstrument();"></div>
            <div><img src="img/gps.png" alt="gps" id="instrument"></div>
        </div>
        <div id="logpanel"></div>
        <script type="text/javascript" src="inc/common.js.php"></script>
    </body>  
</html>
