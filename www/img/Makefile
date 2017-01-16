
IMGS=$(shell ls *.png *.jpg *.ico)

ifeq ($(strip $(TOP)),)
$(error TOP is not set. Invoke from top-makefile)
endif

DEST=$(TOP)/img

SUBDIRS= bg

install: $(IMGS)
	@for i in $(IMGS); do \
	echo "Installing  $$i in $(DEST)"; \
	(sudo install -C -m 0664 -g $(WG) -o $(WO) $$i -D $(DEST)/$$i); done
	@for i in $(SUBDIRS); do \
	echo "Installing in $$i..."; \
	(cd $$i; $(MAKE) TOP=$(DEST) WO=$(WO) WG=$(WG) install); done

clean:
	rm -f  *~
	@for i in $(SUBDIRS); do \
        echo "Cleaning in $$i..."; \
	(cd $$i; $(MAKE) TOP=$(TOP) clean); done

