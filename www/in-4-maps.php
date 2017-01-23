<!DOCTYPE html>
<html>
    <head>
        <title>Google Maps</title>
        <meta http-equiv="Content-Type" content="text/html; charset=utf-8">
        <meta name="viewport" content="initial-scale=6.0, user-scalable=no">
        <script type="text/javascript" src="inc/jquery-2.1.1.min.js"></script>
        <script src="https://maps.googleapis.com/maps/api/js?v=3.exp&language=us"></script>
        
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
var aisupdate = 5;
var amarkers = [];
var gmarker;

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
            aisupdate = 5;
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
        map.setZoom(parseInt(val.zoom));
        gmarker.setMap(null);
        gmarker = new google.maps.Marker({
            position: nmap,
            map: map,
            icon: { 
                    path: google.maps.SymbolPath.FORWARD_OPEN_ARROW,
                    scale: 6,
                    rotation: round_number(val.A,0)   
                }
        });

    } else if (valid == Cmd.GoogleAisFeed) {


        var val = JSON.parse(target);

        for(i=0; i<amarkers.length; i++)
            amarkers[i].setMap(null);

        amarkers = [];
  
        for (var i in val) { 
            var lap = val[i].N == "S"? "-":"";
            var lop = val[i].E == "W"? "-":"";
            var nmap = new google.maps.LatLng(lap+val[i].la, lop+val[i].lo);   
            var amarker = new google.maps.Marker({
                position: nmap,
                title: val[i].name == "unknown"? null:val[i].name,
                map: map,
                icon : {
                        path: google.maps.SymbolPath.CIRCLE,
                        fillColor: "red",
                        fillOpacity: 0.5,
                        scale: 4,
                        strokeColor:"red",
                        strokeWeight: 1,
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
            <img id="instrument" src="img/empty.png" alt="instrument">
            <div id="googlemaps"></div>
        </div>        
        <div id="logpanel"></div>
        <script type="text/javascript" src="inc/wsClient.js.php"></script>  
    </body>
</html>


