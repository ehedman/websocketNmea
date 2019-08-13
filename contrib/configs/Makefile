
ifeq ($(strip $(TOP)),)
$(error TOP is not set. Invoke from top-makefile)
endif

install-configs:
	@sed 's/www-USER/$(WO)/g' lighttpd.conf | sed 's/www-GROUP/$(WG)/g' | sed "s|www-BASE|$(TOP)|g" >/tmp/lighttpd.conf
	@if [ -x /usr/sbin/lighttpd ]; then \
		echo "INSTALLED LIGHTTPD ABOUT TO BE REPLACED"; \
		sudo apt-get remove lighttpd; \
		if [ ! -x /usr/sbin/lighttpd ]; then \
			sudo systemctl unmask lighttpd.service  > /dev/null 2>&1; \
		fi \
        fi
	@if [ -x /bin/netstat ]; then \
		echo "Installing and starting lighttpd services"; \
		netstat -an | grep ":80 "| grep "LISTEN" >/dev/null 2>&1; \
		if [ $$? -eq 0 ]; then \
			pidof lighttpd  >/dev/null 2>&1; \
			if [ $$? -eq 0 ]; then \
				sudo install -m 0644 -g root -o root /tmp/lighttpd.conf -D /usr/local/etc/lighttpd.conf; \
				sudo install -m 0755 -g root -o root lighttpd -D /etc/init.d/lighttpd; \
				sudo systemctl unmask lighttpd.service  > /dev/null 2>&1; \
				sudo update-rc.d lighttpd defaults; \
				sudo  service lighttpd restart; \
			else \
				echo "\n\nAnother web server that is not lighttpd seems to be running."; \
				echo "The installation of lighttpd will not be completed!.\n\n"; \
			fi \
		else \
			sudo install -m 0644 -g root -o root /tmp/lighttpd.conf -D /usr/local/etc/lighttpd.conf; \
			sudo install -m 0755 -g root -o root lighttpd -D /etc/init.d/lighttpd; \
			sudo systemctl unmask lighttpd.service  > /dev/null 2>&1; \
			sudo update-rc.d lighttpd defaults; \
			sudo service lighttpd restart; \
		fi \
	fi
	@rm -f /tmp/lighttpd.conf
