<?php

    if ( isset($_GET['Exit']) && $_GET['Exit'] == "y") {
        shell_exec('killall  -TERM chromium');  
        exit;   // Only possible if (this) server and browser are on the same host by value of DOEXIT
    }

  
?>
<!DOCTYPE html>
<html lang="en">

<style>

body {
    margin: 0;
        background-color: 0;
}

/* Fullscreen iframe */
iframe {
    position: fixed;
    top: 0;
    left: 0;
    width: 100vw;
    height: 100vh;
    border: 0;
    z-index: 1;
}

#ExitButton {
    position: fixed;      /* Float above iframe */
    bottom: 16px;        /* Distance from bottom */
    right: 10px;          /* Distance from right */
    height: 20px;
    width: 30px;
    font-size: 12px;
    z-index: 9999;        /* Ensure it stays above iframe */
    background: rgba(0,0,0,0.6);
    color: white;
    border: none;
//    border-radius: 6px;
    cursor: pointer;
}

#ExitButton:hover {
    background: rgba(0,0,0,0.85);
}
</style>

<script>
function do_exit() {
    window.location.href = "<?php echo $_SERVER['SCRIPT_NAME']; ?>?Exit=y";
}
</script>

<body>

 <input title="Exit" id="ExitButton" type="button" value="Exit" onclick="do_exit();">

 <iframe src="http://<?php echo $_SERVER['SERVER_ADDR']; ?>/gui-v2/"  title="gui-v2"></iframe> 

</body>
</html>

