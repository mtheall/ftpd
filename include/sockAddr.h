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

#include <netinet/in.h>
#include <sys/socket.h>

#include <cstdint>

class SockAddr
{
public:
	~SockAddr ();

	SockAddr ();

	SockAddr (SockAddr const &that_);

	SockAddr (SockAddr &&that_);

	SockAddr &operator= (SockAddr const &that_);

	SockAddr &operator= (SockAddr &&that_);

	SockAddr (struct sockaddr const &addr_);

	SockAddr (struct sockaddr_in const &addr_);

#ifndef _3DS
	SockAddr (struct sockaddr_in6 const &addr_);
#endif

	SockAddr (struct sockaddr_storage const &addr_);

	operator struct sockaddr_in const & () const;

#ifndef _3DS
	operator struct sockaddr_in6 const & () const;
#endif

	operator struct sockaddr_storage const & () const;

	operator struct sockaddr * ();
	operator struct sockaddr const * () const;

	std::uint16_t port () const;
	char const *name (char *buffer_, std::size_t size_) const;
	char const *name () const;

private:
	struct sockaddr_storage m_addr = {};
};
