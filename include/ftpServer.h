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
#include "log.h"
#include "platform.h"
#include "socket.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

class FtpServer;
using UniqueFtpServer = std::unique_ptr<FtpServer>;

class FtpServer
{
public:
	~FtpServer ();

	void draw ();

	static UniqueFtpServer create (std::uint16_t port_);

	static void updateFreeSpace ();

	static std::time_t startTime ();

private:
	FtpServer (std::uint16_t port_);

	void handleStartButton ();
	void handleStopButton ();

	void loop ();
	void threadFunc ();

	platform::Thread m_thread;
	platform::Mutex m_lock;

	UniqueSocket m_socket;

	std::string m_name;

	SharedLog m_log;

	std::vector<UniqueFtpSession> m_sessions;

	std::uint16_t m_port;

	std::atomic<bool> m_quit;
};
