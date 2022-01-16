PREFIX = /usr/local
MANPREFIX = $(PREFIX)/man

RM = rm -f
SED = sed
GZIP = gzip --force
INSTALL = install

CFLAGS = -pedantic -Wall -Wextra -Werror -Wno-unused-parameter -Os -s -lutil

TARGET = fand
MAN = $(TARGET).8.gz
RC = $(TARGET).rc

all: $(TARGET) $(MAN) $(RC)

$(TARGET): $(TARGET).c

$(MAN): $(MAN:%.gz=%)
	$(GZIP) -9 --keep $>

$(RC): $(RC).in
	$(SED) "s|PREFIX|$(PREFIX)|g" < $> > $@

.PHONY: install uninstall clean

install: all
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/sbin/
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/etc/rc.d/
	$(INSTALL) -d $(DESTDIR)$(MANPREFIX)/man8/
	$(INSTALL) -m 0555 $(TARGET) $(DESTDIR)$(PREFIX)/sbin/
	$(INSTALL) -m 0555 $(RC) $(DESTDIR)$(PREFIX)/etc/rc.d/$(TARGET)
	$(INSTALL) -m 0444 $(MAN) $(DESTDIR)$(MANPREFIX)/man8/

uninstall:
	$(RM) $(DESTDIR)$(PREFIX)/sbin/$(TARGET)
	$(RM) $(DESTDIR)$(PREFIX)/etc/rc.d/$(TARGET)
	$(RM) $(DESTDIR)$(MANPREFIX)/man8/$(MAN)

clean:
	$(RM) $(TARGET) $(MAN) $(RC)
