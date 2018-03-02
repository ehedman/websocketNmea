<?php
    /*
     * power.php
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
<html>
    <head>
        <title>Power Chart at <?php echo gethostname(); ?></title>
        <meta name="description" content="hedmanshome.se Marine GlassCockpit">
        <meta http-equiv="Content-Type" content="text/html; charset=utf-8">
        <meta name="author" content="Erland Hedman">
        <meta name="license" content="GPL">
        <meta http-equiv="Pragma" content="no-cache">
        <meta http-equiv="Expires" content="-1">
        <link rel="icon" href="img/icon.ico">
        <script type="text/javascript" src="inc/jquery-2.1.1.min.js"></script>
        <script type="text/javascript" src="inc/pako.js"></script>
        <script type="text/javascript" src="inc/Chart.bundle.js"></script>
        <script type="text/javascript" src="inc/utils.js"></script>
    <style>
    canvas{
        -moz-user-select: none;
        -webkit-user-select: none;
        -ms-user-select: none;
        background-color: white;
        padding: 0px;
        border: 0px;
    }
    #logpanel
    {
        position:relative;
        bottom: 70px;
        left: 36%;
        font-size: 0.4em;
    }
    </style>
    </head>

    <body>
        <div style="width:92%;padding-left:3%;">
            <canvas id="canvas"></canvas>
        </div>
        <div style="padding-left:8%">
        <button id="resetPage">Reset Chart</button>
        <button id="pauseData">Pause Data</button>
        <button id="removeData">Remove Data</button>
        <select id="setDuration" title="Chart Duration">
            <option selected="selected">Duration (3)</option>
            <option>0</option>
            <option>3</option>
            <option>5</option>
            <option>10</option>
            <option>20</option>
            <option>30</option>
        </select>
        </div>
        <div id="logpanel"></div>

        <script>

            var pt = 0;
            var ut = 0;
            var target = 0;
            var ticks = 2000;
            var update = 2000;
            var debug = false;
            var connection = true;
            var turn = 0;
            var duration = 3;   // Minutes before removing aged data
            var pause = false;
            var toggle = true;
            var volt = 0;
            var amp = 0;


            function do_addData(val)
            {
                var i = 0;
                var r = 0;
                turn += (update/1000);

                if (turn < 4) return;

                if (config.data.datasets.length > 0) {                
                    var date = new Date(null);
                    date.setSeconds(turn);
                    var tms = date.toISOString().substr(11, 8);

                    if (turn > duration*60) { /// Remove aging data
                        config.data.labels.splice(0, 1); // remove first label
                        config.data.datasets.forEach(function(dataset) {
                            dataset.data.splice(0, 1); // remove first data point
                        });
                        window.myLine.update();
                     }

                    config.data.labels.push(tms);

                    var v = volt;
                    var w = parseFloat(amp*volt).toFixed(1);

                    config.data.datasets.forEach(function(dataset) {
                        if (i++ == 0) r= w; else r=v;
                        dataset.data.push(r);
                    });
                    window.myLine.update();
                }
            }

            function do_update()
            {        
                if (connection) {

                    send(toggle==true? Cmd.SensorVolt : Cmd.SensorCurr);

                    toggle^=true;

                    if (pt == 0)           
                        pt = setInterval(function () {do_poll();}, ticks);
                } else {
                    window.clearInterval(ut);
                    reconnect();
                }
            }

            function do_poll()
            { 
                if (target == "Exp" || connection == false || pause == true) {
                    return;
                }

                if (!(valid == Cmd.SensorVolt || valid == Cmd.SensorCurr)) return;

                var val = JSON.parse(target);

                if (valid == Cmd.SensorVolt)
                    volt = val.volt;
                else
                    amp = val.curr;

                do_addData();
            }

            var randomScalingFactor = function() {
                return Math.round(Math.random() * 100);
            };

            var config = {
                type: 'line',
                data: {
                    labels: [],
                    datasets: [{
                        label: "Watt",
                        backgroundColor: window.chartColors.red,
                        borderColor: window.chartColors.red,
                        data: [],
                        yAxisID: "y-axis-1",
                        fill: false,
                    }, {
                        label: "Volt",
                        
                        backgroundColor: window.chartColors.blue,
                        borderColor: window.chartColors.blue,
                        data: [],
                        yAxisID: "y-axis-2",
                        fill: false
                    }]
                },
                options: {
                    responsive: true,
                    title:{
                        display:true,
                        text:'Power Chart'
                    },
                    tooltips: {
                        mode: 'index',
                        intersect: false,
                    },
                    hover: {
                        mode: 'nearest',
                        intersect: true
                    },
                    scales: {
                        xAxes: [{
                        display: true,
                        scaleLabel: {
                            display: true,
                            labelString: 'Time'
                        }
                    }],
                    yAxes: [{
                        type: "linear", 
                        display: true,
                        position: "right",
                        id: "y-axis-1",
                        ticks: {
                            stepSize: 2
                        },
                    }, {
                        type: "linear",
                        display: true,
                        position: "right",
                        id: "y-axis-2",
                        gridLines: {
                            drawOnChartArea: false, // only want the grid lines for one axis to show up
                        },
                        ticks: {
                            min: 8,
                            max: 16,
                            stepSize: 1
                        },
                    }],
                }
                }
            };

            window.onload = function() {
                var ctx = document.getElementById("canvas").getContext("2d");
                init();
                window.myLine = new Chart(ctx, config);
            };

            document.getElementById('pauseData').addEventListener('click', function() {
                pause = pause == true? false:true;
            });

            document.getElementById('resetPage').addEventListener('click', function() {
                location.reload();
            });

            document.getElementById('setDuration').addEventListener('change', function() {
                var obj=document.getElementById("setDuration");
                var d = obj.options[obj.selectedIndex].text;
                if (isNaN(d) == false) {
                    duration = Number(d); 
		        } else duration = 3; // Default
            });

            document.getElementById('removeData').addEventListener('click', function() {
                config.data.labels.splice(-1, 1); // remove the label first
    
                config.data.datasets.forEach(function(dataset, datasetIndex) {
                    dataset.data.pop();
                });

                window.myLine.update();
            });

        </script>
        <script type="text/javascript" src="inc/common.js.php"></script>
    </body>
</html>
