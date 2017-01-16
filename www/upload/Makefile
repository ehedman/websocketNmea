#
# Please visit http://www.navmonpc.com/ to check out another great navigation application but for Windows.
#

APPS=PacCupStart.txt README

all: $(APPS)

ifeq ($(strip $(TOP)),)
$(error TOP is not set. Invoke from top-makefile)
endif

DEST=$(TOP)/upload

PacCupStart.txt :
	wget -O PacCupStart.zip  'http://www.navmonpc.com/Downloads/Pac%20Cup%202012%20Start.zip'
	unzip PacCupStart.zip
	mv Pac*.txt PacCupStart.txt

install: $(APPS)
	@for i in $(APPS); do \
	echo "Installing  $$i in $(DEST)"; \
        (sudo install -m 0664 -g $(WG) -o $(WO) $$i -D $(DEST)/$$i); done
	sudo chown $(WO):$(WG) $(DEST)

clean:
	rm -f PacCupStart.txt
	rm -f PacCupStart.zip
	rm -f  *~
