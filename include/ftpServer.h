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

#include "ftpSession.h"
#include "platform.h"
#include "socket.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

class FtpServer;
using UniqueFtpServer = std::unique_ptr<FtpServer>;

/// \brief FTP server
class FtpServer
{
public:
	~FtpServer ();

	/// \brief Draw server and all of its sessions
	void draw ();

	/// \brief Create server
	/// \param port_ Port to listen on
	static UniqueFtpServer create (std::uint16_t port_);

	/// \brief Update free space
	static void updateFreeSpace ();

	/// \brief Server start time
	static std::time_t startTime ();

private:
	/// \brief Paramterized constructor
	/// \param port_ Port to listen on
	FtpServer (std::uint16_t port_);

	/// \brief Handle when network is found
	void handleNetworkFound ();

	/// \brief Handle when network is lost
	void handleNetworkLost ();

	/// \brief Server loop
	void loop ();

	/// \brief Thread entry point
	void threadFunc ();

	/// \brief Thread
	platform::Thread m_thread;

	/// \brief Mutex
	platform::Mutex m_lock;

	/// \brief Listen socket
	UniqueSocket m_socket;

	/// \brief ImGui window name
	std::string m_name;

	/// \brief Sessions
	std::vector<UniqueFtpSession> m_sessions;

	/// \brief Port to listen on
	std::uint16_t const m_port;

	/// \brief Whether thread should quit
	std::atomic<bool> m_quit;
};
