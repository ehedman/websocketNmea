<?php
    header('Cache-Control: no-cache, no-store, must-revalidate');
    header('Pragma: no-cache');
    header('Expires: 0');
?>
<!DOCTYPE html>
<html lang="en">
    <head>
        <title>Analog Panel Board</title>
        <meta http-equiv="Content-Type" content="text/html; charset=utf-8">     
        <meta name="description" content="hedmanshome.se Marine GlassCockpit">
        <meta name="author" content="Erland Hedman">
        <link rel="icon" href="img/icon.ico">
        <link rel="stylesheet" type="text/css" href="inc/navi.css">
        <script src="inc/pako.js"></script>
        <script src="inc/common.js.php"></script>

        <script>
            function openInNewTab(url) {
              var win = window.open(url, '_blank');
              win.focus();
            }
        </script>

        <style>
        html>body
        {
            background:transparent;
            padding:0;
            width: 100%;
            overflow: hidden;
        }

        #next-butt
        {
            z-index:100;
            position:relative;
            top:175px;
            margin-left:100px;
            height:40px;
            width:40px;
            font-weight: bold;
            border-radius:50%;
            -moz-border-radius:50%;
        }
        #chart-butt
        {
            z-index:100;
            position:relative;
            top:175px;
            margin-left:246px;
            height:40px;
            width:40px;
            border-radius:50%;
            -moz-border-radius:50%;
            font-weight: bold;
        }

     </style>

    </head>
    <body onload="init()">
    <div id="adc_main_div">
        <div id="adc_left_div">   
            <iframe width="256" height="256" scrolling=no src="in-8-volt.php" id="adc_left_fr"></iframe>
        </div>
        <div id=adc_r2_div>
            <div id="adc_right_div">       
                <iframe  width="256" height="256" scrolling=no src="in-9-curr.php" id="adc_right_fr"></iframe>
            </div>
             <div id="adc_center_div">       
                <iframe  width="256" height="256" scrolling=no src="in-10-temp.php" id="adc_center_fr"></iframe>
            </div>
        </div>
        <input id="next-butt" type="button" value="N" title="Click to shift instrument" onclick="nextinstrument();">
        <input id="chart-butt" type="button" value="P" title="Click to show Power Chart" onclick="openInNewTab('power.php');">
    </div>
    </body>
</html>
