<!DOCTYPE html>
<html>
    <head>
        <title>Google Maps</title>
        <meta http-equiv="Content-Type" content="text/html; charset=utf-8">
        <meta name="viewport" content="initial-scale=6.0, user-scalable=no">
        <script type="text/javascript" src="inc/jquery-2.1.1.min.js"></script>
        <script src="https://maps.googleapis.com/maps/api/js?v=3.exp&language=us"></script>
<?php
        define('DOCROOT', dirname(__FILE__));
        define('NAVIDBPATH', DOCROOT.'/inc/navi.db');

        $key = "";
        if (file_exists (NAVIDBPATH)) {
            try {
                $DBH = new PDO('sqlite:'.NAVIDBPATH);
                $DBH->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION); 
                $stmt = $DBH->prepare("SELECT key from gmap WHERE Id=1"); 
                $stmt->execute(); 
                $row = $stmt->fetch();
                $key=$row['key'];
            } catch(PDOException $e) {}
        }
        if (strlen($key)) { ?>
        <script async defer src="https://maps.googleapis.com/maps/api/js?key=<?php echo $key; ?>&callback=initMap" type="text/javascript"></script><?php } ?>


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
      
#googlemaps {
    position:absolute;
    top: 9%;
    left: 9%;
    height: 82%;
    width: 82%;
    max-height: 512px;
    border-radius:50%;
    -moz-border-radius:50%;
    background-color: black;
    z-index: 1;
    visibility: hidden;
}

#instrument
{
    position:absolute;
    height: 100%;
    width: 100%;
    max-height: 512px;
    border-radius:50%;
    border-radius:50%;
    -moz-border-radius:50%;
    background-color: black;
    z-index: 0;
}

#main {
    position: relative;
    height: 100%;
    z-index: 0;
}

#logpanel
{
    position:relative;
    bottom: 0px;
    left: 16%;
    font-size: 1.3em;
}
        </style>
        
        <script>
        
var ut = 0;
var target = 0;
var update = 3000;
var debug = false;
var connection = true;
var map;
var aisupdate = 2;
var stationary = 0.3;
var amarkers = [];
var gmarker;
var first = true;

function setWindowSize(){
   
    var W = window.innerWidth;
    
    W=W>512?512:W;
    
    document.getElementsByTagName('body')[0].style.height = W + "px";
    document.getElementsByTagName('body')[0].style.width = W + "px";
}

$(document).ready(function()
{ 
    window.onresize=setWindowSize;
        setWindowSize();  
});

