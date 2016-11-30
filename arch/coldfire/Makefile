CROSS_COMPILE ?=
EXTRA_CFLAGS ?=

SUBDIRS = i_5206 tracer common

all:
	for f in $(SUBDIRS); do \
		$(MAKE) -C $$f EXTRA_CFLAGS="$(EXTRA_CFLAGS)" CROSS_COMPILE=$(CROSS_COMPILE); \
	done

clean:
	for f in $(SUBDIRS); do \
		$(MAKE) -C $$f clean; \
	done
	rm -f tags
