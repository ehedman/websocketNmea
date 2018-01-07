<?php
    /*
     * in-5-analog.php
     *
     *  Copyright (C) 2013-2018 by Erland Hedman <erland@hedmanshome.se>
     *
     * This program is free software; you can redistribute it and/or
     * modify it under the terms of the GNU General Public License
     * as published by the Free Software Foundation; either version
     * 2 of the License, or (at your option) any later version.
     */
     
    define('DOCROOT', dirname(__FILE__)); 
    define('KPCONFPATH', DOCROOT.'/inc/kplex.conf');
    define('NAVIDBPATH', DOCROOT.'/inc/navi.db');
    define('FIFOKPLEX', "/tmp/fifo_kplex");
    define('MAXFILESIZE', "10000000");

    require  DOCROOT.'/inc/npanel-inc.php';
    
?>
<!DOCTYPE html>
<html lang="en">
    <head>
        <title>Glass Cockpit at <?php echo gethostname(); ?></title>
        <meta http-equiv="Content-Type" content="text/html; charset=utf-8">     
        <meta name="description" content="hedmanshome.se Marine GlassCockpit">
        <meta name="author" content="Erland Hedman">
        <meta name="license" content="GPL">
        <meta http-equiv="Pragma" content="no-cache">
        <meta http-equiv="Expires" content="-1">
        <link rel="icon" href="img/icon.ico">
        <link rel="stylesheet" type="text/css" href="inc/navi.css">
        <script type="text/javascript" src="inc/common.js.php"></script>

        <style>
        html>body
        {
            background:transparent;
            margin:0;
            padding:0;
            margin: 0 0 0 0;
            width: 100%;
            overflow: hidden;
        }
        </style>

    </head>
    <body onload="init()">
    <div>
        <div id="adc_left_div">   
            <iframe scrolling=no src="in-8-volt.php" id="adc_left_fr">
                <p>Your browser does not support iframes.</p>
            </iframe>
        </div>
         <div id="adc_center_div">       
            <iframe scrolling=no src="in-9-curr.php" id="adc_center_fr"></iframe>
        </div>
        <div id="adc_right_div">       
            <iframe scrolling=no src="in-10-temp.php" id="adc_right_fr"></iframe>
        </div> 
        <input style="z-index:100;position:fixed;top:0;" type="button" value="Next" title="Click to shift instrument" onclick="nextinstrument();">
    </div>
    </body>
</html>
