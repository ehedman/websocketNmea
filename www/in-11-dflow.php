<?php
    header('Cache-Control: no-cache, no-store, must-revalidate');
    header('Pragma: no-cache');
    header('Expires: 0');
?>
<!DOCTYPE html>
<html lang="en">
    <head>
        <meta http-equiv="Content-Type" content="text/html; charset=utf-8">
        <title>WaterTank</title>
        <script src="inc/jquery-2.1.1.min.js"></script>
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
    top: 28%;
    left: 35%;
    font-weight: bold;
    font-size: 4.8em;
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
    font-size: 4.8em;
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
        $("#LEDpanel1").css("top", "24%");
        $("#LEDpanel2").css("top", "44%"); 
        $("#LEDpanel3").css("top", "60%"); 
    } 
*/
    window.onresize=resize;
    resize();
});

var pt = 0;
var ut = 0;

var target = 0;
var ticks = 4000;
var update = 4000;
var valid = "";
var msg = "";
var msg1 = "";
const amonth = 2628288;

var debug = true;
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

function formatDate(date) {
    var d = new Date(date),
        month = '' + (d.getMonth() + 1),
        day = '' + d.getDate(),
        year = d.getFullYear();

    if (month.length < 2) 
        month = '0' + month;
    if (day.length < 2) 
        day = '0' + day;

    return [year, month, day].join('-');
}

function ToLocalDate(inDate) {

    var date = new Date(0);
    return formatDate(date.setUTCSeconds(inDate));
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

    var msg0 = "Expires";
    var epoch = parseInt(val.fdate, 10);
    var curepoch = parseInt(val.date, 10);

    if (epoch < curepoch + (amonth/2)) {
        msg0 = "Expired";
    } else if (epoch < curepoch + amonth) {
        msg0 = "About to expire";
    }

    msg1 = ToLocalDate(val.fdate);

    var lp1 = document.getElementById("LEDpanel1");
    var lp2 = document.getElementById("LEDpanel2");

    lp1.innerHTML=round_number(val.tank,0);
    lp2.innerHTML=round_number(val.tvol,0);
    
    lp3 = document.getElementById("LEDpanel3");
    lp4 = document.getElementById("LEDpanel4");

    lp3.innerHTML=msg0;
    lp4.innerHTML=msg1;
    lp3.title = lp4.title = "Filter status";

    var totv = parseInt(val.tvol, 10);
    var tnkv = parseInt(val.tank, 10);
    var vleft = Math.round(((tnkv-totv)/tnkv)*100);
    lp1.title = "Volume left " + vleft + "%";
    lp2.title = "Volume left " + vleft + "%";

}
    </script>

    </head>
    <body onload="init()">
        <div id="main">
            <div id="LEDpanel1"></div>
            <div id="LEDpanel2"></div>
            <div id="LEDpanel3"></div>
            <div id="LEDpanel4"></div>
            <div><img src="img/dflow.png" title="Click to shift instrument" alt="watertank" id="instrument" onclick="nextinstrument();"></div>
        </div>
        <div id="logpanel"></div>
    </body>  
</html>
