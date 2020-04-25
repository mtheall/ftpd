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

#pragma once

#include "platform.h"

extern char const *const g_dearImGuiVersion;
extern char const *const g_dearImGuiCopyright;
extern char const *const g_mitLicense;

#ifdef _3DS
extern char const *const g_libctruVersion;
extern char const *const g_citro3dVersion;
extern char const *const g_citro3dCopyright;
#endif

#ifdef __SWITCH__
extern char const *const g_libnxVersion;
extern char const *const g_deko3dVersion;
extern char const *const g_zstdVersion;
extern char const *const g_libnxCopyright;
extern char const *const g_deko3dCopyright;
extern char const *const g_zstdCopyright;
extern char const *const g_libnxLicense;
extern char const *const g_bsdLicense;
#endif

#if !defined(NDS)
extern char const *const g_zlibLicense;
#endif

#if !defined(NDS) && !defined(_3DS) && !defined(__SWITCH__)
extern char const *const g_glfwVersion;
extern char const *const g_glfwCopyright;
#endif
