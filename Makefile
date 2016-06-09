.PHONY: all 3dsx cia clean linux

export VERSION := 2.2

all: 3dsx

3dsx:
	@$(MAKE) -f Makefile.3ds 3dsx

cia:
	@$(MAKE) -f Makefile.3ds cia
	
linux:
	@$(MAKE) -f Makefile.linux

clean:
	@$(MAKE) -f Makefile.3ds   clean
	@$(MAKE) -f Makefile.linux clean
