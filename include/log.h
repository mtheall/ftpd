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

#include <cstdarg>
#include <string>
#include <string_view>

/// \brief Log level
enum LogLevel
{
	DEBUG,
	INFO,
	ERROR,
	COMMAND,
	RESPONSE,
};

/// \brief Draw log
void drawLog ();

/// \brief Add debug message to bound log
/// \param fmt_ Message format
__attribute__ ((format (printf, 1, 2))) void debug (char const *fmt_, ...);

/// \brief Add info message to bound log
/// \param fmt_ Message format
__attribute__ ((format (printf, 1, 2))) void info (char const *fmt_, ...);

/// \brief Add error message to bound log
/// \param fmt_ Message format
__attribute__ ((format (printf, 1, 2))) void error (char const *fmt_, ...);

/// \brief Add command message to bound log
/// \param fmt_ Message format
__attribute__ ((format (printf, 1, 2))) void command (char const *fmt_, ...);

/// \brief Add response message to bound log
/// \param fmt_ Message format
__attribute__ ((format (printf, 1, 2))) void response (char const *fmt_, ...);

/// \brief Add log message to bound log
/// \param level_ Log level
/// \param fmt_ Message format
/// \param ap_ Message arguments
void addLog (LogLevel level_, char const *fmt_, va_list ap_);

/// \brief Add log message to bound log
/// \param level_ Log level
/// \param message_ Message to log
void addLog (LogLevel level_, std::string_view message_);
