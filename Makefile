.PHONY: all nro 3dsx cia clean linux

export GITREV  := $(shell git rev-parse HEAD 2>/dev/null | cut -c1-8)
export VERSION_MAJOR := 2
export VERSION_MINOR := 3
export VERSION_MICRO := 0
export VERSION := $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_MICRO)

ifneq ($(strip $(GITREV)),)
export VERSION := $(VERSION)-$(GITREV)
endif

all:
	@echo please choose 3dsx, cia, linux, or nro

nro:
	@$(MAKE) -f Makefile.switch all

3dsx:
	@$(MAKE) -f Makefile.3ds 3dsx

cia:
	@$(MAKE) -f Makefile.3ds cia

linux:
	@$(MAKE) -f Makefile.linux

clean:
	@$(MAKE) -f Makefile.switch   clean
	@$(MAKE) -f Makefile.3ds   clean
	@$(MAKE) -f Makefile.linux clean
