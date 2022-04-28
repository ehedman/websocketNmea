<?php
    /*
     * npanel.php
     *
     *  Copyright (C) 2013-2019 by Erland Hedman <erland@hedmanshome.se>
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
    define('FIFOPNMEA', "/tmp/fifo_nmea_p");
    define('MAXFILESIZE', "10000000");
    
    require  DOCROOT.'/inc/npanel-inc.php';

    $NULLPAGE = "http://".$_SERVER['HTTP_HOST'].dirname($_SERVER['PHP_SELF'])."/null.html";
//    header('Cache-Control: no-cache, no-store, must-revalidate');
//    header('Pragma: no-cache');
//    header('Expires: 0');

    $hu_Agent = $_SERVER['HTTP_USER_AGENT'];

    $u_Agent = "";
    if (preg_match('/\(iP/i', $hu_Agent) && preg_match('/Safari/i', $hu_Agent))
    {
        $u_Agent = "Safari";
    }

    $u_IsPad = "";
    if (preg_match('/Android/i', $hu_Agent) || preg_match('/OS X/i', $hu_Agent))
    {
        $u_IsPad = "_pad";
    }
?>
<!DOCTYPE html>
<html lang="en">
    <head>
        <title>Glass Cockpit at <?php echo gethostname(); ?></title>
        <meta http-equiv="Content-Type" content="text/html; charset=utf-8">     
        <meta name="description" content="hedmanshome.se Marine GlassCockpit">
        <meta name="author" content="Erland Hedman">
        <meta name="license" content="GPL">
        <link rel="icon" href="img/icon.ico">
        <link rel="stylesheet" type="text/css" href="inc/navi.css">
        <script src="inc/jquery-2.1.1.min.js"></script>
        <script  src="inc/pako.js"></script>
        <script src="inc/common.js.php"></script>
<?php if ($NOSAVE==1 ) {?>        <script src="inc/webtoolkit.md5.js"></script><?php } ?>
        
<?php if($NIGHT==1) { ?>

        <style>
body:after {
  content: "";
  position: fixed;
  top: 0; bottom: 0; left: 0; right: 0; 
  background: hsla(180,0%,10%,0.80);
  pointer-events: none;
  z-index: 20;
}
        </style>
<?php } ?>

        <script>

var socket;
var ut = 0;
var pt = 0;
var target = 0;
var update = 2000;
var ticks = 1000;
var debug = false;
var connection = true;
var underConfig = false;
var aisping = 1;
var tdtping = 1;
var relayping = 1;
var pollitem = 0;

var stled = document.createElement("img");
stled.setAttribute("height", "46");
stled.setAttribute("width", "46");

function do_update()
{
    if (connection) {
        stled.src = 'img/indicator-green.png'
        stled.setAttribute("title", "Server OK");
        document.getElementById("status").appendChild(stled);

        if (document.getElementById("a2dserial").value == "/dev/null" || document.getElementById("a2dserial").value == "")
            document.getElementById("relayContent").style.display = "none";
        else document.getElementById("relayContent").style.display = "block";

        switch (pollitem++)
        {
            case 0: send(Cmd.ServerPing); break;
            case 1: send(Cmd.StatusReport + "-" + "Status"); break;              
            default: pollitem = 0; break;
        }
       
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

function setRelayStatus(mask)
{
    document.getElementById("relay1").checked = (1 & mask); 
    document.getElementById("relay2").checked = (2 & mask);
    document.getElementById("relay3").checked = (4 & mask);
    document.getElementById("relay4").checked = (8 & mask); 
}

function setTdtStatus(mask)
{
 
    var err = (mask & (1 << 5)) === 0 ? 0 : 1;

    if (err)
    {      
        if ((1 & mask)) {
            document.getElementById("tdt-status-1-label").style.color = "red";
        }
        if ((2 & mask)) {
            document.getElementById("tdt-status-2-label").style.color = "red";
        }
    } else {
        document.getElementById("tdt-status-1-label").style.color = "black";
        document.getElementById("tdt-status-2-label").style.color = "black";
        document.getElementById("tdt-status-1").checked = (1 & mask);
        document.getElementById("tdt-status-2").checked = (2 & mask);
    }
}

function do_poll()
{
    var val = JSON.parse(target);

    if (valid == Cmd.StatusReport)
    {
        if (--relayping == 0) {
            relayping++;
            setRelayStatus(val.relaySts);
        }

        if (--aisping == 0) {
            aisping++;

            if (val.aisTxSts == -1) {
                 document.getElementById("trx-status-div").style.display = "none";
            } else {        
                document.getElementById("trx-status-div").style.display = "inline-block";
                document.getElementById("trx-status").checked = val.aisTxSts == 1? true : false;
            }
        }

        if (--tdtping == 0) {
            tdtping++;

            if (val.tdtSts == -1) {
                document.getElementById("tdt-status-tr").style.display = "none";
            } else {
                setTdtStatus(val.tdtSts);
            }
        }

        if (val.nmRec.length) {
            document.getElementById("msg").innerHTML=": Recording of NMEA stream to file " + val.nmRec + " in progress";
            document.getElementById("recordAction").value = "Stop";
        } else {
            document.getElementById("msg").innerHTML=":";
            document.getElementById("recordAction").value = "Record";
        }

        document.getElementById("recordAction").disabled = false;

        if (val.nmPlay.length && !val.nmRec.length) {
            document.getElementById("msg").innerHTML=": Replaying of NMEA stream from file " + val.nmPlay;
        }

<?php if ($NOSAVE==1 ) {?>
        if (val.Authen.length) {
            docheckpw(val.Authen);
        } else {
            docheckpw(4);
        }
<?php } ?>
        
        return;
    }

<?php if ($NOSAVE==1 ) {?>
    if (valid == Cmd.Authentication) {
        if (val.Authen.length) {
            docheckpw(val.Authen);
        }
    }
<?php } ?>

    if (connection == false || !(valid == Cmd.ServerPing)) {
        stled.src = 'img/indicator-red.png';
        stled.setAttribute("title", "Server Fail");
        document.getElementById("status").appendChild(stled);
        return;
    }

    if (valid == Cmd.ServerPing && val.status == 0) {
        stled.src = 'img/indicator-yellow.png'
        stled.setAttribute("title", "No data (kplex)");
        document.getElementById("status").appendChild(stled);
    }
    
}

function dorelay()
{
    var rlbits = 0;
    if (document.getElementById("relay1").checked == true) {
        rlbits |= (1 << 0);
    }
    if (document.getElementById("relay2").checked == true) {
        rlbits |= (1 << 1);
    }
    if (document.getElementById("relay3").checked == true) {
        rlbits |= (1 << 2);
    }
    if (document.getElementById("relay4").checked == true) {
        rlbits |= (1 << 3);
    }
    relayping = 5;
    send(Cmd.SensorRelay + "-" + rlbits);
}


function doTdt()
{
    var tdbits = 0;
    if (document.getElementById("tdt-status-1").checked == true) {
        tdbits |= (1 << 0);
    }
    if (document.getElementById("tdt-status-2").checked == true) {
        tdbits |= (1 << 1);
    }

    tdtping = 5;
    send(Cmd.tdtStatus + "-" + tdbits);
}

function doAis(cb)
{
    var status = cb.checked == true? 1:0;
    aisping = 5;
    send(Cmd.AisTrxStatus + "-" + status);
}

function dosavenmea()
{

    if (document.getElementById("recordAction").value == "Stop") {
        send(Cmd.SaveNMEAstream + "-ABORT");
        return;
    }

    document.getElementById("recordAction").disabled = true;

    var fpath = document.getElementById("record_file");
    if (!fpath.value.trim().length) {
        return;
    }
    if (/[^0-9a-zA-Z\.\-\_/\s]/gi.test(fpath.value.trim())) {
        document.getElementById("msg").innerHTML=": Invalid character(s) in file name";
        fpath.focus();
        return;
    }
   
    document.getElementById("msg").innerHTML=": Recording of NMEA stream to " + fpath.value.trim() + " initiated";

    var m = document.getElementById("record_max");
    send(Cmd.SaveNMEAstream + "-" + fpath.value.trim() + ":" + m.options[m.selectedIndex].value);
}

<?php if ($NOSAVE==1 ) {?>

function docheckpw(seq)
{
    var status;

    if (seq == 0)
        return;

    if (seq == 1) {

        if (document.getElementById("password").value.length < 8)
            return;

        send(Cmd.Authentication + "-" + MD5(document.getElementById("password").value).trim());
        status = true;
    }

    if (seq == 2) {
        document.getElementById("msg").innerHTML=": Authentication denied";
        document.getElementById("dosavePw").checked = false;
        document.getElementById("password").value = "";
        status = true;
    }

    if (seq == 3) {
       status = false;
    }

    if (seq == 4) {
       status = true;
    }
  
    document.getElementById("Play").disabled = status;
    if (status == true) {
        document.getElementById("record_file").value = "";
    }
    document.getElementById("recordAction").disabled = status;
    document.getElementById("record_file").disabled = status;
    document.getElementById("Save").disabled = status;
    document.getElementById("trx-status").disabled = status;
    document.getElementById("tdt-status-1").disabled = status;
    document.getElementById("tdt-status-2").disabled = status;
    document.getElementById("ais-use-cb").disabled = status;
    document.getElementById("relayAction").disabled = status;  
    document.getElementById("dosavePw").disabled = status;   
}

<?php }?>

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

$(document).ready(function()
{ 

    var maxi = instruments.length;
    var i = 0;
    var frs = [ document.getElementById("left_fr"),
                document.getElementById("right_fr"),
                document.getElementById("right_fr_b<?php echo $u_IsPad; ?>"),
                document.getElementById("left_fr_b<?php echo $u_IsPad; ?>"),
                document.getElementById("center_fr")];
                
    for (i=0; i < maxi; i++) {
        if (i >= frs.length) break;
        frs[i].src = instruments[i];
    }

    instrument_indx = i+1;
    
    window.onresize=check_overlap;
    check_overlap();
    <?php echo count($_FILES)? 'document.getElementById("config").style.display = "block";':""; ?>
    document.getElementById("msg").innerHTML=": <?php echo $PMESSAGE ?>";

    init(); // common.js.php
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
    underConfig = true;
    document.getElementById("config").style.display = "block";
}

function enter_key()
{
    var key = prompt("Please enter your Google Map Key"<?php echo strlen($KEY)? ',"'.$KEY.'"':""; ?>);

    document.getElementById("msg").innerHTML=":";
    
    if (key.length) {
        document.getElementById("msg").innerHTML=": Your key: '"+key+"', now save your configuration";
        document.getElementById("gkey").value = key;
        return;
    }  
}

function delete_key()
{
    var key = "<?php echo $KEY; ?>";

    document.getElementById("msg").innerHTML=": Your key: '"+key+"', is to be deleted. Now save your configuration";
    document.getElementById("gkey").value = "invalid";
}

function done_config(val)
{
    var f = document.getElementById("form");
    
    underConfig = false;

    if (val == 0) {
        document.getElementById("config").style.display = "none";
        f.POST_ACTION.value = "OK";
        return;
    }
    var l;
    if ((l=document.getElementById("aisid").value.length) >0) {
        if (l != 9) {
            document.getElementById("msg").innerHTML=": Invalid lenght in field 'Vessel Userid'. Should be 9 digits";
            return false;
        }
        if (isDecimal(document.getElementById("aisid").value) == false) {
            document.getElementById("msg").innerHTML=": Invalid digit(s) in field 'Vessel Userid'";
            return false;
        }
    }

    if (/[^0-9a-zA-Z/\s]/gi.test(document.getElementById("aisname").value)) {
        document.getElementById("msg").innerHTML=": Invalid character(s) in field 'Vessel Name'";
        return false;
    }
    
    var nip=f.nnetifs.value;

    for (i=1; i-1<nip; i++) { 

        if (document.getElementById("nwdev-"+i).checked == false)
            continue;
            
        obj=document.getElementById("nwaddr-"+i);
        if (isIPValid(obj) == false) {
            document.getElementById("msg").innerHTML=": Invalid I.P address";
            document.getElementById("nifs-"+i).style.display = "block";
            obj.focus();
            return false;
        }
        obj=document.getElementById("nwport-"+i);
        if (isDecimal(obj.value) == false) {
            document.getElementById("msg").innerHTML=": Invalid Port number";
            document.getElementById("nifs-"+i).style.display = "block";
            obj.focus();
            return false;
        }
    }

<?php if ($NOSAVE==1 ) {?>
    if (document.getElementById("dosavePw").checked == true) {
        if (document.getElementById("password").value.length > 7) {
            var md5pw = MD5(document.getElementById("password").value);
            document.getElementById("password").value = md5pw.trim();
        } else document.getElementById("password").value = "";
    } else document.getElementById("password").value = "";
<?php } ?>

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

function setaisuse(obj)
{

    document.getElementById("aisuse").value = obj.checked==true? "1":"0";
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

function dragElement(elmnt) {

    var pos1 = 0, pos2 = 0, pos3 = 0, pos4 = 0;
    if (document.getElementById(elmnt.id + "header")) {
      /* if present, the header is where you move the DIV from:*/
      document.getElementById(elmnt.id + "header").onmousedown = dragMouseDown;
    } else {
      /* otherwise, move the DIV from anywhere inside the DIV:*/
      elmnt.onmousedown = dragMouseDown;
    }

    function dragMouseDown(e) {
      e = e || window.event;
      // get the mouse cursor position at startup:
      pos3 = e.clientX;
      pos4 = e.clientY;
      document.onmouseup = closeDragElement;
      // call a function whenever the cursor moves:
      document.onmousemove = elementDrag;
    }

    function elementDrag(e) {
      e = e || window.event;
      // calculate the new cursor position:
      pos1 = pos3 - e.clientX;
      pos2 = pos4 - e.clientY;
      pos3 = e.clientX;
      pos4 = e.clientY;
      // set the element's new position:
      elmnt.style.top = (elmnt.offsetTop - pos2) + "px";
      elmnt.style.left = (elmnt.offsetLeft - pos1) + "px";
    }

    function closeDragElement() {
      /* stop moving when mouse button is released:*/
      document.onmouseup = null;
      document.onmousemove = null;
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
        <input title="Settings" type="button" onclick="do_config();" value="Settings">
        <?php if ($u_Agent != "Safari") { ?>

        <input title="Fullscreen/F11" type="button" value="Fullscreen" onclick="full_screen()">

        <?php } ?>
    </div>
    
    <div id="status"></div>  
    <div id="top_section">
        <div id="left_div">   
            <iframe src="<?php echo $NULLPAGE; ?>" id="left_fr"></iframe>
        </div>
        <div id="right_div">       
            <iframe src="<?php echo $NULLPAGE; ?>" id="right_fr"></iframe>
        </div>     
         <div id="center_div"<?php if ($u_Agent == "Safari") echo 'style="width:100%";';?>>       
            <iframe src="<?php echo $NULLPAGE; ?>" id="center_fr"></iframe>
        </div>
    </div>
        
    <div id="bottom_section">
        <div id="show_bottom">    
            <div id="left_div_b">
                <iframe src="<?php echo $NULLPAGE; ?>" id="left_fr_b<?php echo $u_IsPad; ?>"></iframe> 
            </div>
            <div id="right_div_b">       
                <iframe src="<?php echo $NULLPAGE; ?>" id="right_fr_b<?php echo $u_IsPad; ?>"></iframe>
            </div>          
        </div>  
    </div>
                                         
    <div id="config"> <!-- Configuration Page -->
    <form name="form" id="form" method="post" action="<?php echo $_SERVER['SCRIPT_NAME']; ?>" onsubmit="return done_config(1);">
    <input name="POST_ACTION" value="OK" type="hidden">
    <input type="hidden" name="MAX_FILE_SIZE" value="<?php echo MAXFILESIZE; ?>">
    <input id="nttys" name="nttys" value="0" type="hidden">
    <input id="nnetifs" name="nnetifs" value="0" type="hidden">
    <input type="hidden" name="aisuse" id="aisuse" value="<?php echo $aisuse; ?>">
    <input type="hidden" name="gkey" id="gkey" value="">
    <input type="button" id="quitb" value="quit" onclick="done_config(0);">
 
                        
    <table style="padding:6px;padding-top:30px">
        <tr>
            <td style="text-align:center;" colspan="3"><h1><?php echo $aisname ?> Settings</h1></td>
        </tr>
    <tr>
        <td style="width:40%;"> <!-- Left Column -->
        <table>
            <tr>
                <td class="contentBox">
                    <h2>Google Map</h2>
                    Zoom: <select name="map_zoom" title="Initial zoom factor">
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
                    </select><br>
                        Enter : <input type="button" value="key" title="Enter a Google Map Key" onclick="enter_key();">
                        <?php if (strlen($KEY)) { ?>Delete : <input type="button" value="key" title="Delete the Google Map Key" onclick="delete_key();"><?php } ?>
                </td>
            </tr>
             <tr>
                <td class="contentBox">
                    <h2>AIS</h2>
                    Vessel Name<br><input <?php echo $aisro==1? "readonly ":""; ?>type="text" style="text-transform:uppercase" name="aisname" title="This vessels' name" id="aisname" maxlength="20" value="<?php echo $aisname ?>"><br>
                    Vessel Callsign<br><input <?php echo $aisro==1? "readonly ":""; ?>type="text" style="text-transform:uppercase" name="aiscallsign" title="This vessels' Callsign" id="aiscallsign" maxlength="20" value="<?php echo $aiscallsign ?>"><br>
                    Vessel Userid<br><input <?php echo $aisro==1? "readonly ":""; ?>type="text" name="aisid" id="aisid" title="This vessels' i.d (MMSI) - nine digits" maxlength="9" value="<?php echo $aisid ?>"><br>
                    Use<input id="ais-use-cb" type="checkbox"<?php echo $NOSAVE==1? " disabled":""; ?> onclick="setaisuse(this);" title="Show AIS on Google Map"<?php echo $aisuse==1? " checked=checked":""; ?>>
                    <div id="trx-status-div" style="display: inline-block;">
                        Transmitter On<input type="checkbox"<?php echo $NOSAVE==1? " disabled":""; ?> id="trx-status" title="Send settings now" onchange="doAis(this)">
                    </div>
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
        
        <td style="width:33%;">  <!-- Center Column -->
        <table>
            <tr>
                <td class="contentBox">
                    <h2>Replay NMEA from File</h2>
                    <label title="Select an existing NMEA file">File:&nbsp;&nbsp;<?php print_nmea_recordings(); ?></label>New:
                    <label for="files" title="Upload a new NMEA file" class="fileLabel">&nbsp;&nbsp;File
                        <input style="max-width:4%;visibility:hidden" id="files" name="uploaded_file" type="file" accept="text/plain"></label>
                    <label title="Sentences per second">Rate:&nbsp;<select name="nmea_rate">
                          <option value="2">2</option>
                          <option value="4">4</option>
                          <option value="6">6</option>
                          <option value="8">8</option>
                          <option value="10">10</option>
                          <option value="12">12</option>
                          <option selected value="14">14</option>
                          <option value="24">24</option>
                          <option value="48">48</option>
                        </select>
                    </label>
                    <input style="position:relative;left:4%;" type="submit" title="Play this file" value="Play"<?php echo $NOSAVE==1? " disabled":""; ?> id="Play" onclick="submit_file();">
                    
                </td>
            </tr>
            <tr>
                <td class="contentBox">
                    <h2>Record NMEA to File</h2>
                    <input type="text" title="Filename to record" id="record_file" maxlength="60"<?php echo $NOSAVE==1? " disabled":""; ?>><br>
                          <select title="Duration" id="record_max">
                          <option value="1">1</option>
                          <option value="4">4</option>
                          <option selected value="6">6</option>
                          <option value="8">8</option>
                          <option value="10">10</option>
                          <option value="30">30</option>
                          <option value="60">60</option>
                          <option value="120">120</option>
                          <option value="180">180</option>
                        </select>&nbsp; minutes                
                    <input style="position:relative;left:10%;" type="button" title="Record NMEA stream to file now" value="Record" id="recordAction" onclick="dosavenmea();">
                    
                </td>
            </tr>
            <tr>
                <td class="contentBox" style="padding-right:16px; padding-left:16px">
                    <h2>Relay Settings</h2>
                    <input type="text" name="a2dserial" title="UK1104 Data Acquisition Module" id="a2dserial" maxlength="20" value="<?php echo $a2dserial ?>"><br>
                    <div id="relayContent">
                        Relay-1<input type="checkbox" id="relay1" onclick="relayping = 5;" title="Relay 1 ON/OFF">
                        <input type="text" title="Description" name ="relay1txt" id="relay1txt" size="9" value="<?php echo $a2dreltxt1 ?>"><input type="text" title="time-out min" name ="relay1tmo" id="relay1tmo" size="2" value="<?php echo $a2drel1tmo ?>"<br>
                        Relay-2<input type="checkbox" id="relay2" onclick="relayping = 5;" title="Relay 2 ON/OFF">
                        <input type="text" title="Description" name ="relay2txt" id="relay2txt" size="9" value="<?php echo $a2dreltxt2 ?>"><input type="text" title="time-out min" name ="relay2tmo" id="relay2tmo" size="2" value="<?php echo $a2drel2tmo ?>"<br>
                        Relay-3<input type="checkbox" id="relay3" onclick="relayping = 5;" title="Relay 3 ON/OFF">
                        <input type="text" title="Description" name ="relay3txt" id="relay3txt" size="9" value="<?php echo $a2dreltxt3 ?>"><input type="text" title="time-out min" name ="relay3tmo" id="relay3tmo" size="2" value="<?php echo $a2drel3tmo ?>"<br>
                        Relay-4<input type="checkbox" id="relay4" onclick="relayping = 5;" title="Relay 4 ON/OFF">
                        <input type="text" title="Description" name ="relay4txt" id="relay4txt" size="9" value="<?php echo $a2dreltxt4 ?>"><input type="text" title="time-out min" name ="relay4tmo" id="relay4tmo" size="2" value="<?php echo $a2drel4tmo ?>"<br>
                        <input type="button"<?php echo $NOSAVE==1? " disabled":""; ?> id="relayAction" value="Send settings" title="Send settings now" onclick="dorelay()">
                    </div>
                </td>
            </tr>
            <tr><td style="height:100%"></td></tr>
        </table>
        </td>
        
        <td style="width:33%;"> <!-- Right Column -->
        <table>
            <tr id="tdt-status-tr">
                <td class="contentBox">
                    <h2>SmartPlug Settings</h2>
                    <label id="tdt-status-1-label" for="tdt-status-1">Outlet-1</label>
                    <input type="checkbox"<?php echo $NOSAVE==1? " disabled":""; ?> id="tdt-status-1" title="Send settings now" onchange="doTdt()">
                    <label id="tdt-status-2-label" for="tdt-status-2">Outlet-2</label>
                    <input type="checkbox"<?php echo $NOSAVE==1? " disabled":""; ?> id="tdt-status-2" title="Send settings now" onchange="doTdt()">
                </td>
            </tr>
            <tr>
                <td class="contentBox">
                    <h2>NMEA Multiplexer I/O</h2>
                   <?php print_serInterfaces(); ?>

                </td>
            </tr>
            <tr>
                <td class="contentBox">
                    <h2 title="All listeners must be set accordingly">Network  Properties</h2>
                    <?php print_netInterfaces(); ?>
                    
                </td>
            </tr>
<?php if ($NOSAVE==1 ) {?>
            <tr>
                <td class="contentBox">
                    <h2 title="Unlock protected fields">Authentication</h2>
                    <input title="Password (8 characters minimum)" type="password" id="password" name="password" autocomplete="off" minlength="8" required><br>
                    Autosave &nbsp;<input type="checkbox"<?php echo $NOSAVE==1? " disabled":""; ?> title="Save new password on save configuration" id="dosavePw">
                    Confirm &nbsp;<input type="button" title="Set password" value="OK" onclick="docheckpw(1);">
                </td>
            </tr>
<?php }?>
            
            <tr><td>&nbsp;</td></tr>
            <tr>
                <td class="contentBox">
                    <h2 title="Save and restart server">Save configuration</h2>
                    <input type="submit" title="Save settings" id="Save" value="Save"<?php echo $NOSAVE==1? " disabled":""; ?>>
                </td>
            </tr>
        </table>
        </td>
         
    </tr>
    <tr>
        <td colspan="2" style="text-align: left;"><div id="msg"></div></td>
        <td style="text-align: right">
            <a title="Go to GitHub" href="https://github.com/ehedman/websocketNmea/tree/master" target="_blank">About</a>
        </td>
    </tr>
    </table> 
    </form>
    </div>
    
    <div id="logpanel"></div>      
    <script>dragElement(document.getElementById("config"));</script>
       
    </body>
</html>
