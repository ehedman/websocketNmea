all: kplex aisparser sendemail hs100 libwebsockets

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

$(eval value=$(shell apt list -a libwebsockets-dev 2>/dev/null |awk  '{ printf "%s", $$(2);  }' | cut -d. -f1))

libwebsockets:
ifeq ($(shell expr $(value) \>= 3), 1)
	@echo "Using native libwebsockets"
else
	@echo "Building libwebsockets from source:"
	git clone -b v4.2.0 https://github.com/warmcat/libwebsockets.git
	mkdir libwebsockets/build
	cd libwebsockets/build && cmake ..; make -j; sudo make -j install/local/fast; sudo install lib/libwebsockets.so.18 -m 0755 -g root -o root -D /usr/local/lib
	sudo rm -f /usr/local/lib/libwebsockets.so
	sudo ln -s /usr/local/lib/libwebsockets.so.18 /usr/local/lib/libwebsockets.so
endif


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
	rm -rf kplex aisparser sendemail hs100 libwebsockets .configured

