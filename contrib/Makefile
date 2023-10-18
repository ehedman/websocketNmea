OSREL = $(shell awk -F\" '/VERSION=/ {printf("%.2s",$$2)}' /etc/os-release)

all: kplex aisparser sendemail hs100 openssl libwebsockets

kplex:
	git clone https://github.com/stripydog/kplex.git
	cd kplex && make CFLAGS="-O2 -Wall"

aisparser:
	git clone https://github.com/bcl/aisparser.git
	cd aisparser/c/so && make linux

sendemail:
	git clone  https://github.com/mogaal/sendemail.git

hs100:
	git clone https://github.com/jkbenaim/hs100.git
	cd hs100 && make

openssl:
ifeq ($(shell expr $(OSREL) \> 11), 1)
	git clone -b OpenSSL_1_1_1w https://github.com/openssl/openssl.git
	cd openssl && ./config; make -j; sudo make install_sw
endif

libwebsockets:
	git clone -b v3.1-stable https://github.com/warmcat/libwebsockets.git
	mkdir libwebsockets/build
	cd libwebsockets/build && cmake ..; make -j; sudo make -j install/local/fast; sudo install lib/libwebsockets.so.14 -m 0755 -g root -o root -D /usr/local/lib
	sudo rm -f /usr/local/lib/libwebsockets.so
	sudo ln -s /usr/local/lib/libwebsockets.so.14 /usr/local/lib/libwebsockets.so

install: kplex aisparser
	cd kplex && sudo make BINDIR=/usr/local/bin install
	sudo install sendemail/sendEmail -m 755 -o root -g root /usr/local/bin
	sudo install hs100/hs100 -m 755 -o root -g root /usr/local/bin
	sudo install aisparser/c/so/libais.so.1.* -m 0755 -g root -o root -D /usr/local/lib
	sudo ln -fs /usr/local/lib/libais.so.1.* /usr/local/lib/libais.so.1
	sudo ln -fs /usr/local/lib/libais.so.1.* /usr/local/lib/libais.so
	sudo install aisparser/c/src/portable.h -m 0664 -o root -g root /usr/local/include
	sudo install aisparser/c/src/nmea.h -m 0664 -o root -g root /usr/local/include
	sudo install aisparser/c/src/sixbit.h -m 0664 -o root -g root /usr/local/include
	sudo install aisparser/c/src/vdm_parse.h -m 0664 -o root -g root /usr/local/include

install-configs:
	@if [ ! -f .configured ]; then \
		sudo systemctl stop lighttpd; \
		sudo systemctl disable lighttpd; \
		sudo lighttpd-enable-mod fastcgi || true; \
		sudo lighttpd-enable-mod fastcgi-php || true; \
		sudo systemctl enable lighttpd; \
		sudo systemctl start lighttpd; \
		touch .configured; \
	else \
		echo "Configuration already done"; \
	fi

clean:
	rm -rf kplex aisparser sendemail hs100 libwebsockets openssl .configured