function do_update()
{
    if (connection) {
        if (--aisupdate <= 0) {
            aisupdate = 2;
            send(Cmd.GoogleAisFeed);
        } else {
            send(Cmd.GoogleMapFeed);
        }
    } else {
        window.clearInterval(ut);
        reconnect();
    }

    window.clearInterval(ut);

    if (target == "Exp" ) {    
        setInterval(function () {do_update();}, update);
        return;
    }
   
    if (valid == Cmd.GoogleMapFeed) {


        var val = JSON.parse(target);
        update = parseInt(val.updt)*1000;
        var lap = val.N == "S"? "-":"";
        var lop = val.E == "W"? "-":"";
 
        var nmap = new google.maps.LatLng(lap+val.la, lop+val.lo);
        map.setCenter(nmap, 5);
        if (first == true) {     
            map.setZoom(parseInt(val.zoom));      
        }
        
        if (first) {
            gmarker = new google.maps.Marker({
                position: nmap,
                title: val.myname,
                map: map,
                icon: { 
                        path: google.maps.SymbolPath.FORWARD_OPEN_ARROW,
                        scale: 3,
                        fillColor: "red",
                        fillOpacity: 0.5,
                        strokeColor:"red",
                        strokeWeight: 1,
                        rotation: round_number(val.A,0)   
                    }
            });
            first = false;
        } else {
            gmarker.setPosition(nmap);
            gmarker.setIcon({
                path: google.maps.SymbolPath.FORWARD_OPEN_ARROW,
                scale: 3,
                fillColor: "red",
                fillOpacity: 0.5,
                strokeColor:"red",
                strokeWeight: 1,
                rotation: round_number(val.A,0)
            })
        }

    } else if (valid == Cmd.GoogleAisFeed) {


        var val = JSON.parse(target);

        var emarkers = [];
        

        for (var c in val) { // build list of existing markers to update  
            for(var m=0; m<amarkers.length; m++) {      
                if (val[c].userid == amarkers[m].get('userid')) {
                    var lap = val[c].N == "S"? "-":"";
                    var lop = val[c].E == "W"? "-":"";
                    var newLatLng = new google.maps.LatLng(lap+val[c].la, lop+val[c].lo);
                    amarkers[m].setPosition(newLatLng);
                    var symbp = google.maps.SymbolPath.FORWARD_OPEN_ARROW; 
                    if (val[c].sog <= stationary || val[c].trueh == 360) { val[c].trueh = 0; symbp = google.maps.SymbolPath.CIRCLE; }
                    if (val[c].name == "n.n") val[c].name = val[c].userid;
                    amarkers[m].setTitle(val[c].name + (val[c].sog >stationary? " | SOG " +val[c].sog:""));
                    amarkers[m].setIcon({
                        path: symbp,
                        fillColor: val[c].buddyid==0?"yellow":"white",
                        fillOpacity: 0.5,
                        scale: 3,
                        strokeColor:"red",
                        strokeWeight: 1,
                        rotation: round_number(val[c].trueh,0)
                    })
                    emarkers.push(amarkers[m]);
                }    
            }
        }

        // Check if markers are to be deleted
        for(var a=0; a<amarkers.length; a++) {
            var u = amarkers[a].get('userid');
            var found = false;
            for (var i in val) {
                if (val[i].userid == u) {
                    found = true;
                    break;
                }
            }
            if (found == false)
                amarkers[a].setMap(null); 
        }

        amarkers = []; 

        // (re)bebuild markerlist and add new
        for (var i in val) {

            var found = false;
            for(var e=0; e<emarkers.length; e++) {
                if (emarkers[e].get('userid') == val[i].userid) {
                    found = true;
                    amarkers.push(emarkers[e]);
                    break;
                }
            }

            if (found == true) continue;
 
            // New marker
            var lap = val[i].N == "S"? "-":"";
            var lop = val[i].E == "W"? "-":"";
            var name = val[i].name;
            var nmap = new google.maps.LatLng(lap+val[i].la, lop+val[i].lo); 
            var symbp = google.maps.SymbolPath.FORWARD_OPEN_ARROW; 
            if (val[i].sog <= stationary || val[c].trueh == 360) { val[i].trueh = 0; symbp = google.maps.SymbolPath.CIRCLE; }
            if (val[i].name == "n.n") val[i].name = val[i].userid;
            var amarker = new google.maps.Marker({
                position: nmap,
                title: val[i].name + (val[i].sog >stationary? " | SOG " +val[i].sog:""),
                map: map,
                icon : {
                        path: symbp,
                        fillColor: "yellow",
                        fillOpacity: 0.5,
                        scale: 3,
                        strokeColor:"red",
                        strokeWeight: 1,
                        rotation: round_number(val[i].trueh,0)
                       }
                });

            amarker.set('userid', val[i].userid);
            google.maps.event.addListener(amarker, 'click', function() {
                var name = this.get('title').split("|");
                if (confirm("Save/Remove " + name[0] + " from buddyList?")) {
                    send(Cmd.GoogleAisBuddy + "-" + this.get('userid'));
                }
            });
            amarkers.push(amarker);
        }
    }
    
    ut = setInterval(function () {do_update();}, update);
}

function initialize() {
    
    var myLatlng = new google.maps.LatLng("51.47879", "-0.010677"); // Greenwich

    var mapOptions = {
        zoom: 12,
        center: myLatlng,
        mapTypeId: google.maps.MapTypeId.SATELLITE,
        disableDefaultUI: true
    };
  
    map = new google.maps.Map(document.getElementById('googlemaps'), mapOptions);

    gmarker = new google.maps.Marker({
        position: myLatlng,
        map: map,
        icon: {
                path: google.maps.SymbolPath.CIRCLE,
                fillColor: "red",
                fillOpacity: 0.5,
                strokeColor:"red",
                strokeWeight: 1,
                scale: 6,
        }
    });    
    
    google.maps.event.addListener(map, 'tilesloaded', function(){
        document.getElementById('googlemaps').style.position = 'absolute';
        document.getElementById('googlemaps').style.backgroundColor = 'black';
        document.getElementById('googlemaps').style.zIndex = '0';
        document.getElementById('googlemaps').style.visibility = 'visible';
    });

}

google.maps.event.addDomListener(window, 'load', initialize);

    </script>
        
    </head>
    <body onload="init()">
        <div id="main">
            <img id="instrument" src="img/empty.png" title="Click to shift instrument" alt="instrument" onclick="nextinstrument();">
            <div id="googlemaps"></div>
        </div>        
        <div id="logpanel"></div>
        <script type="text/javascript" src="inc/common.js.php"></script>  
    </body>
</html>


