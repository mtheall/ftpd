
// ftpd is a server implementation based on the following:
// - RFC  959 (https://datatracker.ietf.org/doc/html/rfc959)
// - RFC 3659 (https://datatracker.ietf.org/doc/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
//
// ftpd implements mdns based on the following:
// - RFC 1035 (https://datatracker.ietf.org/doc/html/rfc1035)
// - RFC 6762 (https://datatracker.ietf.org/doc/html/rfc6762)
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

#pragma once

#include "sockAddr.h"
#include "socket.h"

#include <cstddef>

namespace mdns
{
void setHostname (std::string hostname_);

UniqueSocket createSocket ();

void handleSocket (Socket *socket_, SockAddr const &addr_);
}
