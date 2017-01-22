# websocketNmea
README Jan-2017

The wsocknmea application package is a marine glass cockpit solution that features electronic marine instrument displays, typically used on private sailing yachts.
The look and feel of the visualized instruments tries to mimic the look of real physical instrumnts and will by design avoid a digital look.

The instruments can be accessed individually as a web page or assembled into a frameset to be viewed on most web browsers, phones and tablets included.

The frameset in this application pacakge that assembles the instrument into one page also features a control panel to set user preferences and the look of the panel.

The communication mechanism between that server and the instrument uses two paralell paths: the standard http protocol to instantiate the instrument and the websocket protocol (port 9000) to show realtime data typically (but not always) shown as arrows that turns on top of a scale.

The wsocknmea daemon is not directly involved in the (nmea) data acquisition but relies on a server daemon dedicated for this purpose that has the capability to do just that and then feed the nmea sentences to the network by means of TCP or UDP data streams.

The wsocknmea daemon uses kplex for that purpose and it will configure and control kplex as a child process of the wsocknmea daemon.

Sucessfully activated the wsocknmea daemon will serve the individual instruments through the network with data assembled into JSON strings to be parsed by each instruemnt.

Checkout a somewhat outdated presentation here: http://www.hedmanshome.se/content/view/17/1/

Currently there are nine virtual instrument working:

    Log         : SOW, SOG
    Wind        : Real, Relative and speed
    Depth       : With low water warning and water temp
    Compass     : With heading
    Goggle Map  : With current satellite view and AIS radar view
    Volt meter  : From ADC
    Clock       : The vessels time
    GPS         : Lo, Lat and Heading
    WaterMaker: : Conductivity, temp, volume (separate project)

Tested runtime environment:
- Odroid XU3 (Ubuntu 15.04)
- Raspberry Pi Model 3 (Debian wheezy)
- Linux Mint 17/18
- Browsers: Firefox, IE, Chrome

For the ADC to work properly I recommend the Pi with the MCP3208 SPI Chip 12 bit ADC 8 channels.
I have a Raymarine E97 Network Multifunction Display that feeds kplex with NMEA-183 through a serial line with wind, speed, water temp, gps etc.

Also included in my set-up is the OpenCPN chart plotter that connects to the kplex traffic on the local WiFi on the yacht.
Obviously the yacht has to be connected to the internet for the Google Map view.


