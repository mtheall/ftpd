// ftpd is a server implementation based on the following:
// - RFC  959 (https://tools.ietf.org/html/rfc959)
// - RFC 3659 (https://tools.ietf.org/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
//
// Copyright (C) 2023 Michael Theall
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

#ifdef __NDS__
struct sockaddr_storage
{
	unsigned short ss_family;
	char ss_data[sizeof (struct sockaddr_in) - sizeof (unsigned short)];
};
#endif

/// \brief Socket address
class SockAddr
{
public:
	~SockAddr ();

	SockAddr ();

	/// \brief Copy constructor
	/// \param that_ Object to copy
	SockAddr (SockAddr const &that_);

	/// \brief Move constructor
	/// \param that_ Object to move from
	SockAddr (SockAddr &&that_);

	/// \brief Copy assignment
	/// \param that_ Object to copy
	SockAddr &operator= (SockAddr const &that_);

	/// \brief Move assignment
	/// \param that_ Object to move from
	SockAddr &operator= (SockAddr &&that_);

	/// \param Parameterized constructor
	/// \param addr_ Address
	SockAddr (struct sockaddr const &addr_);

	/// \param Parameterized constructor
	/// \param addr_ Address
	SockAddr (struct sockaddr_in const &addr_);

#ifndef __3DS__
	/// \param Parameterized constructor
	/// \param addr_ Address
	SockAddr (struct sockaddr_in6 const &addr_);
#endif

	/// \param Parameterized constructor
	/// \param addr_ Address
	SockAddr (struct sockaddr_storage const &addr_);

	/// \param sockaddr_in cast operator
	operator struct sockaddr_in const & () const;

#ifndef __3DS__
	/// \param sockaddr_in6 cast operator
	operator struct sockaddr_in6 const & () const;
#endif

	/// \param sockaddr_storage cast operator
	operator struct sockaddr_storage const & () const;

	/// \param sockaddr* cast operator
	operator struct sockaddr * ();
	/// \param sockaddr const* cast operator
	operator struct sockaddr const * () const;

	/// \brief Address port
	std::uint16_t port () const;

	/// \brief Set address port
	/// \param port_ Port to set
	bool setPort (std::uint16_t port_);

	/// \brief Address name
	/// \param buffer_ Buffer to hold name
	/// \param size_ Size of buffer_
	/// \retval buffer_ success
	/// \retval nullptr failure
	char const *name (char *buffer_, std::size_t size_) const;

	/// \brief Address name
	/// \retval nullptr failure
	/// \note This function is not reentrant
	char const *name () const;

private:
	/// \brief Address storage
	struct sockaddr_storage m_addr = {};
};
