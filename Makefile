.PHONY: all 3dsx cia clean linux

export GITREV  := $(shell git rev-parse HEAD 2>/dev/null | cut -c1-8)
export VERSION := 2.2

ifneq ($(strip $(GITREV)),)
export VERSION := $(VERSION)-$(GITREV)
endif

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
