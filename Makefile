.PHONY: all clean linux

export VERSION := 2.2

all:
	@$(MAKE) -f Makefile.3ds

linux:
	@$(MAKE) -f Makefile.linux

clean:
	@$(MAKE) -f Makefile.3ds   clean
	@$(MAKE) -f Makefile.linux clean
