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

#include "ioBuffer.h"
#include "sockAddr.h"

#include <chrono>
#include <memory>

class Socket;
using UniqueSocket = std::unique_ptr<Socket>;
using SharedSocket = std::shared_ptr<Socket>;

class Socket
{
public:
	struct PollInfo
	{
		std::reference_wrapper<Socket> socket;
		int events;
		int revents;
	};

	~Socket ();

	UniqueSocket accept ();
	int atMark ();
	bool bind (SockAddr const &addr_);
	bool connect (SockAddr const &addr_);
	bool listen (int backlog_);
	bool shutdown (int how_);

	bool setLinger (bool enable_, std::chrono::seconds time_);
	bool setNonBlocking (bool nonBlocking_ = true);
	bool setReuseAddress (bool reuse_ = true);
	bool setRecvBufferSize (std::size_t size_);
	bool setSendBufferSize (std::size_t size_);

	ssize_t read (void *buffer_, std::size_t size_, bool oob_ = false);
	ssize_t read (IOBuffer &buffer_, bool oob_ = false);
	ssize_t write (void const *buffer_, std::size_t size_);
	ssize_t write (IOBuffer &buffer_);

	SockAddr const &sockName () const;
	SockAddr const &peerName () const;

	static UniqueSocket create ();

	static int poll (PollInfo *info_, std::size_t count_, std::chrono::milliseconds timeout_);

	int fd () const
	{
		return m_fd;
	}

private:
	Socket () = delete;

	Socket (int fd_);

	Socket (int fd_, SockAddr const &sockName_, SockAddr const &peerName_);

	Socket (Socket const &that_) = delete;

	Socket (Socket &&that_) = delete;

	Socket &operator= (Socket const &that_) = delete;

	Socket &operator= (Socket &&that_) = delete;

	SockAddr m_sockName;
	SockAddr m_peerName;

	int const m_fd;

	bool m_listening : 1;
	bool m_connected : 1;
};
