
APPS=$(shell ls licenseManifest.txt *.php *.html)

ifeq ($(strip $(TOP)),)
$(error TOP is not set. Invoke from top-makefile)
endif

DEST=$(TOP)

SUBDIRS= img inc upload audio

install: $(APPS)
	@for i in $(APPS); do \
	echo "Installing  $$i in $(DEST)"; \
	(sudo install -m 0664 -g $(WO) -o $(WG) $$i -D $(DEST)/$$i); done
	@for i in $(SUBDIRS); do \
	echo "Installing in $$i..."; \
	(cd $$i; $(MAKE) TOP=$(TOP) WO=$(WO) WG=$(WG) install); done

clean:
	rm -f  *~
	@for i in $(SUBDIRS); do \
	echo "Cleaning in $$i..."; \
	(cd $$i; $(MAKE) TOP=$(TOP) clean); done
