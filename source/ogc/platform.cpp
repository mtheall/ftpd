// ftpd is a server implementation based on the following:
// - RFC  959 (https://tools.ietf.org/html/rfc959)
// - RFC 3659 (https://tools.ietf.org/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
//
// Copyright (C) 2020 Michael Theall
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

#include <cstddef>
#ifndef CLASSIC
#error "Wii and GameCube must be built in classic mode"
#endif

#include "platform.h"

#include "log.h"

#include <sys/socket.h>
#include <netinet/in.h>

#include <cstring>
#include <cassert>

#include <ogcsys.h>
#include <gccore.h>
#include <ogc/if_config.h>

#ifdef __wii__
#include <wiiuse/wpad.h>
#endif

#include <fat.h>

#include <mutex>
#include <thread>

PrintConsole g_statusConsole;
PrintConsole g_logConsole;
PrintConsole g_sessionConsole;


static bool networkUp = false;

namespace
{

static struct in_addr loc_ip, loc_netmask, loc_gateway;

}


bool platform::networkVisible ()
{
	return networkUp;
}

bool platform::networkAddress (SockAddr &addr_)
{
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_addr   = loc_ip;

	addr_ = addr;
	return true;
}

bool platform::init ()
{
	fatInitDefault ();

	VIDEO_Init();
#ifdef __wii__
	WPAD_Init();
#endif
	consoleInit (&g_statusConsole);
	consoleInit (&g_logConsole);
	consoleInit (&g_sessionConsole);

	consoleSetWindow (&g_statusConsole, 1, 1, 80, 1);
	consoleSetWindow (&g_logConsole, 1, 2, 80, 21);
	consoleSetWindow (&g_sessionConsole, 1, 23, 80, 8);
	consoleSelect(&g_sessionConsole);
	printf(CONSOLE_ESC(46;1m) CONSOLE_ESC(2J));

	int ret = if_configex ( &loc_ip, &loc_netmask, &loc_gateway, TRUE, 20);
	if (ret>=0) {
		networkUp = true;
	}

	return true;
}

bool platform::loop ()
{
#ifdef __wii__
	WPAD_ScanPads();

	u32 pressed = WPAD_ButtonsDown(0);

	if ( pressed & WPAD_BUTTON_HOME ) return false;
#endif
	return SYS_MainLoop();
}

void platform::render ()
{
	VIDEO_WaitVSync();
}

void platform::exit ()
{
}

std::string const &platform::hostname ()
{
	static std::string const hostname = "wii-ftpd";
	return hostname;
}
