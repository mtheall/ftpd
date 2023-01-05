.PHONY: all all-classic format clean
.PHONY: dslink 3dslink 3dslink-classic nxlink-classic
.PHONY: nds 3dsx cia nro linux
.PHONY: 3dsx-classic cia-classic nro-classic
.PHONY: release release-nds release-3dsx release-cia release-nro
.PHONY: release-3dsx-classic release-cia-classic release-nro-classic

export GITREV  := $(shell git rev-parse HEAD 2>/dev/null | cut -c1-6)
export VERSION_MAJOR := 3
export VERSION_MINOR := 1
export VERSION_MICRO := 0
export VERSION := $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_MICRO)

###########################################################################
all: nds 3dsx nro linux

all-classic: nds 3dsx-classic nro-classic linux

format:
	@clang-format -style=file -i $(filter-out \
		include/imgui.h \
		source/imgui/imgui.cpp \
		source/imgui/imgui_demo.cpp \
		source/imgui/imgui_draw.cpp \
		source/imgui/imgui_internal.h \
		source/imgui/imgui_internal.h, \
		source/imgui/imgui_tables.cpp \
		source/imgui/imgui_widgets.cpp \
		source/imgui/imstb_rectpack.h \
		source/imgui/imstb_textedit.h \
		source/imgui/imstb_truetype.h \
		source/linux/KHR/khrplatform.h \
		source/linux/glad.c \
		source/linux/glad/glad.h \
		source/linux/imgui_impl_glfw.cpp \
		source/linux/imgui_impl_glfw.h \
		source/linux/imgui_impl_opengl3.cpp \
		source/linux/imgui_impl_opengl3.h \
		source/linux/imgui_impl_opengl3_loader.h \
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
	@$(RM) -r release
	@mkdir release
	@xz -c <nds/ftpd.nds >release/ftpd.nds.xz
	@ln -s ../nds/ftpd.nds release/ftpd.nds
	@xz -c <3ds/ftpd.3dsx >release/ftpd.3dsx.xz
	@ln -s ../3ds/ftpd.3dsx release/ftpd.3dsx
	@xz -c <3ds-classic/ftpd-classic.3dsx >release/ftpd-classic.3dsx.xz
	@ln -s ../3ds-classic/ftpd-classic.3dsx release/ftpd-classic.3dsx
	@xz -c <3ds/ftpd.cia >release/ftpd.cia.xz
	@ln -s ../3ds/ftpd.cia release/ftpd.cia
	@xz -c <3ds-classic/ftpd-classic.cia >release/ftpd-classic.cia.xz
	@ln -s ../3ds-classic/ftpd-classic.cia release/ftpd-classic.cia
	@xz -c <switch/ftpd.nro >release/ftpd.nro.xz
	@ln -s ../switch/ftpd.nro release/ftpd.nro
	@xz -c <switch-classic/ftpd-classic.nro >release/ftpd-classic.nro.xz
	@ln -s ../switch-classic/ftpd-classic.nro release/ftpd-classic.nro

release-nds:
	@$(MAKE) -f Makefile.nds DEFINES=-DNDEBUG OPTIMIZE="-O3 -flto"

release-3dsx:
	@$(MAKE) -f Makefile.3ds 3dsx DEFINES=-DNDEBUG OPTIMIZE="-O3 -flto"

release-3dsx-classic:
	@$(MAKE) -f Makefile.3ds 3dsx DEFINES=-DNDEBUG OPTIMIZE="-O3 -flto" CLASSIC="-DCLASSIC"

release-cia: release-3dsx
	@$(MAKE) -f Makefile.3ds cia DEFINES=-DNDEBUG OPTIMIZE="-O3 -flto"

release-cia-classic: release-3dsx-classic
	@$(MAKE) -f Makefile.3ds cia DEFINES=-DNDEBUG OPTIMIZE="-O3 -flto" CLASSIC="-DCLASSIC"

release-nro:
	@$(MAKE) -f Makefile.switch all DEFINES=-DNDEBUG OPTIMIZE="-O3 -flto"

release-nro-classic:
	@$(MAKE) -f Makefile.switch all DEFINES=-DNDEBUG OPTIMIZE="-O3 -flto" CLASSIC="-DCLASSIC"
