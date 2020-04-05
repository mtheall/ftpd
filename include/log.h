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

#include <cstdarg>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class Log;
using SharedLog = std::shared_ptr<Log>;
using WeakLog   = std::weak_ptr<Log>;

class Log
{
public:
	enum Level
	{
		DEBUG,
		INFO,
		ERROR,
		COMMAND,
		RESPONSE,
	};

	~Log ();

	void draw ();

	static SharedLog create ();
	static void bind (SharedLog log_);

	__attribute__ ((format (printf, 1, 2))) static void debug (char const *fmt_, ...);
	__attribute__ ((format (printf, 1, 2))) static void info (char const *fmt_, ...);
	__attribute__ ((format (printf, 1, 2))) static void error (char const *fmt_, ...);
	__attribute__ ((format (printf, 1, 2))) static void command (char const *fmt_, ...);
	__attribute__ ((format (printf, 1, 2))) static void response (char const *fmt_, ...);

	static void log (Level level_, char const *fmt_, va_list ap_);
	static void log (Level level_, std::string_view message_);

private:
	Log ();

	void _log (Level level_, char const *fmt_, va_list ap_);

	struct Message
	{
		Message (Level const level_, std::string message_)
		    : level (level_), message (std::move (message_))
		{
		}

		Level level;
		std::string message;
	};

	std::vector<Message> m_messages;
	platform::Mutex m_lock;
};
