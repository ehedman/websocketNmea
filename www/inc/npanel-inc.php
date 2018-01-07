<?php
    /*
     * npanel.php
     *
     *  Copyright (C) 2013-2014 by Erland Hedman <erland@hedmanshome.se>
     *
     * This program is free software; you can redistribute it and/or
     * modify it under the terms of the GNU General Public License
     * as published by the Free Software Foundation; either version
     * 2 of the License, or (at your option) any later version.
     */
     
    putenv('PATH='.getenv('PATH').':'.DOCROOT.'/inc:/usr/local/bin'); 
    
    if (count($_GET)) $NIGHT = $_GET['Night']=='y'? 1:0; else  $NIGHT = 0;
    
    $PMESSAGE="";
    $DBH = NULL;
    $KEY = NULL;

   //if (count($_POST)) {echo "<pre>"; print_r($_POST); echo "</pre>";}

    if (file_exists (NAVIDBPATH)) {
        if (count($_POST) && ! is_writable(NAVIDBPATH)) {
            echo "<pre>The configuration database ".NAVIDBPATH." is not writable!. Giving up save.</pre>";
            exit;
        }
    } else {
        echo "<pre>The configuration database ".NAVIDBPATH." does not exist. Use wsocknmea to create one.</pre>";
        exit;
    }

  try {
    $DBH = new PDO('sqlite:'.NAVIDBPATH);
    $DBH->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    $sql = "SELECT * from netif WHERE Id=1"; // Force early catch if corrupt
    $DBH->exec($sql);

    $STTYS=array();
    $SNETIFS=array();

    if (count($_POST) && $_POST['POST_ACTION'] == "OK" && $DBH) {

        $sql = "UPDATE `gmap` SET `zoom`='".$_POST['map_zoom']."', `updt`='".$_POST['map_updt']."' WHERE `id`=1";
        $DBH->exec($sql);

        if (strlen($_POST['gkey'])) {
            $key = $_POST['gkey'] == "invalid"? "": $_POST['gkey'];
            $sql = "UPDATE `gmap` SET `key`='".$key."' WHERE `id`=1";
            $DBH->exec($sql);
        }
        
        $sql = "UPDATE `depth` SET `vwrn`='".$_POST['depth_vwrn']."', `tdb`='".$_POST['depth_transp']."' WHERE `id`=1";
        $DBH->exec($sql);

        $sql = "UPDATE `sumlog` SET `display`='".$_POST['smlog_disp']."', `cal`='".$_POST['smlog_calb']."' WHERE `id`=1";
        $DBH->exec($sql);

        $sql="UPDATE `file` SET `fname`='nofile', `rate`='1', `use`='off' WHERE `Id`=1";
        $DBH->exec($sql);

        $sql="UPDATE `devadc` SET `device`='".$_POST['a2dserial']."',`relay1txt`='".$_POST['relay1txt']."',`relay2txt`='".$_POST['relay2txt']."',`relay3txt`='".$_POST['relay3txt']."',`relay4txt`='".$_POST['relay4txt']."' WHERE `Id`=1";
        $DBH->exec($sql);

        $sql="UPDATE `ais` SET `aisname`='".strtoupper($_POST['aisname'])."', `aisid`='".$_POST['aisid']."', `aisuse`='".$_POST['aisuse']."' WHERE `Id`=1";
        $DBH->exec($sql);

        if (($n=intval($_POST['nttys'])) > 0) {
            for ($i=1; $i <$n+1; $i++) {
                if (isset($_POST['tty'][$i]['use'])) {
                    $sql="UPDATE `ttys` SET `name`='".$_POST['tty'][$i]['name']."', ";
                    $sql.="`baud`='".$_POST['tty'][$i]['baud']."', ";
                    $sql.="`dir`='".$_POST['tty'][$i]['dir']."', ";
                    $sql.="`use`='".$_POST['tty'][$i]['use']."' ";
                    $sql.="WHERE `Id`=$i";
                } else {
                    $sql="UPDATE `ttys` SET `use`='off' WHERE `Id`=$i";
                }
                $DBH->exec($sql);
            }
        }
 
        if (($n=intval($_POST['nnetifs'])) > 0) {
            $ipa=array();
            $bca=array();
            for ($i=1; $i <$n+1; $i++) {
                exec("naviSystem.sh get_ipaddr ".$_POST['nifs'][$i]['device'], $ipa);
                exec("naviSystem.sh get_broadcast_addr ".$_POST['nifs'][$i]['device'], $bca);

                $sql="UPDATE `netif` SET `device`='".$_POST['nifs'][$i]['device']."', ";
                if ($_POST['nifs'][$i]['device'] == "lo") {
                    if ($_POST['nifs'][$i]['proto'] == "tcp" || $_POST['nifs'][$i]['type'] == "broadcast")
                        $sql.="`addr`='127.0.0.1', ";
                    else 
                        $sql.="`addr`='".trim($_POST['nifs'][$i]['addr'])."', ";                 
                   
                } else {
                    if ($_POST['nifs'][$i]['proto'] == "tcp") {
                        $sql.="`addr`='".trim($ipa[$i-1])."', ";
                    } else if ($_POST['nifs'][$i]['proto'] == "udp" && $_POST['nifs'][$i]['type'] == "broadcast") {
                        $sql.="`addr`='".trim($bca[$i-1])."', ";
                    } else if ($_POST['nifs'][$i]['proto'] == "udp") {
                        $sql.="`addr`='".trim($_POST['nifs'][$i]['addr'])."', ";
                    }
                }
               
                if ($_POST['nifs'][$i]['device'] == "lo" && $_POST['nifs'][$i]['type'] == "broadcast") 
                    $sql.="`proto`='tcp', ";
                else
                    $sql.="`proto`='".$_POST['nifs'][$i]['proto']."', ";   
                 
                $sql.="`type`='".$_POST['nifs'][$i]['type']."', ";                
                $sql.="`port`='".trim($_POST['nifs'][$i]['port'])."', "; 
                
                $sql.="`use`='".$_POST['nifs'][$i]['use']."' ";
                $sql.="WHERE `Id`=$i"; //echo $sql;
                $DBH->exec($sql); 
            }   
        }
    }
    
    if ($DBH) {    
        $stmt = $DBH->prepare("SELECT zoom, updt, key FROM gmap LIMIT 1"); 
        $stmt->execute(); 
        $row = $stmt->fetch();
        $map_zoom=$row['zoom'];
        $map_updt=$row['updt'];
        $KEY = $row['key'];
        
        $stmt = $DBH->prepare("SELECT vwrn, tdb FROM depth LIMIT 1"); 
        $stmt->execute(); 
        $row = $stmt->fetch();
        $depth_vwrn=$row['vwrn'];
        $depth_transp=$row['tdb'];

        $stmt = $DBH->prepare("SELECT display, cal FROM sumlog LIMIT 1"); 
        $stmt->execute(); 
        $row = $stmt->fetch();
        $smlog_disp=$row['display'];
        $smlog_calb=$row['cal'];

        $stmt = $DBH->prepare("SELECT aisname, aisid, aisuse FROM ais LIMIT 1");
        $stmt->execute(); 
        $row = $stmt->fetch();
        $aisname=$row['aisname'];
        $aisid=$row['aisid'];
        $aisuse=$row['aisuse'];

        $stmt = $DBH->prepare("SELECT device,relay1txt,relay2txt,relay3txt,relay4txt FROM devadc LIMIT 1");
        $stmt->execute(); 
        $row = $stmt->fetch();
        $a2dserial=$row['device'];
        $a2dreltxt1=$row['relay1txt'];
        $a2dreltxt2=$row['relay2txt'];
        $a2dreltxt3=$row['relay3txt'];
        $a2dreltxt4=$row['relay4txt'];

        $stmt = $DBH->prepare('SELECT name, baud, dir, use FROM ttys ORDER BY Id');
        $stmt->execute(); 
        
        $itm=1;  
        while ($row = $stmt->fetch()) {
            $STTYS[$itm]['name'] = $row['name'];
            $STTYS[$itm]['baud'] = $row['baud'];
            $STTYS[$itm]['dir'] = $row['dir'];
            $STTYS[$itm]['use'] = $row['use'];
            $itm++;
        } 

        $stmt = $DBH->prepare('SELECT device, port, addr, type, proto, use FROM netif ORDER BY Id');
        $stmt->execute(); 
        
        $itm=1;
        $ipa=array();
        $bca=array();
        while ($row = $stmt->fetch()) {
            exec("naviSystem.sh get_ipaddr ". $row['device'], $ipa);
            exec("naviSystem.sh get_broadcast_addr ". $row['device'], $bca);
            $SNETIFS[$itm]['device'] = $row['device'];
            $SNETIFS[$itm]['port'] = $row['port'];
            $SNETIFS[$itm]['addr'] = $row['addr'];
            $SNETIFS[$itm]['type'] = $row['type'];
            $SNETIFS[$itm]['proto'] = $row['proto'];
            $SNETIFS[$itm]['use'] = $row['use'];
            $itm++;
        }
           
        $stmt->closeCursor();
    }
    
    if (count($_FILES) && count($_POST) && $_POST['POST_ACTION'] == "FOK") {
        if((!empty($_FILES["uploaded_file"])) && ($_FILES['uploaded_file']['error'] == 0)) {
            //Check if the file is .txt image and it's size is less than 10M
            $filename = basename($_FILES['uploaded_file']['name']);
            $ext = substr($filename, strrpos($filename, '.') + 1);
            if (($ext == "txt") && ($_FILES["uploaded_file"]["type"] == "text/plain") && 
                    ($_FILES["uploaded_file"]["size"] < MAXFILESIZE)) {
                //Determine the path to which we want to save this file
                $newname = DOCROOT.'/upload/'.$filename;
                //Check if the file with the same name is already exists on the server
                if (file_exists($newname)) { unlink($newname); }
                    //Attempt to move the uploaded file to it's new place
                if ((move_uploaded_file($_FILES['uploaded_file']['tmp_name'],$newname))) {
                    $PMESSAGE="The file has been saved as: ".$newname;
                    @exec("unix2dos ".$newname);
                    kplex_config("file-action $newname");
                } else {
                    $PMESSAGE="Error: A problem occurred during file upload!";
                }
            } else {
                $PMESSAGE="Error: Only .txt files under 10M are accepted for upload";
            }
        } else {
            if ($_POST['nfList'] != "Select" && file_exists(DOCROOT.'/upload/'. $_POST['nfList']) != false) {
                if ($DBH) {
                    $file = DOCROOT.'/upload/'. $_POST['nfList'];
                    $PMESSAGE="Using file ".$file;
                    $sql="UPDATE `file` SET `fname`='".$file."', `rate`='".$_POST['nmea_rate']."', `use`='on' WHERE `Id`=1";
                    $DBH->exec($sql); 
                    $stmt->closeCursor();
                    kplex_config("file-action ". $file);
                }
            } else {
                $PMESSAGE="Error: No file selected";
            }
        }
    } else if (count($_POST) && $_POST['POST_ACTION'] == "OK" && count($STTYS) && $DBH) {
        kplex_config("config-action");
    }

  } catch(PDOException $e) {
        echo "<pre>The configuration database ".NAVIDBPATH." appears corrupt: ";
        echo $e->getMessage();
        echo  ". Delete it and use wsocknmea to create new one.</pre>";
        exit;
  }


