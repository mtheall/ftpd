cmake_minimum_required(VERSION 3.12)

project(ftpd VERSION 3.2.0)

option(FTPD_CLASSIC "Build ${PROJECT_NAME} classic" OFF)

if(FTPD_CLASSIC AND (NINTENDO_SWITCH OR NINTENDO_3DS))
	set(FTPD_TARGET "${PROJECT_NAME}-classic")
else()
	set(FTPD_TARGET "${PROJECT_NAME}")
endif()

add_executable(${FTPD_TARGET})
target_compile_features(${FTPD_TARGET} PRIVATE cxx_std_20)
target_include_directories(${FTPD_TARGET} PRIVATE include)
target_compile_definitions(${FTPD_TARGET} PRIVATE
	STATUS_STRING="${PROJECT_NAME} v${PROJECT_VERSION}"
)

include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_SUPPORTED)

if(IPO_SUPPORTED)
	set_target_properties(${FTPD_TARGET} PROPERTIES
	    INTERPROCEDURAL_OPTIMIZATION FALSE
		INTERPROCEDURAL_OPTIMIZATION_DEBUG FALSE
		INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
		INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE
		INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL TRUE
	)
endif()

if(FTPD_CLASSIC OR NINTENDO_DS)
	target_compile_definitions(${FTPD_TARGET} PRIVATE CLASSIC)
endif()

if(NINTENDO_SWITCH OR NINTENDO_3DS OR NINTENDO_DS)
	target_compile_definitions(${FTPD_TARGET} PRIVATE
		NO_IPV6
		FTPDCONFIG="/config/${PROJECT_NAME}/${PROJECT_NAME}.cfg"
	)
endif()

target_sources(${FTPD_TARGET} PRIVATE
	include/fs.h
	include/ftpConfig.h
	include/ftpServer.h
	include/ftpSession.h
	include/ioBuffer.h
	include/log.h
	include/platform.h
	include/sockAddr.h
	include/socket.h
	source/fs.cpp
	source/ftpConfig.cpp
	source/ftpServer.cpp
	source/ftpSession.cpp
	source/ioBuffer.cpp
	source/log.cpp
	source/main.cpp
	source/sockAddr.cpp
	source/socket.cpp
)

if(NOT FTPD_CLASSIC AND NOT NINTENDO_DS)
	target_sources(${FTPD_TARGET} PRIVATE
		include/imconfig.h
		include/imgui.h
		include/licenses.h
		source/imgui/imgui.cpp
		source/imgui/imgui_draw.cpp
		source/imgui/imgui_internal.h
		source/imgui/imgui_tables.cpp
		source/imgui/imgui_widgets.cpp
		source/imgui/imstb_rectpack.h
		source/imgui/imstb_textedit.h
		source/imgui/imstb_truetype.h
		source/licenses.cpp
	)
endif()

if(NINTENDO_SWITCH)
	target_sources(${FTPD_TARGET} PRIVATE
		source/switch/init.c
		source/switch/platform.cpp
	)

	if(FTPD_CLASSIC)
		nx_generate_nacp(${FTPD_TARGET}.nacp
			NAME "${PROJECT_NAME} classic"
			AUTHOR "(c) 2023 Michael Theall, Dave Murphy, TuxSH"
			VERSION ${PROJECT_VERSION}
		)

		nx_create_nro(${FTPD_TARGET}
			NACP ${FTPD_TARGET}.nacp
			ICON meta/${PROJECT_NAME}.jpg
		)
		return()
	endif()

	include(FindPkgConfig)
	pkg_check_modules(CURL libcurl IMPORTED_TARGET)
	pkg_check_modules(ZSTD libzstd IMPORTED_TARGET)

	target_link_libraries(${FTPD_TARGET} PRIVATE
		deko3d$<$<CONFIG:Debug>:d>
		PkgConfig::CURL
		PkgConfig::ZSTD
	)

	target_sources(${FTPD_TARGET} PRIVATE
		source/switch/imgui_deko3d.cpp
		source/switch/imgui_deko3d.h
		source/switch/imgui_nx.cpp
		source/switch/imgui_nx.h
	)

	dkp_add_asset_target(${PROJECT_NAME}_romfs ${CMAKE_CURRENT_BINARY_DIR}/romfs)

	nx_add_shader_program(imgui_fsh source/switch/imgui_fsh.glsl frag)
	nx_add_shader_program(imgui_vsh source/switch/imgui_vsh.glsl vert)

	foreach(PNG 
		airplane_icon.png
		battery_icon.png
		charging_icon.png
		eth_icon.png
		eth_none_icon.png
		wifi1_icon.png
		wifi2_icon.png
		wifi3_icon.png
		wifi_none_icon.png)
		string(REGEX REPLACE "[.]png$" ".rgba" RGBA ${PNG})
		string(REGEX REPLACE "[.]png$" ".rgba.zst" ZST ${PNG})

		add_custom_command(OUTPUT ${ZST}
			COMMAND
				convert ${CMAKE_CURRENT_SOURCE_DIR}/switch/gfx/${PNG} ${RGBA}
			COMMAND
				zstd -f ${RGBA} -o ${ZST} --ultra -22
			MAIN_DEPENDENCY
				switch/gfx/${PNG}
			BYPRODUCTS
				${RGBA}
		)
		add_custom_target(${ZST}_target DEPENDS ${ZST})

		dkp_set_target_file(${ZST}_target ${ZST})
		dkp_install_assets(${PROJECT_NAME}_romfs TARGETS ${ZST}_target)
	endforeach()

	foreach(ASTC deko3d.12x12.astc)
		string(REGEX REPLACE "[.]astc$" ".astc.zst" ZST ${ASTC})

		add_custom_command(OUTPUT ${ZST}
			COMMAND
				zstd -f ${CMAKE_CURRENT_SOURCE_DIR}/switch/gfx/${ASTC} -o ${ZST} --ultra -22
			MAIN_DEPENDENCY
				switch/gfx/${ASTC}
		)
		add_custom_target(${ZST}_target DEPENDS ${ZST})

		dkp_set_target_file(${ZST}_target ${ZST})
		dkp_install_assets(${PROJECT_NAME}_romfs TARGETS ${ZST}_target)
	endforeach()

	dkp_install_assets(${PROJECT_NAME}_romfs
		DESTINATION
			shaders
		TARGETS
			imgui_fsh
			imgui_vsh
	)

	nx_generate_nacp(${FTPD_TARGET}.nacp
		NAME "${PROJECT_NAME} pro"
		AUTHOR "(c) 2023 Michael Theall, Dave Murphy, TuxSH"
		VERSION ${PROJECT_VERSION}
	)

	nx_create_nro(${FTPD_TARGET}
		NACP ${FTPD_TARGET}.nacp
		ICON meta/${PROJECT_NAME}.jpg
		ROMFS ${PROJECT_NAME}_romfs
	)
