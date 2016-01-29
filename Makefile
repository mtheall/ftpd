.PHONY: all clean linux

export VERSION := 2.2

all:
	@$(MAKE) -f Makefile_3ds

linux:
	@$(MAKE) -f Makefile_linux

clean:
	@$(MAKE) -f Makefile_3ds   clean
	@$(MAKE) -f Makefile_linux clean
