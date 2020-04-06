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

/// \brief Log object
class Log
{
public:
	/// \brief Log level
	enum Level
	{
		DEBUG,
		INFO,
		ERROR,
		COMMAND,
		RESPONSE,
	};

	~Log ();

	/// \brief Draw log
	void draw ();

	/// \brief Create log
	static SharedLog create ();

	/// \brief Bind log
	/// \param log_ Log to bind
	static void bind (SharedLog log_);

	/// \brief Add debug message to bound log
	/// \param fmt_ Message format
	__attribute__ ((format (printf, 1, 2))) static void debug (char const *fmt_, ...);
	/// \brief Add info message to bound log
	/// \param fmt_ Message format
	__attribute__ ((format (printf, 1, 2))) static void info (char const *fmt_, ...);
	/// \brief Add error message to bound log
	/// \param fmt_ Message format
	__attribute__ ((format (printf, 1, 2))) static void error (char const *fmt_, ...);
	/// \brief Add command message to bound log
	/// \param fmt_ Message format
	__attribute__ ((format (printf, 1, 2))) static void command (char const *fmt_, ...);
	/// \brief Add response message to bound log
	/// \param fmt_ Message format
	__attribute__ ((format (printf, 1, 2))) static void response (char const *fmt_, ...);

	/// \brief Add log message to bound log
	/// \param level_ Log level
	/// \param fmt_ Message format
	/// \param ap_ Message arguments
	static void log (Level level_, char const *fmt_, va_list ap_);

	/// \brief Add log message to bound log
	/// \param level_ Log level
	/// \param message_ Message to log
	static void log (Level level_, std::string_view message_);

private:
	Log ();

	/// \brief Add log message
	/// \param level_ Log level
	/// \param fmt_ Message format
	/// \param ap_ Message arguments
	void _log (Level level_, char const *fmt_, va_list ap_);

	/// \brief Log message
	struct Message
	{
		/// \brief Parameterized constructor
		/// \param level_ Log level
		/// \param message_ Log message
		Message (Level const level_, std::string message_)
		    : level (level_), message (std::move (message_))
		{
		}

		/// \brief Log level
		Level level;
		/// \brief Log message
		std::string message;
	};

	/// \brief Log messages
	std::vector<Message> m_messages;

	/// \brief Log lock
	platform::Mutex m_lock;
};
