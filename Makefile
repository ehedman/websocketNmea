
SHELL=/bin/sh

MAKE = make

SUBDIRS = www

# Package dependencies
PKGS += g++
PKGS += git
PKGS += cmake
PKGS += unzip
PKGS += automake
PKGS += autoconf
PKGS += libtool
PKGS += pkg-config
PKGS += libpcre3 libpcre3-dev
PKGS += libbz2-dev
PKGS += libz-dev
PKGS += libev-dev
ifeq "$(shell apt-cache search php7| grep cgi)" ""
PKGS += php5-cgi php5-sqlite*
else
PKGS += php7.*-cgi php7.*-sqlite3
endif
PKGS += libsqlite3-dev sqlite3
PKGS += libssl-dev

# Source files to be compiled
SRCS = wsocknmea.c adc-sensors.c ais.c
HDRS = wsocknmea.h
BIN = wsocknmea

# Where to install web pages
WWWTOP?=/var/www

# The web-server's runtime user and group belongings
WO = www-data
WG = $(WO)

UID=$(shell getent passwd $(WO) |cut -d: -f3)
GID=$(shell getent group $(WG) |cut -d: -f3)

ifeq ($(strip $(UID)),)
$(error user $(WO) not found. Try "./mk-user $(WO) $(WWWTOP)" and run make again)
endif

ifeq ($(strip $(GID)),)
$(error GID is not set)
endif

# Configuration database
NAVIDBPATH = $(WWWTOP)/inc/navi.db
# Kplex config file
KPCONFPATH = $(WWWTOP)/inc/kplex.conf

UPLOADPATH = $(WWWTOP)/upload

# Where to install binaries
DEST=/usr/local/bin

# Where to find websockets lib
LIBDIR=/usr/local/lib
                                                                                                      
# Extra includes
INCDIR=/usr/local/include

CC=gcc

GETC=".git/HEAD"

ifeq ($(shell test -e $(GETC) && echo -n yes),yes)
CFLAGS=-DREV=\"$(shell git log --pretty=format:'%h' -n 1 2>/dev/null)\"
endif

CFLAGS+= -Wall -g -std=gnu99 -pedantic  -D_REENTRANT
CFLAGS+= -DUID=$(UID) -DGID=$(GID) -I$(INCDIR) -DUPLOADPATH=\"$(UPLOADPATH)\"
CFLAGS+= -DNAVIDBPATH=\"$(NAVIDBPATH)\" -DKPCONFPATH=\"$(KPCONFPATH)\"

LDFLAGS=-L$(LIBDIR) -lwebsockets -lsqlite3 -lais -lpthread -lrt -lz -Wl,-rpath=$(LIBDIR)

all: $(BIN)

$(BIN):	$(SRCS) $(HDRS)
	$(CC) $(SRCS) $(CFLAGS) $(LDFLAGS) -o $(BIN)

clean:
	rm -f $(BIN) *~

distclean:
	@for i in $(SUBDIRS); do \
	echo "Clearing in $$i..."; \
	(cd $$i; $(MAKE) TOP=$(WWWTOP) clean); done;
	cd contrib && $(MAKE) clean
	$(MAKE) clean
	

install: $(BIN)
	sudo install -m 0755 -g root -o root $(BIN) -D $(DEST)/$(BIN)
	@if [ ! -e $(DEST)/a2dnotice ]; then \
		sudo install -m 755 -g root -o root a2dnotice -D $(DEST)/a2dnotice; \
	fi
	-sudo systemctl stop wsocknmea.service
	-sudo systemctl disable wsocknmea.service
	-sudo install -m 0644 -g root -o root wsocknmea.service -D /lib/systemd/system/
	-sudo systemctl enable wsocknmea.service

status: 
	-sudo systemctl status wsocknmea.service --no-pager -l || true

stop: 
	-sudo systemctl stop wsocknmea.service || true
	-sudo systemctl status wsocknmea.service --no-pager -l || true

restart: $(DEST)/$(BIN)
	-sudo systemctl restart wsocknmea.service || true
	-sudo systemctl status wsocknmea.service --no-pager -l || true

install-www:
	$(MAKE) install
	@for i in $(SUBDIRS); do \
	echo "Installing in $$i..."; \
	(cd $$i; $(MAKE) TOP=$(WWWTOP) WO=$(WO) WG=$(WG) install); done

install-dep:
	@if [ ! -f .updated ]; then \
		echo "\nNeeded packages:" ; \
		echo $(PKGS) | sed 's/\s\+/\n/g'; echo ; \
		read -p "OK to check for and install these packages (y/n)? " yn ; \
		if [ "$${yn}" = "y" ]; then \
			sudo apt-get update; \
			sudo apt-get install $(PKGS); \
			touch .updated; \
		fi \
	else \
		echo "Dependency installation  done"; \
	fi

install-contribs:
	cd contrib && $(MAKE)
	cd contrib && $(MAKE) install

contribs:
	cd contrib && $(MAKE)

install-configs:
	cd contrib && $(MAKE) TOP=$(WWWTOP) WO=$(WO) WG=$(WG) install-configs

world:
	$(MAKE) install-dep
	$(MAKE) contribs
	$(MAKE) install-contribs
	$(MAKE)
	$(MAKE) install-www
	$(MAKE) install-configs