elseif(NINTENDO_3DS)
	target_sources(${FTPD_TARGET} PRIVATE
		source/3ds/platform.cpp
	)

	if(FTPD_CLASSIC)
		ctr_generate_smdh(${FTPD_TARGET}.smdh
			NAME "${PROJECT_NAME} classic"
			DESCRIPTION "v${PROJECT_VERSION}"
			AUTHOR "(c) 2023 Michael Theall, Dave Murphy, TuxSH"
			ICON meta/icon.png
		)

		ctr_create_3dsx(${FTPD_TARGET}
			SMDH ${FTPD_TARGET}.smdh
		)
		return()
	endif()

	target_compile_definitions(${FTPD_TARGET} PRIVATE
		ANTI_ALIAS=1
	)

	include(FindPkgConfig)
	pkg_check_modules(CURL libcurl IMPORTED_TARGET)

	target_link_libraries(${FTPD_TARGET} PRIVATE
		citro3d$<$<CONFIG:Debug>:d>
		PkgConfig::CURL
	)

	target_sources(${FTPD_TARGET} PRIVATE
		source/3ds/imgui_citro3d.cpp
		source/3ds/imgui_citro3d.h
		source/3ds/imgui_ctru.cpp
		source/3ds/imgui_ctru.h
	)

	ctr_add_shader_library(vshader source/3ds/vshader.v.pica)
	dkp_add_embedded_binary_library(vshader_data vshader)
	target_link_libraries(${FTPD_TARGET} PRIVATE vshader_data)

	dkp_add_asset_target(${PROJECT_NAME}_romfs ${CMAKE_CURRENT_BINARY_DIR}/romfs)

	add_custom_target(ca_cert DEPENDS 3ds/romfs/sni.cloudflaressl.com.ca)
	dkp_set_target_file(ca_cert ${CMAKE_CURRENT_SOURCE_DIR}/3ds/romfs/sni.cloudflaressl.com.ca)
	dkp_install_assets(${PROJECT_NAME}_romfs TARGETS ca_cert)

	ctr_add_graphics_target(gfx ATLAS
		OPTIONS
			-f rgba -z auto -H gfx.h
		INPUTS
			3ds/gfx/battery0.png
			3ds/gfx/battery1.png
			3ds/gfx/battery2.png
			3ds/gfx/battery3.png
			3ds/gfx/battery4.png
			3ds/gfx/batteryCharge.png
			3ds/gfx/c3dlogo.png
			3ds/gfx/wifi1.png
			3ds/gfx/wifi2.png
			3ds/gfx/wifi3.png
			3ds/gfx/wifiNull.png
	)
	target_include_directories(${FTPD_TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

	dkp_install_assets(${PROJECT_NAME}_romfs
		TARGETS
			gfx
	)

	ctr_generate_smdh(${FTPD_TARGET}.smdh
		NAME "${PROJECT_NAME} pro"
		DESCRIPTION "v${PROJECT_VERSION}"
		AUTHOR "(c) 2023 Michael Theall, Dave Murphy, TuxSH"
		ICON meta/icon.png
	)

	ctr_create_3dsx(${FTPD_TARGET}
		SMDH ${FTPD_TARGET}.smdh
		ROMFS ${PROJECT_NAME}_romfs
	)
elseif(NINTENDO_DS)
	target_link_libraries(${FTPD_TARGET} PRIVATE
		dswifi9
		fat
	)

	target_sources(${FTPD_TARGET} PRIVATE
		source/nds/platform.cpp
	)

	nds_create_rom(${FTPD_TARGET}
		NAME "${PROJECT_NAME} classic"
		SUBTITLE1 "v${PROJECT_VERSION}"
		SUBTITLE2 "(c) 2023 mtheall"
		ICON nds/icon.bmp
	)
else()
	find_package(glfw3 REQUIRED)
	find_package(CURL REQUIRED)
	find_package(OpenGL REQUIRED)

	target_link_libraries(${FTPD_TARGET} PRIVATE CURL::libcurl glfw OpenGL::GL)

	target_compile_definitions(${FTPD_TARGET} PRIVATE
		FTPDCONFIG="${PROJECT_NAME}.cfg"
	)

	target_sources(${FTPD_TARGET} PRIVATE
		source/linux/imgui_impl_glfw.cpp
		source/linux/imgui_impl_glfw.h
		source/linux/imgui_impl_opengl3.cpp
		source/linux/imgui_impl_opengl3.h
		source/linux/imgui_impl_opengl3_loader.h
		source/linux/platform.cpp
	)
endif()
