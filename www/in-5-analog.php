<!DOCTYPE html>
<html lang="en">
    <head>
        <title>Analog Panel Board</title>
        <meta http-equiv="Content-Type" content="text/html; charset=utf-8">     
        <meta name="description" content="hedmanshome.se Marine GlassCockpit">
        <meta name="author" content="Erland Hedman">
        <meta http-equiv="Pragma" content="no-cache">
        <meta http-equiv="Expires" content="-1">
        <link rel="icon" href="img/icon.ico">
        <link rel="stylesheet" type="text/css" href="inc/navi.css">
        <script type="text/javascript" src="inc/pako.js"></script>
        <script type="text/javascript" src="inc/common.js.php"></script>

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
            margin: 165px 0 0 0;
            width: 100%;
            overflow: hidden;
        }

        #next-butt
        {
            z-index:100;
            position:fixed;
            top:165px;
            left:0px;
            height:40px;
            width:40px;
            border-radius:50%;
            -moz-border-radius:50%;
        }
         #chart-butt
        {
            z-index:100;
            position:fixed;
            top:165px;
            left:260px;
            height:40px;
            width:40px;
            border-radius:50%;
            -moz-border-radius:50%;
        }
        </style>

    </head>
    <body onload="init()">
    <div>
        <div id="adc_left_div">   
            <iframe width="256" height="256" frameborder="0" border="0" cellspacing="0" scrolling=no src="in-8-volt.php" id="adc_left_fr">
                <p>Your browser does not support iframes.</p>
            </iframe>
        </div>
         <div id="adc_center_div">       
            <iframe  width="256" height="256" frameborder="0" border="0" cellspacing="0" scrolling=no src="in-9-curr.php" id="adc_center_fr"></iframe>
        </div>
        <div id="adc_right_div">       
            <iframe  width="256" height="256" frameborder="0" border="0" cellspacing="0" scrolling=no src="in-10-temp.php" id="adc_right_fr"></iframe>
        </div> 
        <input id="next-butt" type="button" value="N" title="Click to shift instrument" onclick="nextinstrument();">
        <input id="chart-butt" type="button" value="P" title="Click to show Power Chart" onclick="openInNewTab('power.php');">
    </div>
    </body>
</html>
