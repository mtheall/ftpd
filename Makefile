.PHONY: all nro 3dsx cia clean linux

export GITREV  := $(shell git rev-parse HEAD 2>/dev/null | cut -c1-8)
export VERSION_MAJOR := 2
export VERSION_MINOR := 3
export VERSION_MICRO := 1
export VERSION := $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_MICRO)

ifneq ($(strip $(GITREV)),)
export VERSION := $(VERSION)-$(GITREV)
endif

all:
	@echo please choose 3dsx, cia, linux, or nro

release:
	# can't let these three run in parallel with each other due to using same
	# ftpd.elf file name
	@$(MAKE) -f Makefile.switch all
	@$(MAKE) -f Makefile.3ds 3dsx
	@$(MAKE) -f Makefile.3ds cia
	@xz -c <ftpd.3dsx >ftpd.3dsx.xz
	@xz -c <ftpd.cia >ftpd.cia.xz
	@xz -c <ftpd.nro >ftpd.nro.xz

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
	@$(RM) ftpd.3dsx.xz	ftpd.cia.xz	ftpd.nro.xz
