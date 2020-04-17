.PHONY: all all-classic format clean
.PHONY: dslink 3dslink 3dslink-classic nxlink-classic
.PHONY: nds 3dsx cia nro linux
.PHONY: 3dsx-classic cia-classic nro-classic
.PHONY: release release-nds release-3dsx release-cia release-nro
.PHONY: release-3dsx-classic release-cia-classic release-nro-classic

export GITREV  := $(shell git rev-parse HEAD 2>/dev/null | cut -c1-6)
export VERSION_MAJOR := 3
export VERSION_MINOR := 0
export VERSION_MICRO := 0
export VERSION := $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_MICRO)

###########################################################################
all: nds 3dsx nro linux

all-classic: nds 3dsx-classic nro-classic linux

format:
	@clang-format -style=file -i $(filter-out \
		include/imgui.h \
		source/linux/imgui_impl_glfw.cpp \
		source/linux/imgui_impl_glfw.h \
		source/linux/imgui_impl_opengl3.cpp \
		source/linux/imgui_impl_opengl3.h \
		source/linux/KHR/khrplatform.h \
		source/linux/glad.c \
		source/linux/glad/glad.h \
		source/imgui/imgui.cpp \
		source/imgui/imgui_demo.cpp \
		source/imgui/imgui_draw.cpp \
		source/imgui/imgui_widgets.cpp \
		source/imgui/imstb_rectpack.h \
		source/imgui/imstb_textedit.h \
		source/imgui/imstb_truetype.h \
		source/imgui/imgui_internal.h, \
		$(shell find source include -type f -name \*.c -o -name \*.cpp -o -name \*.h))

clean:
	@$(MAKE) -f Makefile.nds clean
	@$(MAKE) -f Makefile.3ds clean
	@$(MAKE) -f Makefile.3ds clean CLASSIC="-DCLASSIC"
	@$(MAKE) -f Makefile.switch clean
	@$(MAKE) -f Makefile.switch clean CLASSIC="-DCLASSIC"
	@$(MAKE) -f Makefile.linux clean
	@$(RM) ftpd.nds.xz ftpd*.3dsx.xz ftpd*.cia.xz ftpd*.nro.xz

###########################################################################
dslink:
	@$(MAKE) -f Makefile.nds dslink

3dslink:
	@$(MAKE) -f Makefile.3ds 3dslink

3dslink-classic:
	@$(MAKE) -f Makefile.3ds 3dslink CLASSIC="-DCLASSIC"

nxlink:
	@$(MAKE) -f Makefile.switch nxlink

nxlink-classic:
	@$(MAKE) -f Makefile.switch nxlink CLASSIC="-DCLASSIC"

###########################################################################
nds:
	@$(MAKE) -f Makefile.nds CLASSIC="-DCLASSIC"

3dsx:
	@$(MAKE) -f Makefile.3ds 3dsx

3dsx-classic:
	@$(MAKE) -f Makefile.3ds 3dsx CLASSIC="-DCLASSIC"

cia: 3dsx
	@$(MAKE) -f Makefile.3ds cia

cia-classic: 3dsx-classic
	@$(MAKE) -f Makefile.3ds cia CLASSIC="-DCLASSIC"

nro:
	@$(MAKE) -f Makefile.switch all

nro-classic:
	@$(MAKE) -f Makefile.switch all CLASSIC="-DCLASSIC"

linux:
	@$(MAKE) -f Makefile.linux

###########################################################################
release: release-nds \
		release-3dsx release-3dsx-classic \
		release-cia release-cia-classic \
		release-nro release-nro-classic
	@xz -c <nds/ftpd.nds >ftpd.nds.xz
	@xz -c <3ds/ftpd.3dsx >ftpd.3dsx.xz
	@xz -c <3ds-classic/ftpd-classic.3dsx >ftpd-classic.3dsx.xz
	@xz -c <3ds/ftpd.cia >ftpd.cia.xz
	@xz -c <3ds-classic/ftpd-classic.cia >ftpd-classic.cia.xz
	@xz -c <switch/ftpd.nro >ftpd.nro.xz
	@xz -c <switch-classic/ftpd-classic.nro >ftpd-classic.nro.xz

release-nds:
	@$(MAKE) -f Makefile.nds DEFINES=-DNDEBUG 

release-3dsx:
	@$(MAKE) -f Makefile.3ds 3dsx DEFINES=-DNDEBUG

release-3dsx-classic:
	@$(MAKE) -f Makefile.3ds 3dsx DEFINES=-DNDEBUG CLASSIC="-DCLASSIC"

release-cia: release-3dsx
	@$(MAKE) -f Makefile.3ds cia DEFINES=-DNDEBUG

release-cia-classic: release-3dsx-classic
	@$(MAKE) -f Makefile.3ds cia DEFINES=-DNDEBUG CLASSIC="-DCLASSIC"

release-nro:
	@$(MAKE) -f Makefile.switch all DEFINES=-DNDEBUG

release-nro-classic:
	@$(MAKE) -f Makefile.switch all DEFINES=-DNDEBUG CLASSIC="-DCLASSIC"