function kplex_config($args)
{

    global $SNETIFS;
    global $STTYS;
      
    if (!($fd=fopen(KPCONFPATH, "w")))
        return;
        
    $a = preg_split("/[\s,]+/", $args);
    
    switch (trim($a[0]))
    {
        case 'file-action':
            fputs($fd,"[file]\nfilename=".FIFOKPLEX."\ndirection=in\neol=rn\npersist=yes\n\n"); 
            //$cmdline = "-b -f ".$a[1]." -r ".$_POST['nmea_rate'];
            $cmdline = "-b";
        break;
        case 'config-action':
            $n=count($STTYS);
            $str="";
            for ($i=1; $i<$n+1; $i++) {
                if ($STTYS[$i]['use'] != 'on')
                    continue;
                $str="[serial]\n";
                $str.="direction=".$STTYS[$i]['dir']."\n";
                $str.="filename=".$STTYS[$i]['name']."\n";
                $str.="baud=".$STTYS[$i]['baud']."\n\n";
                fputs($fd, $str);   
            }
            $cmdline = "-b";
        break;
        default:
        break;   
    }

    // Add network interfaces (UDP parts incomplete)
    for ($i=1; $i<count($SNETIFS)+1; $i++) {
        if ($SNETIFS[$i]['use'] != 'on')
            continue;
        fputs($fd, "[".$SNETIFS[$i]['proto']."]\n");
        if ($SNETIFS[$i]['proto'] == "tcp")
            fputs($fd, "mode=server\n");
        if (!($SNETIFS[$i]['type'] == "broadcast" && $SNETIFS[$i]['proto'] == "udp" && $SNETIFS[$i]['device'] == "lo")) {
            fputs($fd, "address=".$SNETIFS[$i]['addr']."\n");
        }
        fputs($fd, "port=".$SNETIFS[$i]['port']."\n");
        if ($SNETIFS[$i]['proto'] == "udp") {
            fputs($fd, "device=".$SNETIFS[$i]['device']."\n");
            fputs($fd, "type=".$SNETIFS[$i]['type']."\n");
        }
    }
    
    fclose($fd);

    exec("naviSystem.sh restart_wsserver ". $cmdline);
}
    
