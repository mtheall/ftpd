// ftpd is a server implementation based on the following:
// - RFC  959 (https://tools.ietf.org/html/rfc959)
// - RFC 3659 (https://tools.ietf.org/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
// - Deflate transmission mode for FTP
//   (https://tools.ietf.org/html/draft-preston-ftpext-deflate-04)
//
// Copyright (C) 2024 Michael Theall
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "platform.h"

#include "log.h"

#include <dswifi9.h>
#include <fat.h>

#include <netinet/in.h>

#include <cstring>

#ifndef CLASSIC
#error "NDS must be built in classic mode"
#endif

PrintConsole g_statusConsole;
PrintConsole g_logConsole;
PrintConsole g_sessionConsole;

namespace
{
/// \brief Host address
in_addr s_addr = {0};
/// \brief Which side of double-buffer we're on
bool s_backBuffer = false;
/// \brief Whether to power backlight
bool s_backlight = true;
}

bool platform::networkVisible ()
{
	switch (Wifi_AssocStatus ())
	{
	case ASSOCSTATUS_DISCONNECTED:
		s_addr.s_addr = 0;
		Wifi_AutoConnect ();
		break;

	case ASSOCSTATUS_SEARCHING:
	case ASSOCSTATUS_ASSOCIATING:
	case ASSOCSTATUS_ACQUIRINGDHCP:
		s_addr.s_addr = 0;
		break;

	case ASSOCSTATUS_ASSOCIATED:
		if (!s_addr.s_addr)
			s_addr = Wifi_GetIPInfo (nullptr, nullptr, nullptr, nullptr);
		return true;
	}

	return false;
}

bool platform::networkAddress (SockAddr &addr_)
{
	sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr   = Wifi_GetIPInfo (nullptr, nullptr, nullptr, nullptr);

	addr_ = addr;
	return true;
}

bool platform::init ()
{
	fatInitDefault ();
	defaultExceptionHandler ();

	// turn off unused arm7 hardware
	powerOff (PM_SOUND_AMP);
	powerOn (PM_SOUND_MUTE);

	// turn off unused arm9 hardware
	powerOff (POWER_MATRIX | POWER_3D_CORE);

	videoSetMode (MODE_0_2D);
	videoSetModeSub (MODE_0_2D);

	vramSetBankA (VRAM_A_MAIN_BG);
	vramSetBankC (VRAM_C_SUB_BG);

	consoleInit (&g_statusConsole, 0, BgType_Text4bpp, BgSize_T_256x256, 4, 0, true, true);
	g_logConsole = g_statusConsole;
	consoleInit (&g_sessionConsole, 0, BgType_Text4bpp, BgSize_T_256x256, 4, 0, false, true);

	consoleSetWindow (&g_statusConsole, 0, 0, 32, 1);
	consoleSetWindow (&g_logConsole, 0, 1, 32, 23);
	consoleSetWindow (&g_sessionConsole, 0, 0, 32, 24);

	consoleDebugInit (DebugDevice_NOCASH);
	std::setvbuf (stderr, nullptr, _IONBF, 0);

	Wifi_InitDefault (INIT_ONLY);
	Wifi_AutoConnect ();

	return true;
}

bool platform::loop ()
{
	if (!pmMainLoop ())
		return false;

	scanKeys ();

	// check if the user wants to exit
	auto const kDown = keysDown ();
	if (kDown & KEY_START)
		return false;

	// check if the user wants to toggle the backlight
	if (kDown & KEY_SELECT)
	{
		s_backlight = !s_backlight;
		(s_backlight ? powerOn : powerOff) (POWER_LCD);
	}

	return true;
}

void platform::render ()
{
	// make consoles point to maps being drawn
	g_statusConsole.fontBgMap  = bgGetMapPtr (g_statusConsole.bgId);
	g_logConsole.fontBgMap     = g_statusConsole.fontBgMap;
	g_sessionConsole.fontBgMap = bgGetMapPtr (g_sessionConsole.bgId);

	swiWaitForVBlank ();

	// point maps to back buffer to draw on next frame
	bgInit (0, BgType_Text4bpp, BgSize_T_256x256, 4 + s_backBuffer, 0);
	bgInitSub (0, BgType_Text4bpp, BgSize_T_256x256, 4 + s_backBuffer, 0);

	// initialize back buffer with previous contents
	dmaCopyWordsAsynch (0, bgGetMapPtr (g_statusConsole.bgId), g_statusConsole.fontBgMap, 0x800);
	dmaCopyWordsAsynch (1, bgGetMapPtr (g_sessionConsole.bgId), g_sessionConsole.fontBgMap, 0x800);
	while ((DMA_CR (0) & DMA_BUSY) || (DMA_CR (1) & DMA_BUSY))
	{
	}

	// flip buffers
	s_backBuffer = !s_backBuffer;
}

void platform::exit ()
{
	powerOn (POWER_LCD);
}
