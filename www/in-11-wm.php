<!DOCTYPE html>
<html>
    <head>
        <meta http-equiv="Content-Type" content="text/html; charset=utf-8">
        <title>WaterMaker</title>
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
    font-size: 1.8em;
	letter-spacing: 2px;
    text-align: center;
    min-width: 55%;
}

#LEDpanel4
{
    position:absolute;
    top: 66%;
    left: 23%;
    font-weight: bold;
    font-size: 1.8em;
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
var valid = "";
var msg = "";
var msg1 = "";

var debug = false;
var connection = true;

function do_update()
{
    if (connection) {
        send("210");
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
        document.getElementById("LEDpanel3").innerHTML="";
        document.getElementById("LEDpanel4").innerHTML="";
        return;
    }
    
    if (valid != "210") return;
   
    var val = JSON.parse(target);
    msg1 = "";
    if (val.runs == 10002) {
        document.getElementById("LEDpanel1").innerHTML=round_number(val.condu,2);
        document.getElementById("LEDpanel2").innerHTML=round_number(val.temp,1);
        
        if (val.quality == 1200) {
            msg="PRD " + val.flowh + " L/H, "+ val.flowt +" L";
            if (val.pvol > 0) msg1 = "OF PRESET "+ val.pvol +" L"; 
        } else if (val.quality == 2200) msg="PRD WRN";
        else if (val.quality == 4000) msg="PRD to Waste";
        if (val.flowh + val.flowt == 0) { msg = "NO PRODUCTION"; msg1 = "";}
        document.getElementById("LEDpanel3").innerHTML=msg;
        document.getElementById("LEDpanel4").innerHTML=msg1;
    } else {
        document.getElementById("LEDpanel1").innerHTML="-";
        document.getElementById("LEDpanel2").innerHTML="-";
        document.getElementById("LEDpanel3").innerHTML="NO PRODUCTION";
        document.getElementById("LEDpanel4").innerHTML="";
    }

}
    </script>

    </head>
    <body onload="init()">
        <div id="main">
            <div id="LEDpanel1"></div>
            <div id="LEDpanel2"></div>
            <div id="LEDpanel3"></div>
            <div id="LEDpanel4"></div>
            <div><img src="img/wm.png" title="Click to shift instrument" alt="watermaker" id="instrument" onclick="nextinstrument();"></div>
        </div>
        <div id="logpanel"></div>
        <script type="text/javascript" src="inc/common.js.php"></script>
    </body>  
</html>
