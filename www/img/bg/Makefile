
IMGS=$(shell ls *.jpg)

ifeq ($(strip $(TOP)),)
$(error TOP is not set. Invoke from top-makefile)
endif

DEST=$(TOP)/bg

install: $(IMGS)
	@for i in $(IMGS); do \
	echo "Installing  $$i in $(DEST)"; \
	(sudo install -m 0664 -g $(WG) -o $(WO) $$i -D $(DEST)/$$i); done

clean:
	rm -f  *~
