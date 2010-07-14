man_MANS = ethtool.8
EXTRA_DIST = ethtool.8

sbin_PROGRAMS = ethtool
ethtool_SOURCES = ethtool.c ethtool-copy.h

RPMSRCS = /usr/src/redhat/SOURCES/
rpm:
	sed 's/@@RPMVER@@/$(VERSION)/' redhat/ethtool.spec.in > ethtool.spec
	tar --exclude=CVS -zcvf $(RPMSRCS)/$(PACKAGE)-$(VERSION).tar.gz .
	rpm -ta $(RPMSRCS)/$(PACKAGE)-$(VERSION).tar.gz