function print_nmea_recordings()
{
    $rfs=array();
    
    exec ("naviSystem.sh get_nrecordings", $rfs);

    echo '<select id="nfList" name="nfList" title="Select existing recordings">';
    echo "<option>Select</option>";
    
    foreach ($rfs as $rf)
        echo "<option>$rf</option>";
        
    echo "</select><br>";
}
    
function print_netInterfaces()
{

    $ifs=array();
    global $SNETIFS;
    
    exec ("naviSystem.sh get_netifs", $ifs);
    
    echo '<select id="netifList" onchange="showNIF();" title="Show properties for:"><option>Select</option>';
    
    foreach ($ifs as $if)
       echo "<option>$if</option>";
       
    echo "</select><br><br>";
    
    $dev=1;
    foreach ($ifs as $if) {
        ?>
        
                    <div style="display:none" id="nifs-<?php echo $dev; ?>">
                    <label title="Recommended:10110" >Port:&nbsp;</label>
                    <input type="text" title="Recommended:10110" id="nwport-<?php echo $dev; ?>" name="<?php echo "nifs[$dev][port]"; ?>" value="<?php echo $SNETIFS[$dev]['port']; ?>"><br>
                    <label title="TCP adress will be set by system" >I.P:&nbsp;&nbsp;&nbsp;</label>
                    <input type="text" title="TCP adress will be set by system"<?php echo $SNETIFS[$dev]['proto']=="tcp"? " readonly ":" "; ?>id="nwaddr-<?php echo $dev; ?>" name="<?php echo "nifs[$dev][addr]"; ?>" value="<?php echo $SNETIFS[$dev]['addr']; ?>"><br>
                    <label title="Protocol: tcp/udp" >Protocol:&nbsp;</label>
                    TCP<input type="radio" onclick="nifstypeset(<?php echo $dev; ?>,0)" id="nwprottcp-<?php echo $dev; ?>" name="<?php echo "nifs[$dev][proto]"; ?>" value="tcp"<?php echo $SNETIFS[$dev]['proto']=="tcp"? " checked":""; ?>>
                    UDP<input type="radio" onclick="nifstypeset(<?php echo $dev; ?>,1)" id="nwprotudp-<?php echo $dev; ?>" name="<?php echo "nifs[$dev][proto]"; ?>" value="udp"<?php echo $SNETIFS[$dev]['proto']=="udp"? " checked":""; ?>><br>
                    <div style="display:<?php echo $SNETIFS[$dev]['proto']=="udp"? "visible":"none"; ?>" id="nifstype-<?php echo $dev; ?>">
                    <label title="U/B/Mcast valid only for UDP" >Cast:</label>
                    UNI<input type="radio" id="nwtypeuni-<?php echo $dev; ?>" name="<?php echo "nifs[$dev][type]"; ?>" value="unicast"<?php echo $SNETIFS[$dev]['type']=="unicast"? " checked":""; ?>>
                    BRD<input type="radio" id="nwtypebrd-<?php echo $dev; ?>" name="<?php echo "nifs[$dev][type]"; ?>" value="broadcast"<?php echo $SNETIFS[$dev]['type']=="broadcast"? " checked":""; ?>>
                    MLT<input type="radio" id="nwtypemlt-<?php echo $dev; ?>" name="<?php echo "nifs[$dev][type]"; ?>" value="multicast"<?php echo $SNETIFS[$dev]['type']=="multicast"? " checked":""; ?>><br>
                    </div>
                    <input type="hidden" value="<?php echo $if; ?>" name="<?php echo "nifs[$dev][device]"; ?>">
                    <label title="Use this device">Use:</label>
                    <input type="radio" onclick="set_used_nwdev(<?php echo $dev; ?>)" id="nwdev-<?php echo $dev; ?>" name="dev-nifs-use"<?php echo $SNETIFS[$dev]['use']=="on"? " checked":""; ?>>
                    <input type="hidden" id="nwdev-use-<?php echo $dev; ?>" value="<?php echo $SNETIFS[$dev]['use']=="on"? "on":"off" ?>" name="<?php echo "nifs[$dev][use]"; ?>">
                    </div>
        <?php

        $dev++;
    }
    ?>
    
                    <script type="text/javascript">
                    function showNIF()
                    {
                        var items=<?php echo $dev; ?>;
                        for (i=1; i<items; i++) {
                                document.getElementById("nifs-"+i).style.display = "none";
                        }
                        var s = document.getElementById("netifList").selectedIndex;
                         if (s<1) return;
                        document.getElementById("nifs-"+s).style.display = "block"; 
                    }
                    document.getElementById("nnetifs").value=<?php echo $dev; ?>-1;
                    </script>
    <?php
}

