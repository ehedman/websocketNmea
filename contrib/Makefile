all: kplex lighttpd1.4-lighttpd-1.4.33 libwebsockets aisparser sendemail

kplex:
	git clone https://github.com/stripydog/kplex.git
	cd kplex && make CFLAGS="-O2 -Wall"

aisparser:
	git clone https://github.com/bcl/aisparser.git
	cd aisparser/c/so && make linux

sendemail:
	git clone  https://github.com/mogaal/sendemail.git

libwebsockets:
	git clone -b v2.1-stable https://github.com/warmcat/libwebsockets.git
	mkdir libwebsockets/build
	cd libwebsockets/build && cmake ..;make

lighttpd1.4-lighttpd-1.4.33: mod_websocket
	wget https://github.com/lighttpd/lighttpd1.4/archive/lighttpd-1.4.33.tar.gz
	tar xof lighttpd-1.4.33.tar.gz
	cd mod_websocket;  ./configure --with-lighttpd=../lighttpd1.4-lighttpd-1.4.33; make install; cd ..
	cd lighttpd1.4-lighttpd-1.4.33 && autoreconf -fis;  ./configure --with-websocket=all; make;
	
mod_websocket:
	git clone https://github.com/nori0428/mod_websocket.git
	patch  -p0  < ./configs/mod_websocket.patch
	cd mod_websocket && ./bootstrap; 

install: lighttpd1.4-lighttpd-1.4.33 kplex libwebsockets aisparser
	@if [ -x /usr/sbin/lighttpd ]; then \
		echo "INSTALLED LIGHTTPD ABOUT TO BE REPLACED"; \
		sudo apt-get remove lighttpd; \
		if [ ! -x /usr/sbin/lighttpd ]; then \
			sudo systemctl unmask lighttpd.service  > /dev/null 2>&1; \
		fi \
	fi
	cd kplex && sudo make BINDIR=/usr/local/bin install
	cd lighttpd1.4-lighttpd-1.4.33 && sudo make install
	sudo install sendemail/sendEmail -m 755 -o root -g root /usr/local/bin
	sudo install libwebsockets/lib/libwebsockets.h -m 0664 -o root -g root /usr/local/include
	sudo install libwebsockets/build/lws_config.h -m 0664 -o root -g root /usr/local/include
	sudo install libwebsockets/build/lib/libwebsockets.a -m 0644 -g root -o root -D /usr/local/lib
	sudo rm -f /usr/local/lib/libwebsockets.so.*
	sudo install libwebsockets/build/lib/libwebsockets.so.* -m 0755 -g root -o root -D /usr/local/lib
	cd  /usr/local/lib && sudo ln -fs libwebsockets.so.* libwebsockets.so
	sudo install aisparser/c/so/libais.so.1.* -m 0755 -g root -o root -D /usr/local/lib
	cd /usr/local/lib && sudo ln -fs libais.so.1.* libais.so.1
	cd /usr/local/lib && sudo ln -fs libais.so.1 libais.so
	sudo install aisparser/c/src/portable.h -m 0664 -o root -g root /usr/local/include
	sudo install aisparser/c/src/nmea.h -m 0664 -o root -g root /usr/local/include
	sudo install aisparser/c/src/sixbit.h -m 0664 -o root -g root /usr/local/include
	sudo install aisparser/c/src/vdm_parse.h -m 0664 -o root -g root /usr/local/include

install-configs:
	cd configs &&  make TOP=$(TOP) WO=$(WO) WG=$(WG) install-configs
clean:
	rm -rf kplex lighttpd1.4-lighttpd-1.4.33 mod_websocket libwebsockets aisparser sendemail
	rm -f lighttpd-1.4.33.tar.gz

