
SHELL=/bin/sh

MAKE = make

SUBDIRS = www

# Package dependencies
PKGS += g++
PKGS += git
PKGS += cmake
PKGS += automake
PKGS += libtool
PKGS += libpcre3 libpcre3-dev
PKGS += libbz2-dev
ifeq "$(shell apt-cache search php5| grep cgi)" ""
PKGS += php-cgi php-sqlite3
else
PKGS += php5-cgi php5-sqlite 
endif
PKGS += libsqlite3-dev sqlite3
PKGS += libssl-dev

# Source files to be compiled
SRCS = wsocknmea.c adc-sensors.c ais.c
HDRS = wsocknmea.h
BIN = wsocknmea

# Where to install web pages
WWWTOP = /var/www

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

# Where to install binaries
DEST=/usr/local/bin

# Where to find websockets lib
LIBDIR=/usr/local/lib
                                                                                                      
# Extra includes
INCDIR=/usr/local/include

CC=gcc
ARCH=$(shell $(CC) -dumpmachine |  cut -d- -f1 | tr '[:lower:]' '[:upper:]')

GETC=".git/HEAD"

ifeq ($(shell test -e $(GETC) && echo -n yes),yes)
CFLAGS=-DREV=\"$(shell git branch -v | awk '{print $$2"-"$$3}')\"
endif

CFLAGS+= -Wall -g -std=gnu99 -pedantic
CFLAGS+= -DARCH=$(ARCH) -DUID=$(UID) -DGID=$(GID) -I$(INCDIR)
CFLAGS+= -DNAVIDBPATH=\"$(NAVIDBPATH)\" -DKPCONFPATH=\"$(KPCONFPATH)\"

LDFLAGS=-L$(LIBDIR) -lwebsockets -lsqlite3 -lais -lpthread -lrt -Wl,-rpath=$(LIBDIR)

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
	@if [ ! -f /etc/init.d/wsocknmea-daemon ]; then \
		echo "Installing wsocknmea-daemon in /etc/init.d"; \
		sudo install -m 0755 -g root -o root wsocknmea-daemon -D /etc/init.d/wsocknmea-daemon; \
		sudo update-rc.d wsocknmea-daemon defaults; \
	fi

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

