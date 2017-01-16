all: kplex-1.3.4 lighttpd1.4-lighttpd-1.4.33 libwebsockets

kplex-1.3.4:
	wget http://www.stripydog.com/download/kplex-1.3.4.tgz
	tar xof kplex-1.3.4.tgz
	cd kplex-1.3.4; make; cd ..

libwebsockets:
	git clone -b v2.1-stable https://github.com/warmcat/libwebsockets.git
	cd libwebsockets; mkdir build; cd build; cmake ..; make; cd ..

lighttpd1.4-lighttpd-1.4.33: mod_websocket
	wget https://github.com/lighttpd/lighttpd1.4/archive/lighttpd-1.4.33.tar.gz
	tar xof lighttpd-1.4.33.tar.gz
	cd mod_websocket;  ./configure --with-lighttpd=../lighttpd1.4-lighttpd-1.4.33; make install; cd ..
	cd lighttpd1.4-lighttpd-1.4.33; ./autogen.sh;  ./configure --with-websocket=all; make; cd ..
	
mod_websocket:
	git clone https://github.com/nori0428/mod_websocket.git
	cd mod_websocket; ./bootstrap; 

install: lighttpd1.4-lighttpd-1.4.33 kplex-1.3.4 libwebsockets
	@if [ -x /usr/sbin/lighttpd ]; then \
		echo "INSTALLED LIGHTTPD ABOUT TO BE REPLACED"; \
		sudo apt-get remove lighttpd; \
		if [ ! -x /usr/sbin/lighttpd ]; then \
			sudo systemctl unmask lighttpd.service  > /dev/null 2>&1; \
		fi \
	fi
	cd lighttpd1.4-lighttpd-1.4.33; sudo make install; cd ..
	cd kplex-1.3.4; sudo make BINDIR=/usr/local/bin install; cd ..
	cd libwebsockets/build; sudo make install; cd ..

install-configs:
	cd configs; make TOP=$(TOP) WO=$(WO) WG=$(WG) install-configs
clean:
	rm -rf kplex-1.3.4 lighttpd1.4-lighttpd-1.4.33 mod_websocket libwebsockets
	rm -f kplex-1.3.4.tgz lighttpd-1.4.33.tar.gz

