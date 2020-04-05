.PHONY: all nro 3dsx cia clean linux 3dslink nxlink format release release-3dsx release-cia release-3ds release-nro

TARGET := $(notdir $(CURDIR))

export GITREV  := $(shell git rev-parse HEAD 2>/dev/null | cut -c1-8)
export VERSION_MAJOR := 3
export VERSION_MINOR := 0
export VERSION_MICRO := 0
export VERSION := $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_MICRO)-rc1

ifneq ($(strip $(GITREV)),)
export VERSION := $(VERSION)-$(GITREV)
endif

all: 3dsx nro linux

nxlink:
	@$(MAKE) -f Makefile.switch nxlink

3dslink: 3dsx
	@/opt/devkitpro/tools/bin/3dslink $(TARGET)-3ds.3dsx

format:
	@clang-format -style=file -i $(filter-out \
		include/imgui.h \
		source/pc/imgui_impl_glfw.cpp \
		source/pc/imgui_impl_glfw.h \
		source/pc/imgui_impl_opengl3.cpp \
		source/pc/imgui_impl_opengl3.h \
		source/pc/KHR/khrplatform.h \
		source/pc/glad.c \
		source/pc/glad/glad.h \
		source/imgui/imgui.cpp \
		source/imgui/imgui_demo.cpp \
		source/imgui/imgui_draw.cpp \
		source/imgui/imgui_widgets.cpp \
		source/imgui/imstb_rectpack.h \
		source/imgui/imstb_textedit.h \
		source/imgui/imstb_truetype.h \
		source/imgui/imgui_internal.h, \
		$(shell find source include -type f -name \*.c -o -name \*.cpp -o -name \*.h))

release: release-3ds release-nro
	@xz -c <$(TARGET)-3ds.3dsx >ftpd.3dsx.xz
	@echo xz -c <$(TARGET)-3ds.cia >ftpd.cia.xz
	@echo xz -c <$(TARGET)-nx.nro >ftpd.nro.xz

nro:
	@$(MAKE) -f Makefile.switch all

release-nro:
	@$(MAKE) DEFINES=-DNDEBUG -f Makefile.switch all

3dsx:
	@$(MAKE) -f Makefile.3ds 3dsx

release-3dsx:
	@$(MAKE) DEFINES=-DNDEBUG -f Makefile.3ds 3dsx

cia: 3dsx
	@$(MAKE) -f Makefile.3ds cia

release-cia: release-3dsx
	@$(MAKE) DEFINES=-NDEBUG -f Makefile.3ds cia

release-3ds:
	# can't let these three run in parallel with each other due to using same
	# .elf file name
	@$(MAKE) DEFINES=-DNDEBUG -f Makefile.3ds 3dsx
	@$(MAKE) DEFINES=-DNDEBUG -f Makefile.3ds cia

linux:
	@$(MAKE) -f Makefile.linux

clean:
	@$(MAKE) -f Makefile.switch clean
	@$(MAKE) -f Makefile.3ds clean
	@$(MAKE) -f Makefile.linux clean
	@$(RM) ftpd.3dsx.xz	ftpd.cia.xz	ftpd.nro.xz
