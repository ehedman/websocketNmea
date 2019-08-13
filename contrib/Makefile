all: kplex lighttpd1.4-lighttpd-1.4.54 libwebsockets aisparser sendemail

kplex:
	git clone https://github.com/stripydog/kplex.git
	cd kplex && make CFLAGS="-O2 -Wall"

aisparser:
	git clone https://github.com/bcl/aisparser.git
	cd aisparser/c/so && make linux

sendemail:
	git clone  https://github.com/mogaal/sendemail.git

libwebsockets:
	git clone -b v3.1-stable https://github.com/warmcat/libwebsockets.git
	mkdir libwebsockets/build
	cd libwebsockets/build && cmake ..;make

lighttpd1.4-lighttpd-1.4.54:
	wget https://github.com/lighttpd/lighttpd1.4/archive/lighttpd-1.4.54.tar.gz
	tar xof lighttpd-1.4.54.tar.gz
	cd lighttpd1.4-lighttpd-1.4.54 && ./autogen.sh; ./configure; make
	
install: lighttpd1.4-lighttpd-1.4.54 kplex libwebsockets aisparser
	@if [ -x /usr/sbin/lighttpd ]; then \
		echo "INSTALLED LIGHTTPD ABOUT TO BE REPLACED"; \
		sudo apt-get remove lighttpd; \
		if [ ! -x /usr/sbin/lighttpd ]; then \
			sudo systemctl unmask lighttpd.service  > /dev/null 2>&1; \
		fi \
	fi
	cd kplex && sudo make BINDIR=/usr/local/bin install
	cd lighttpd1.4-lighttpd-1.4.54 && sudo make install
	sudo install sendemail/sendEmail -m 755 -o root -g root /usr/local/bin
	sudo mkdir -p  /usr/local/include/libwebsockets
	sudo install libwebsockets/build/include/libwebsockets.h -m 0664 -o root -g root /usr/local/include
	sudo install libwebsockets/build/lws_config.h -m 0664 -o root -g root /usr/local/include
	sudo install libwebsockets/build/lib/libwebsockets.a -m 0644 -g root -o root -D /usr/local/lib
	sudo rm -f /usr/local/lib/libwebsockets.so.*
	sudo install libwebsockets/build/lib/libwebsockets.so.* -m 0755 -g root -o root -D /usr/local/lib
	sudo install libwebsockets/build/include/libwebsockets/*.h -m 0664 -o root -g root /usr/local/include/libwebsockets
	cd  /usr/local/lib && sudo ln -fs libwebsockets.so.* libwebsockets.so; cd -
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
	rm -rf kplex lighttpd1.4-lighttpd-1.4.54 libwebsockets aisparser sendemail
	rm -f lighttpd-1.4.54.tar.gz