function print_serInterfaces()
{
    $ttys=array();
    global $STTYS;
    
    exec ("naviSystem.sh get_ttys", $ttys);
    
    echo ' <select id="ttyList" onchange="showTTY();" title="Show properties for:"><option>Select</option>';

    foreach ($ttys as $tty)
        echo "<option>$tty</option>";

    echo "</select><br>";
    $dev=1;
    foreach ($ttys as $tty) {
        ?>
        
                    <div style="display:none" id="dtty-<?php echo $dev; ?>">
                        <select title="Baudrate" name="<?php echo "tty[$dev][baud]"; ?>">
                            <option <?php echo $STTYS[$dev]['baud']=='4800'?   'selected ':' ' ?>value="4800">4800</option>
                            <option <?php echo $STTYS[$dev]['baud']=='9600'?   'selected ':' ' ?>value="9600">9600</option>
                            <option <?php echo $STTYS[$dev]['baud']=='19200'?  'selected ':' ' ?>value="19200">19200</option>
                            <option <?php echo $STTYS[$dev]['baud']=='38400'?  'selected ':' ' ?>value="38400">38400</option>
                            <option <?php echo $STTYS[$dev]['baud']=='57600'?  'selected ':' ' ?>value="57600">57600</option>
                            <option <?php echo $STTYS[$dev]['baud']=='115200'? 'selected ':' ' ?>value="115200">115200</option>
                        </select>
                        <select title="Direction" name="<?php echo "tty[$dev][dir]"; ?>">
                            <option <?php echo $STTYS[$dev]['dir']=='In'?  'selected ':' ' ?>value="In">In</option>
                            <option <?php echo $STTYS[$dev]['dir']=='Out'? 'selected ':' ' ?>value="Out">Out</option>
                            <option <?php echo $STTYS[$dev]['dir']=='Both'? 'selected ':' ' ?>value="Both">Both</option>
                        </select>
                        <label title="Use this device">Use:</label>
                        <input <?php echo $STTYS[$dev]['use']=='on'? 'checked ':' ' ?>type="checkbox" name="<?php echo "tty[$dev][use]"; ?>">
                        <input type="hidden" value="<?php echo $tty; ?>" name="<?php echo "tty[$dev][name]"; ?>">
                    </div>
        <?php

        $dev++;
    }
    ?>
    
                    <script type="text/javascript">
                    function showTTY()
                    {
                        var items=<?php echo $dev; ?>;
                        for (i=1; i<items; i++) {
                                document.getElementById("dtty-"+i).style.display = "none";
                        }
                        var s = document.getElementById("ttyList").selectedIndex;
                        if (s<1) return;
                        document.getElementById("dtty-"+s).style.display = "block"; 
                    }
                    document.getElementById("nttys").value=<?php echo $dev; ?>-1;
                    </script>
    <?php
}



?>
