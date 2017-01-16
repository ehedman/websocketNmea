
APPS=$(shell ls *.js *.css *.php *.sh )

ifeq ($(strip $(TOP)),)
$(error TOP is not set. Invoke from top-makefile)
endif

DEST=$(TOP)/inc

install: $(APPS)
	@for i in $(APPS); do \
	echo "Installing  $$i in $(DEST)"; \
        (sudo install -m 0644 -g $(WG) -o $(WO) $$i -D $(DEST)/$$i); done
	sudo chown $(WO):$(WG) $(DEST)
	sudo chmod 755 $(DEST)/*.sh

clean:
	rm -f  *~
