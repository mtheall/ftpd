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

#include "log.h"

#include "platform.h"

#include "imgui.h"

#include <mutex>
#include <vector>

namespace
{
#ifdef _3DS
/// \brief Maximum number of log messages to keep
constexpr auto MAX_LOGS = 250;
#else
/// \brief Maximum number of log messages to keep
constexpr auto MAX_LOGS = 10000;
#endif

/// \brief Message prefix
static char const *const s_prefix[] = {
    [DEBUG]    = "[DEBUG]",
    [INFO]     = "[INFO]",
    [ERROR]    = "[ERROR]",
    [COMMAND]  = "[COMMAND]",
    [RESPONSE] = "[RESPONSE]",
};

/// \brief Log message
struct Message
{
	/// \brief Parameterized constructor
	/// \param level_ Log level
	/// \param message_ Log message
	Message (LogLevel const level_, std::string message_)
	    : level (level_), message (std::move (message_))
	{
	}

	/// \brief Log level
	LogLevel level;
	/// \brief Log message
	std::string message;
};

/// \brief Log messages
std::vector<Message> s_messages;

/// \brief Log lock
platform::Mutex s_lock;
}

void drawLog ()
{
	auto const lock = std::scoped_lock (s_lock);

	if (s_messages.size () > MAX_LOGS)
	{
		auto const begin = std::begin (s_messages);
		auto const end   = std::next (begin, s_messages.size () - MAX_LOGS);
		s_messages.erase (begin, end);
	}

	static ImVec4 const s_colors[] = {
	    [DEBUG]    = ImVec4 (1.0f, 1.0f, 0.4f, 1.0f), // yellow
	    [INFO]     = ImVec4 (1.0f, 1.0f, 1.0f, 1.0f), // white
	    [ERROR]    = ImVec4 (1.0f, 0.4f, 0.4f, 1.0f), // red
	    [COMMAND]  = ImVec4 (0.4f, 1.0f, 0.4f, 1.0f), // green
	    [RESPONSE] = ImVec4 (0.4f, 1.0f, 1.0f, 1.0f), // cyan
	};

	for (auto const &message : s_messages)
	{
		ImGui::PushStyleColor (ImGuiCol_Text, s_colors[message.level]);
		ImGui::TextUnformatted (s_prefix[message.level]);
		ImGui::SameLine ();
		ImGui::TextUnformatted (message.message.c_str ());
		ImGui::PopStyleColor ();
	}

	// auto-scroll if scroll bar is at end
	if (ImGui::GetScrollY () >= ImGui::GetScrollMaxY ())
		ImGui::SetScrollHereY (1.0f);
}

void debug (char const *const fmt_, ...)
{
#ifndef NDEBUG
	va_list ap;

	va_start (ap, fmt_);
	addLog (DEBUG, fmt_, ap);
	va_end (ap);
#endif
}

void info (char const *const fmt_, ...)
{
	va_list ap;

	va_start (ap, fmt_);
	addLog (INFO, fmt_, ap);
	va_end (ap);
}

void error (char const *const fmt_, ...)
{
	va_list ap;

	va_start (ap, fmt_);
	addLog (ERROR, fmt_, ap);
	va_end (ap);
}

void command (char const *const fmt_, ...)
{
	va_list ap;

	va_start (ap, fmt_);
	addLog (COMMAND, fmt_, ap);
	va_end (ap);
}

void response (char const *const fmt_, ...)
{
	va_list ap;

	va_start (ap, fmt_);
	addLog (RESPONSE, fmt_, ap);
	va_end (ap);
}

void addLog (LogLevel const level_, char const *const fmt_, va_list ap_)
{
#ifdef NDEBUG
	if (level_ == DEBUG)
		return;
#endif

	thread_local static char buffer[1024];

	std::vsnprintf (buffer, sizeof (buffer), fmt_, ap_);
	buffer[sizeof (buffer) - 1] = '\0';

	auto const lock = std::scoped_lock (s_lock);
#ifndef NDEBUG
	std::fprintf (stderr, "%s", s_prefix[level_]);
	std::fputs (buffer, stderr);
#endif
	s_messages.emplace_back (level_, buffer);
}

void addLog (LogLevel const level_, std::string_view const message_)
{
#ifdef NDEBUG
	if (level_ == DEBUG)
		return;
#endif

	auto msg = std::string (message_);
	for (auto &c : msg)
	{
		// replace nul-characters with ? to avoid truncation
		if (c == '\0')
			c = '?';
	}

	auto const lock = std::scoped_lock (s_lock);
#ifndef NDEBUG
	std::fprintf (stderr, "%s", s_prefix[level_]);
	std::fwrite (msg.data (), 1, msg.size (), stderr);
#endif
	s_messages.emplace_back (level_, msg);
}
