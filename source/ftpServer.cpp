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

#include "ftpServer.h"

#include "fs.h"
#include "log.h"
#include "platform.h"
#include "socket.h"

#include "imgui.h"

#ifdef NDS
#include <dswifi9.h>
#endif

#include <arpa/inet.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
using namespace std::chrono_literals;

#ifdef NDS
#define LOCKED(x) x
#else
#define LOCKED(x)                                                                                  \
	do                                                                                             \
	{                                                                                              \
		auto const lock = std::scoped_lock (m_lock);                                               \
		x;                                                                                         \
	} while (0)
#endif

namespace
{
/// \brief Application start time
auto const s_startTime = std::time (nullptr);

#ifndef NDS
/// \brief Mutex for s_freeSpace
platform::Mutex s_lock;
#endif

/// \brief Free space string
std::string s_freeSpace;
}

///////////////////////////////////////////////////////////////////////////
FtpServer::~FtpServer ()
{
	m_quit = true;

#ifndef NDS
	m_thread.join ();
#endif
}

FtpServer::FtpServer (std::uint16_t const port_) : m_port (port_), m_quit (false)
{
#ifndef NDS
	m_thread = platform::Thread (std::bind (&FtpServer::threadFunc, this));
#endif
}

void FtpServer::draw ()
{
#ifdef NDS
	loop ();
#endif

#ifdef CLASSIC
	{
		char port[7];
#ifndef NDS
		auto lock = std::scoped_lock (m_lock);
#endif
		if (m_socket)
			std::sprintf (port, ":%u", m_socket->sockName ().port ());

		consoleSelect (&g_statusConsole);
		std::printf ("\x1b[0;0H\x1b[32;1m%s \x1b[36;1m%s%s",
		    STATUS_STRING,
		    m_socket ? m_socket->sockName ().name () : "Waiting on WiFi",
		    m_socket ? port : "");

#ifndef NDS
		char timeBuffer[16];
		auto const now = std::time (nullptr);
		std::strftime (timeBuffer, sizeof (timeBuffer), "%H:%M:%S", std::localtime (&now));

		std::printf (" \x1b[37;1m%s", timeBuffer);
#endif

		std::fputs ("\x1b[K", stdout);
		std::fflush (stdout);
	}

	{
#ifndef NDS
		auto const lock = std::scoped_lock (s_lock);
#endif
		if (!s_freeSpace.empty ())
		{
			consoleSelect (&g_statusConsole);
			std::printf ("\x1b[0;%uH\x1b[32;1m%s",
			    static_cast<unsigned> (g_statusConsole.windowWidth - s_freeSpace.size () + 1),
			    s_freeSpace.c_str ());
			std::fflush (stdout);
		}
	}

	{
#ifndef NDS
		auto lock = std::scoped_lock (m_lock);
#endif
		consoleSelect (&g_sessionConsole);
		std::fputs ("\x1b[2J", stdout);
		for (auto &session : m_sessions)
		{
			session->draw ();
			if (&session != &m_sessions.back ())
				std::fputc ('\n', stdout);
		}
		std::fflush (stdout);
	}

	drawLog ();
#else
	auto const &io    = ImGui::GetIO ();
	auto const width  = io.DisplaySize.x;
	auto const height = io.DisplaySize.y;

	ImGui::SetNextWindowPos (ImVec2 (0, 0), ImGuiCond_FirstUseEver);
#ifdef _3DS
	// top screen
	ImGui::SetNextWindowSize (ImVec2 (width, height * 0.5f));
#else
	ImGui::SetNextWindowSize (ImVec2 (width, height));
#endif
	{
		char title[64];

		{
			auto const serverLock = std::scoped_lock (m_lock);
			std::snprintf (title,
			    sizeof (title),
			    STATUS_STRING " %s###ftpd",
			    m_socket ? m_name.c_str () : "Waiting for WiFi...");
		}

		ImGui::Begin (title,
		    nullptr,
		    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
	}

#ifndef _3DS
	ImGui::BeginChild ("Logs", ImVec2 (0, 200), false, ImGuiWindowFlags_HorizontalScrollbar);
#endif
	drawLog ();
#ifndef _3DS
	ImGui::EndChild ();
#endif

#ifdef _3DS
	ImGui::End ();

	// bottom screen
	ImGui::SetNextWindowSize (ImVec2 (width * 0.8f, height * 0.5f));
	ImGui::SetNextWindowPos (ImVec2 (width * 0.1f, height * 0.5f), ImGuiCond_FirstUseEver);
	ImGui::Begin ("Sessions",
	    nullptr,
	    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
#else
	ImGui::Separator ();
#endif

	{
		auto lock = std::scoped_lock (m_lock);
		for (auto &session : m_sessions)
			session->draw ();
	}

	ImGui::End ();
#endif
}

UniqueFtpServer FtpServer::create (std::uint16_t const port_)
{
	updateFreeSpace ();
	return UniqueFtpServer (new FtpServer (port_));
}

std::string FtpServer::getFreeSpace ()
{
#ifndef NDS
	auto const lock = std::scoped_lock (s_lock);
#endif
	return s_freeSpace;
}

void FtpServer::updateFreeSpace ()
{
	struct statvfs st;
#if defined(NDS) || defined(_3DS) || defined(__SWITCH__)
	if (::statvfs ("sdmc:/", &st) != 0)
#else
	if (::statvfs ("/", &st) != 0)
#endif
		return;

	auto freeSpace = fs::printSize (static_cast<std::uint64_t> (st.f_bsize) * st.f_bfree);

#ifndef NDS
	auto const lock = std::scoped_lock (s_lock);
#endif
	if (freeSpace != s_freeSpace)
		s_freeSpace = std::move (freeSpace);
}

std::time_t FtpServer::startTime ()
{
	return s_startTime;
}

void FtpServer::handleNetworkFound ()
{
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
#if defined(NDS)
	addr.sin_addr = Wifi_GetIPInfo (nullptr, nullptr, nullptr, nullptr);
#elif defined(_3DS) || defined(__SWITCH__)
	addr.sin_addr.s_addr = gethostid ();
#else
	addr.sin_addr.s_addr = INADDR_ANY;
#endif
	addr.sin_port = htons (m_port);

	auto socket = Socket::create ();
	if (!socket)
		return;

	if (m_port != 0 && !socket->setReuseAddress (true))
		return;

	if (!socket->bind (addr))
		return;

	if (!socket->listen (10))
		return;

	auto const &sockName = socket->sockName ();
	auto const name      = sockName.name ();

	m_name.resize (std::strlen (name) + 3 + 5);
	m_name.resize (std::sprintf (&m_name[0], "[%s]:%u", name, sockName.port ()));

	info ("Started server at %s\n", m_name.c_str ());

	LOCKED (m_socket = std::move (socket));
}

void FtpServer::handleNetworkLost ()
{
	{
		UniqueSocket sock;
		LOCKED (sock = std::move (m_socket));
	}

	info ("Stopped server at %s\n", m_name.c_str ());
}

void FtpServer::loop ()
{
	if (!m_socket)
	{
		if (platform::networkVisible ())
			handleNetworkFound ();
	}

	// poll listen socket
	if (m_socket)
	{
		Socket::PollInfo info{*m_socket, POLLIN, 0};
		if (Socket::poll (&info, 1, 0ms) > 0)
		{
			auto socket = m_socket->accept ();
			if (socket)
			{
				auto session = FtpSession::create (std::move (socket));
				LOCKED (m_sessions.emplace_back (std::move (session)));
			}
			else
				handleNetworkLost ();
		}
	}

	{
		std::vector<UniqueFtpSession> deadSessions;
		{
			// remove dead sessions
#ifndef NDS
			auto lock = std::scoped_lock (m_lock);
#endif
			auto it = std::begin (m_sessions);
			while (it != std::end (m_sessions))
			{
				auto &session = *it;
				if (session->dead ())
				{
					deadSessions.emplace_back (std::move (session));
					it = m_sessions.erase (it);
				}
				else
					++it;
			}
		}
	}

	// poll sessions
	if (!m_sessions.empty ())
	{
		if (!FtpSession::poll (m_sessions))
			handleNetworkLost ();
	}
#ifndef NDS
	// avoid busy polling in background thread
	else
		platform::Thread::sleep (16ms);
#endif
}

void FtpServer::threadFunc ()
{
	while (!m_quit)
		loop ();
}
