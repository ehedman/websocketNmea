# websocketNmea
README Jan-2018

The wsocknmea application package is a marine glass cockpit solution that features electronic marine instrument displays, typically used on private sailing yachts.
The look and feel of the visualized instruments tries to mimic the look of real physical instrumnts and will by design avoid a digital look.

The instruments can be accessed individually as a web page or assembled into a frameset to be viewed on most web browsers, phones and tablets included.

The frameset in this application pacakge that assembles the instrument into one page also features a control panel to set user preferences and the look of the panel.

The communication mechanism between that server and the instrument uses two paralell paths: the standard http protocol to instantiate the instrument and the websocket protocol (port 443) to show realtime data typically (but not always) shown as arrows that turns on top of a scale.

The wsocknmea daemon is not directly involved in the (nmea) data acquisition but relies on a server daemon dedicated for this purpose that has the capability to do just that and then feed the nmea sentences to the network by means of TCP or UDP data streams.

The wsocknmea daemon uses kplex for that purpose and it will configure and control kplex as a child process of the wsocknmea daemon.

Sucessfully activated the wsocknmea daemon will serve the individual instruments through the network with data assembled into JSON strings to be parsed by each instruemnt.

Currently there are eleven virtual instrument working:

    Log           : SOW, SOG
    Wind          : Real, Relative and speed
    Depth         : With low water warning and water temp
    Compass       : With heading
    Goggle Map    : With current satellite view and AIS radar view
    Clock         : The vessels time
    GPS           : Lo, Lat and Heading
    WaterMaker:   : Conductivity, temp, volume (separate project)
    Volt meter    : From ADC
    Current meter : From ADC
    Temp meter    : From ADC (Directly in C/F from UK1104)
  I/O Control:
    Relay ON/OFF  : From ADC (UK1104 only)

Tested runtime environment:
- Odroid XU3/4 (Ubuntu 15.04/16.04)
- X86 (Ubuntu 16)
- Raspberry Pi Model 3 (Debian wheezy)
- Linux Mint 17/18
- Browsers: Firefox, IE, Chrome (Chrome & Firefox also on Android)

For the ADC to work properly I recommend the Pi with the MCP3208 SPI Chip 12 bit ADC 8 channels.
For a more comprehensive I/O Solution use the UK1104 driver instead. See https://www.canakit.com
I have a Raymarine E97 Network Multifunction Display that feeds kplex with NMEA-183 through a serial line with wind, speed, water temp, gps etc.

Also included in my set-up is the OpenCPN chart plotter that connects to the kplex traffic on the local WiFi on the yacht.
Obviously the yacht has to be connected to the internet for the Google Map view.
To access your vessel as an IOT (to remotely acess all instruemnts and remote relay control) I recommend to manipulate the ip-table rules with "shorewall" (http://shorewall.org/) in order to establish your device as an firewall with virtual host rules to open ports 80 and 443 to the internet.
Check my firewall GUI interface at https://github.com/ehedman/headwall.

### Screenshots

Running in Firefox on a Galaxy Note 10.1:

<img src="http://hedmanshome.se/screenshots/2018-01-21-04-07-00.png" width=400></br>
<img src="http://hedmanshome.se/screenshots/2018-01-21-04-07-29.png" width=400></br>
<img src="http://hedmanshome.se/screenshots/2018-01-21-04-07-52.png" width=400></br>
<img src="http://hedmanshome.se/screenshots/2018-01-21-04-07-31.png" width=400>

