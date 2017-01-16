<?php
    /*
     * npanel.php
     *
     *  Copyright (C) 2013-2017 by Erland Hedman <erland@hedmanshome.se>
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
        <script type="text/javascript" src="inc/jquery-2.1.1.min.js"></script>
        
<?php if($NIGHT==1) { ?>

        <style>
body:after {
  content: "";
  position: fixed;
  top: 0; bottom: 0; left: 0; right: 0; 
  background: hsla(180,0%,10%,0.80);
  pointer-events: none;
}
        </style>
<?php } ?>
        
        <script type="text/javascript">

var socket;
var ut = 0;
var pt = 0;
var target = 0;
var update = 2000;
var ticks = 1000;
var debug = false;
var connection = true;

var stled = document.createElement("img");
stled.setAttribute("height", "46");
stled.setAttribute("width", "46");

function do_update()
{        
    if (connection) {
        stled.src = 'img/indicator-green.png'
        stled.setAttribute("title", "Server OK");
        document.getElementById("status").appendChild(stled);

        send(Cmd.ServerPing);
        
        if (pt == 0)           
            pt = setInterval(function () {do_poll();}, ticks);
    } else {
        stled.src = 'img/indicator-red.png'
        stled.setAttribute("title", "Server Fail");
        document.getElementById("status").appendChild(stled);
        window.clearInterval(ut);
        reconnect();
    }
}

function do_poll()
{     
    if (connection == false || !(valid == Cmd.ServerPing)) {
        stled.src = 'img/indicator-red.png'
        stled.setAttribute("title", "Server Fail");
        document.getElementById("status").appendChild(stled);
        return;
    }

    var val = JSON.parse(target);
    if (val.status == 0) {
        stled.src = 'img/indicator-yellow.png'
        stled.setAttribute("title", "No data (kplex)");
        document.getElementById("status").appendChild(stled);
    }
    
}

function new_panel()
{
    var options='toolbar=0,status=0,menubar=0,scrollbars=0,location=0,directories=0,resizable=0,width=1280,height=840';
	window.open("npanel.php",'_blank',options);
}

function full_screen() {

    var elem;

    if((navigator.userAgent.indexOf("MSIE") != -1 ) || (!!document.documentMode == true )) //IF IE > 10
      elem = document.body;
    else elem = document.documentElement;
   
    if ((document.fullScreenElement && document.fullScreenElement !== null) ||
        (document.msfullscreenElement && document.msfullscreenElement !== null) ||
        (!document.mozFullScreen && !document.webkitIsFullScreen))
    {
        if (elem.requestFullScreen) {
            elem.requestFullScreen();
        } else if (elem.mozRequestFullScreen) {
            elem.mozRequestFullScreen();
        } else if (elem.webkitRequestFullScreen) {
            elem.webkitRequestFullScreen(Element.ALLOW_KEYBOARD_INPUT);
        } else if (elem.msRequestFullscreen) {
            elem.msRequestFullscreen();
        }
    } else {
        if (document.cancelFullScreen) {
            document.cancelFullScreen();
        } else if (document.mozCancelFullScreen) {
            document.mozCancelFullScreen();
        } else if (document.webkitCancelFullScreen) {
            document.webkitCancelFullScreen();
        } else if (document.msExitFullscreen) {
            document.msExitFullscreen();
        }
    }
}

function do_background(obj)
{
    if (obj.options[obj.selectedIndex].value == "Select")
        return;
        
    if (obj.options[obj.selectedIndex].value == "Night") {
        window.location.href = "<?php echo $_SERVER['SCRIPT_NAME']; ?>?Night=y";
        return;
    } else if (obj.options[obj.selectedIndex].value == "Day") {
        window.location.href = "<?php echo $_SERVER['SCRIPT_NAME']; ?>?Night=n";
        return;
    }
    
    document.body.style.backgroundImage = "url('"+obj.options[obj.selectedIndex].value+"')";
}

function check_overlap()
{
    var bottom_div = document.getElementById("show_bottom");
    var center_div = document.getElementById("center_div");
    
    if (collision($('#top_section'), $('#bottom_section'))) {
        printlog("OVERLAP");
        center_div.style.display = bottom_div.style.display = "none";
   }  else {
        printlog("NO OVERLAP");
        center_div.style.display = bottom_div.style.display = "block";
    }
}

// No need to edit this file for new instruments
// Just place them in the fs here as in-*.php
var instrument_indx = 0;
var instrument = [<?php 
    $items = array();
    exec("cd ".DOCROOT."; ls -l in-*.php | awk '{ print \$NF }'", $items);
    $n = count($items);
    for ($i=0; $i<$n; $i++) {
        echo "'".$items[$i]."'";
        if ($i < $n-1) echo ',';
    } ?>];


$(document).ready(function()
{ 

    var maxi = instrument.length;
    var i = 0;
    var frs = [ document.getElementById("left_fr"),
                document.getElementById("right_fr"),
                document.getElementById("right_fr_b"),
                document.getElementById("left_fr_b"),
                document.getElementById("center_fr")];
                
    for (i=0; i < maxi; i++) {
        if (i >= frs.length) break;
        frs[i].src = instrument[i];
    }
    instrument_indx = i+1;
    
    window.onresize=check_overlap;
	check_overlap();
	<?php echo count($_FILES)? 'document.getElementById("config").style.display = "block";':""; ?>
	document.getElementById("msg").innerHTML="<?php echo $PMESSAGE ?>";
	
	init(); // wsClient
});


function collision($div1, $div2)
{
      var allowed_space = 8; // Test and adjust top collision / bottom collapse
      
      var x1 = $div1.offset().left;
      var y1 = $div1.offset().top+allowed_space;
      var h1 = $div1.outerHeight(true);
      var w1 = $div1.outerWidth(true);
      var b1 = y1 + h1;
      var r1 = x1 + w1;
      var x2 = $div2.offset().left;
      var y2 = $div2.offset().top;
      var h2 = $div2.outerHeight(true);
      var w2 = $div2.outerWidth(true);
      var b2 = y2 + h2;
      var r2 = x2 + w2;

      if (b1 < y2 || y1 > b2 || r1 < x2 || x1 > r2)
        return false;
      return true;
}

function iframeclick(obj)
{
    obj.contentWindow.document.body.onclick = function() {
        obj.src = instrument[instrument_indx++];
        if (instrument_indx >= instrument.length) instrument_indx = 0;
    }
}

function isDecimal(num)
{

    if( /^-?[0-9]+$/.test(num))
       return true;

    return false; 
}

function isIPValid(ip)
{
    var ipaddress = ip.value;
    var patt = /^(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$/;
    var match = ipaddress.match(patt);
    
    if (ipaddress == "0.0.0.0") {
        return false;
    }

    if (ipaddress == "255.255.255.255") {
        return false;
    }

    if (match == null) {
        return false;
    }
       
    return true;
}


function do_config()
{
    document.getElementById("config").style.display = "block";
}

function done_config(val)
{
    var f = document.getElementById("form");
    
    if (val == 0) {
        document.getElementById("config").style.display = "none";
        f.POST_ACTION.value = "OK";
        return;
    }
    
    var nip=f.nnetifs.value;

    for (i=1; i-1<nip; i++) { 

        if (document.getElementById("nwdev-"+i).checked == false)
            continue;
            
        obj=document.getElementById("nwaddr-"+i);
        if (isIPValid(obj) == false) {
            document.getElementById("msg").innerHTML="Invalid I.P address";
            document.getElementById("nifs-"+i).style.display = "block";
            obj.focus();
            return false;
        }
        obj=document.getElementById("nwport-"+i);
        if (isDecimal(obj.value) == false) {
            document.getElementById("msg").innerHTML="Invalid Port number";
            document.getElementById("nifs-"+i).style.display = "block";
            obj.focus();
            return false;
        }
    }

    //f.POST_ACTION.value = "OK";
    f.submit();

}

function submit_file()
{
    var f = document.getElementById("form");
    f.enctype = "multipart/form-data"; 
    f.POST_ACTION.value = "FOK";
    f.submit();
}

function set_used_nwdev(dev)
{
    var f = document.getElementById("form");
    
    var obj = document.getElementById("nwdev-use-"+dev);

    var nip = f.nnetifs.value;

    for (i=1; i-1<nip; i++) {
        document.getElementById("nwdev-use-"+i).value = "off";
    }

    obj.value = document.getElementById("nwdev-"+dev).checked == true? "on":"off";
}

function nifstypeset(dev, stat)
{
    document.getElementById("nifstype-"+dev).style.display = stat == 1? "block":"none";
    obj = document.getElementById("nwaddr-"+dev);
    if (stat == 1) {
        obj.readOnly = false;       
    } else {
        obj.value = obj.defaultValue;
        obj.readOnly = true;
    }
}

</script>

    </head>
    <body>
 
    <div id="screen_ctrl">
        <select title="Backgroud" onchange="do_background(this);">
            <option value="Select">Select</option>
            <?php
                $items = array(); // Place backgrouds in the fs here:
                exec("cd ".DOCROOT."/img/bg; ls -l * | awk '{ print \$NF }'", $items);
                $n = count($items);
                for ($i=0; $i<$n; $i++) {
                    $f=preg_split('/\./',$items[$i]);
                    echo '<option value="img/bg/'.$items[$i].'">'.$f[0].'</option>';
                }
            ?>
            
            <option value="Night">Night</option>
            <option value="Day">Day</option>
        </select>
        <!-- <input title="New panel" type="button" onclick="new_panel();" value="New"> -->
        <input title="Settings" type="button" onclick="do_config();" value="Settings">
        <input title="Fullscreen/F11" type="button" value="Fullscreen" onclick="full_screen()">
    </div>
    
    <div id="status"></div>
    
    <div id="top_section">
        <div id="left_div">   
            <iframe src="tbd" id="left_fr" onload="iframeclick(this)">
                <p>Your browser does not support iframes.</p>
            </iframe>
        </div>
        <div id="right_div">       
            <iframe src="tbd" id="right_fr" onload="iframeclick(this)"></iframe>
        </div>     
         <div id="center_div">       
            <iframe src="tbd" id="center_fr" onload="iframeclick(this)"></iframe>
        </div>
    </div>
        
    <div id="bottom_section"> 
        <div id="show_bottom">    
            <div id="left_div_b">
                <iframe src="tbd" id="left_fr_b" onload="iframeclick(this)"></iframe> 
            </div>
            <div id="right_div_b">       
                <iframe src="tbd" id="right_fr_b" onload="iframeclick(this)"></iframe>
            </div>          
        </div>  
    </div>
                                          
    <div id="config"> <!-- Configuration Page -->
    <form name="form" id="form" method="post" action="<?php echo $_SERVER['SCRIPT_NAME']; ?>" onsubmit="return done_config(1);">
    <input name="POST_ACTION" value="OK" type="hidden">
    <input type="hidden" name="MAX_FILE_SIZE" value="<?php echo MAXFILESIZE; ?>">
    <input id="nttys" name="nttys" value="0" type="hidden">
    <input id="nnetifs" name="nnetifs" value="0" type="hidden">
    <input type="button" id="quitb" value="quit" onclick="done_config(0);">
                        
    <table style="padding:6px;padding-top:30px">
        <tr>
            <td style="width:33%"><h1>Instrument Settings</h1></td>
            <td style="width:33%"><h1>KPlex Settings</h1></td>  
            <td style="width:33%"><h1>Network Settings</h1></td>
        </tr>
    <tr>
        <td> <!-- Left Column -->
        <table>
            <tr>
                <td class="contentBox">
                    <h2>Google Map</h2>
                    Zoom: <select name="map_zoom" title="Zoom factor">
                          <option <?php echo $map_zoom==10? "selected ":""; ?>value="10">10</option>
                          <option <?php echo $map_zoom==12? "selected ":""; ?>value="12">12</option>
                          <option <?php echo $map_zoom==14? "selected ":""; ?>value="14">14</option>
                          <option <?php echo $map_zoom==16? "selected ":""; ?>value="16">16</option>
                          <option <?php echo $map_zoom==18? "selected ":""; ?>value="18">18</option>
                    </select>
                    Update: <select name="map_updt" title="Update rate in seconds">
                          <option <?php echo $map_updt==2?  "selected ":""; ?>value="2">2</option>
                          <option <?php echo $map_updt==6?  "selected ":""; ?>value="6">6</option>
                          <option <?php echo $map_updt==8?  "selected ":""; ?>value="8">8</option>
                          <option <?php echo $map_updt==10? "selected ":""; ?>value="10">10</option>
                          <option <?php echo $map_updt==12? "selected ":""; ?>value="12">12</option>
                    </select> 
                </td>
            </tr>
            <tr>
                <td class="contentBox"> 
                    <h2>Depth Sounder</h2>
                    Warning: <select name="depth_vwrn" title="Visible shallow water warning (metric)">
                          <option <?php echo $depth_vwrn==2?  "selected ":""; ?>value="2">2</option>
                          <option <?php echo $depth_vwrn==3?  "selected ":""; ?>value="3">3</option>
                          <option <?php echo $depth_vwrn==4?  "selected ":""; ?>value="4">4</option>
                          <option <?php echo $depth_vwrn==5?  "selected ":""; ?>value="5">5</option>
                          <option <?php echo $depth_vwrn==8?  "selected ":""; ?>value="8">8</option>
                          <option <?php echo $depth_vwrn==10? "selected ":""; ?>value="10">10</option>
                          <option <?php echo $depth_vwrn==12? "selected ":""; ?>value="12">12</option>
                          <option <?php echo $depth_vwrn==14? "selected ":""; ?>value="14">14</option>
                          <option <?php echo $depth_vwrn==16? "selected ":""; ?>value="16">16</option>
                    </select> 
                    Transp: <select name="depth_transp" title="Transponder depth (metric)">
                          <option <?php echo $depth_transp==0.2? "selected ":""; ?>value="0.2">0.2</option>
                          <option <?php echo $depth_transp==0.3? "selected ":""; ?>value="0.3">0.3</option>
                          <option <?php echo $depth_transp==0.4? "selected ":""; ?>value="0.4">0.4</option>
                          <option <?php echo $depth_transp==0.5? "selected ":""; ?>value="0.5">0.5</option>
                          <option <?php echo $depth_transp==0.6? "selected ":""; ?>value="0.6">0.6</option>
                          <option <?php echo $depth_transp==0.7? "selected ":""; ?>value="0.7">0.7</option>
                          <option <?php echo $depth_transp==0.8? "selected ":""; ?>value="0.8">0.8</option>
                          <option <?php echo $depth_transp==0.9? "selected ":""; ?>value="0.9">0.9</option>
                          <option <?php echo $depth_transp==1.0? "selected ":""; ?>value="1.0">1.0</option>
                    </select> 
                </td>
            </tr>
            <tr>
                <td class="contentBox"> 
                    <h2>Sumlog</h2>
                    Display: <select name="smlog_disp" title="LED Display content">
                          <option <?php echo $smlog_disp==1? "selected ":""; ?>value="1">Speed</option>
                          <option <?php echo $smlog_disp==2? "selected ":""; ?>value="2">Trip</option>
                          <option <?php echo $smlog_disp==3? "selected ":""; ?>value="3">Total</option>
                    </select> 
                    Cal: <select name="smlog_calb" title="Calibration factor">
                          <option <?php echo $smlog_calb==2?  "selected ":""; ?>value="2">2</option>
                          <option <?php echo $smlog_calb==3?  "selected ":""; ?>value="3">3</option>
                          <option <?php echo $smlog_calb==4?  "selected ":""; ?>value="4">4</option>
                          <option <?php echo $smlog_calb==5?  "selected ":""; ?>value="5">5</option>
                          <option <?php echo $smlog_calb==8?  "selected ":""; ?>value="8">8</option>
                          <option <?php echo $smlog_calb==10? "selected ":""; ?>value="10">10</option>
                          <option <?php echo $smlog_calb==12? "selected ":""; ?>value="12">12</option>
                          <option <?php echo $smlog_calb==14? "selected ":""; ?>value="14">14</option>
                          <option <?php echo $smlog_calb==16? "selected ":""; ?>value="16">16</option>
                    </select> 
                </td>
            </tr>
        </table>
        </td>
        
        <td>  <!-- Center Column -->
        <table>
            <tr>
                <td class="contentBox">
                    <h2>Serial</h2>
                   <?php print_serInterfaces(); ?>
                </td>
            </tr>
            <tr>
                <td class="contentBox">
                    <h2>Replay from File</h2>
                    <label title="Select existing file">File:&nbsp;&nbsp;<?php print_nmea_recordings(); ?></label>
                    <label title="New File:">
                        New:&nbsp;<input style="max-width:80%" name="uploaded_file" type="file" accept="text/plain"></label>
                    <label title="Sentences per second">Rate:&nbsp;<select name="nmea_rate">
                          <option value="1">1</option>
                          <option value="3">3</option>
                          <option selected value="5">5</option>
                          <option value="7">7</option>
                          <option value="9">9</option>
                          <option value="11">11</option>
                          <option value="13">13</option>
                          <option value="24">24</option>
                          <option value="48">48</option>
                        </select>
                    </label>
                    <input style="position:relative;left:30%;" type="submit" title="Play this file" value="Play" onclick="submit_file();">
                    
                </td>
            </tr>
            <tr><td style="height:100%"></td></tr>
        </table>
        </td>
        
        <td> <!-- Right Column -->
        <table>
            <tr>
                <td class="contentBox">
                    <h2 title="All listeners must be set accordingly">Network  Properties</h2>
                    <?php print_netInterfaces(); ?>
                    
                </td>
            </tr>
        </table>
        </td>
       
    </tr>
    <tr><td colspan="3" style="text-align: left;"><div id="msg"></div></td></tr>
    <tr><td colspan="3" style="text-align: right;"><input type="submit" title="Save settings" value="Save"></td></tr>
    </table> 
    </form>
    </div>
    
    <div id="logpanel"></div>      
    <script type="text/javascript" src="inc/wsClient.js.php"></script>
       
    </body>
</html>
