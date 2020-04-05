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

#include "imgui.h"

#include <mutex>

namespace
{
#ifdef _3DS
constexpr auto MAX_LOGS = 250;
#else
constexpr auto MAX_LOGS = 10000;
#endif
thread_local WeakLog s_log;

static char const *const s_prefix[] = {
    [Log::DEBUG]    = "[DEBUG]",
    [Log::INFO]     = "[INFO]",
    [Log::ERROR]    = "[ERROR]",
    [Log::COMMAND]  = "[COMMAND]",
    [Log::RESPONSE] = "[RESPONSE]",
};
}

///////////////////////////////////////////////////////////////////////////
Log::~Log () = default;

Log::Log () = default;

void Log::draw ()
{
	auto const lock = std::scoped_lock (m_lock);

	if (m_messages.size () > MAX_LOGS)
	{
		auto const begin = std::begin (m_messages);
		auto const end   = std::next (begin, m_messages.size () - MAX_LOGS);
		m_messages.erase (begin, end);
	}

	static ImVec4 const s_colors[] = {
	    [Log::DEBUG]    = ImVec4 (1.0f, 1.0f, 0.4f, 1.0f),
	    [Log::INFO]     = ImVec4 (1.0f, 1.0f, 1.0f, 1.0f),
	    [Log::ERROR]    = ImVec4 (1.0f, 0.4f, 0.4f, 1.0f),
	    [Log::COMMAND]  = ImVec4 (0.4f, 1.0f, 0.4f, 1.0f),
	    [Log::RESPONSE] = ImVec4 (0.4f, 1.0f, 1.0f, 1.0f),
	};

	static char const *const s_prefix[] = {
	    [Log::DEBUG]    = "[DEBUG]",
	    [Log::INFO]     = "[INFO]",
	    [Log::ERROR]    = "[ERROR]",
	    [Log::COMMAND]  = "[COMMAND]",
	    [Log::RESPONSE] = "[RESPONSE]",
	};

	for (auto const &message : m_messages)
	{
		ImGui::PushStyleColor (ImGuiCol_Text, s_colors[message.level]);
		ImGui::TextUnformatted (s_prefix[message.level]);
		ImGui::SameLine ();
		ImGui::TextUnformatted (message.message.c_str ());
		ImGui::PopStyleColor ();
	}

	// auto scroll if scroll bar is at end
	if (ImGui::GetScrollY () >= ImGui::GetScrollMaxY ())
		ImGui::SetScrollHereY (1.0f);
}

SharedLog Log::create ()
{
	return std::shared_ptr<Log> (new Log ());
}

void Log::bind (SharedLog log_)
{
	s_log = log_;
}

void Log::debug (char const *const fmt_, ...)
{
#ifndef NDEBUG
	va_list ap;

	va_start (ap, fmt_);
	log (DEBUG, fmt_, ap);
	va_end (ap);
#endif
}

void Log::info (char const *const fmt_, ...)
{
	va_list ap;

	va_start (ap, fmt_);
	log (INFO, fmt_, ap);
	va_end (ap);
}

void Log::error (char const *const fmt_, ...)
{
	va_list ap;

	va_start (ap, fmt_);
	log (ERROR, fmt_, ap);
	va_end (ap);
}

void Log::command (char const *const fmt_, ...)
{
	va_list ap;

	va_start (ap, fmt_);
	log (COMMAND, fmt_, ap);
	va_end (ap);
}

void Log::response (char const *const fmt_, ...)
{
	va_list ap;

	va_start (ap, fmt_);
	log (RESPONSE, fmt_, ap);
	va_end (ap);
}

void Log::log (Level const level_, char const *const fmt_, va_list ap_)
{
#ifdef NDEBUG
	if (level_ == DEBUG)
		return;
#endif

	auto log = s_log.lock ();
	if (!log)
		return;

	thread_local static char buffer[1024];

	std::vsnprintf (buffer, sizeof (buffer), fmt_, ap_);
	buffer[sizeof (buffer) - 1] = '\0';

	auto const lock = std::scoped_lock (log->m_lock);
#ifndef NDEBUG
	std::fprintf (stderr, "%s", s_prefix[level_]);
	std::fputs (buffer, stderr);
#endif
	log->m_messages.emplace_back (level_, buffer);
}

void Log::log (Level const level_, std::string_view const message_)
{
#ifdef NDEBUG
	if (level_ == DEBUG)
		return;
#endif

	auto log = s_log.lock ();
	if (!log)
		return;

	auto msg = std::string (message_);
	for (auto &c : msg)
	{
		if (c == '\0')
			c = '?';
	}

	auto const lock = std::scoped_lock (log->m_lock);
#ifndef NDEBUG
	std::fprintf (stderr, "%s", s_prefix[level_]);
	std::fwrite (msg.data (), 1, msg.size (), stderr);
#endif
	log->m_messages.emplace_back (level_, msg);
}
