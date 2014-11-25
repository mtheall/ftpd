.PHONY: all clean linux

all:
	@$(MAKE) -f Makefile.3ds

linux:
	@$(MAKE) -f Makefile.linux

clean:
	@$(MAKE) -f Makefile.3ds   clean
	@$(MAKE) -f Makefile.linux clean
