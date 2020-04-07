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

#include "imgui.h"

#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
using namespace std::chrono_literals;

namespace
{
/// \brief Application start time
auto const s_startTime = std::time (nullptr);

/// \brief Mutex for s_freeSpace
platform::Mutex s_lock;

/// \brief Free space string
std::string s_freeSpace;
}

///////////////////////////////////////////////////////////////////////////
FtpServer::~FtpServer ()
{
	m_quit = true;

	m_thread.join ();
}

FtpServer::FtpServer (std::uint16_t const port_)
    : m_log (Log::create ()), m_port (port_), m_quit (false)
{
	Log::bind (m_log);

	handleStartButton ();

	m_thread = platform::Thread (std::bind (&FtpServer::threadFunc, this));
}

void FtpServer::draw ()
{
	auto const &io    = ImGui::GetIO ();
	auto const width  = io.DisplaySize.x;
	auto const height = io.DisplaySize.y;

	ImGui::SetNextWindowPos (ImVec2 (0, 0), ImGuiCond_FirstUseEver);
#ifdef _3DS
	// top screen
	ImGui::SetNextWindowSize (ImVec2 (width, height / 2.0f));
#else
	ImGui::SetNextWindowSize (ImVec2 (width, height));
#endif
	ImGui::Begin (STATUS_STRING,
	    nullptr,
	    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

	{
		auto const lock = std::scoped_lock (m_lock);
		if (!m_socket)
		{
			if (ImGui::Button ("Start"))
				handleStartButton ();
		}
		else if (ImGui::Button ("Stop"))
			handleStopButton ();

		if (m_socket)
		{
			ImGui::SameLine ();
			ImGui::TextUnformatted (m_name.c_str ());
		}
	}

	{
		auto const lock = std::scoped_lock (s_lock);
		if (!s_freeSpace.empty ())
		{
			ImGui::SameLine ();
			ImGui::TextUnformatted (s_freeSpace.c_str ());
		}
	}

	ImGui::Separator ();

#ifdef _3DS
	// Fill rest of top screen window
	ImGui::BeginChild ("Logs", ImVec2 (0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
#else
	ImGui::BeginChild ("Logs", ImVec2 (0, 200), false, ImGuiWindowFlags_HorizontalScrollbar);
#endif
	m_log->draw ();
	ImGui::EndChild ();

#ifdef _3DS
	ImGui::End ();

	// bottom screen
	ImGui::SetNextWindowSize (ImVec2 (width * 0.8f, height / 2.0f));
	ImGui::SetNextWindowPos (ImVec2 (width * 0.1f, height / 2.0f), ImGuiCond_FirstUseEver);
	ImGui::Begin ("Sessions",
	    nullptr,
	    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
#else
	ImGui::Separator ();
#endif

	for (auto &session : m_sessions)
		session->draw ();

	ImGui::End ();
}

UniqueFtpServer FtpServer::create (std::uint16_t const port_)
{
	updateFreeSpace ();
	return UniqueFtpServer (new FtpServer (port_));
}

void FtpServer::updateFreeSpace ()
{
#if defined(_3DS) || defined(__SWITCH__)
	struct statvfs st;
	if (::statvfs ("sdmc:/", &st) != 0)
		return;

	auto const lock = std::scoped_lock (s_lock);
	s_freeSpace     = fs::printSize (static_cast<std::uint64_t> (st.f_bsize) * st.f_bfree);
#endif
}

std::time_t FtpServer::startTime ()
{
	return s_startTime;
}

void FtpServer::handleStartButton ()
{
	if (m_socket)
		return;

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
#if defined(_3DS) || defined(__SWITCH__)
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

	Log::info ("Started server at %s\n", m_name.c_str ());

	m_socket = std::move (socket);
}

void FtpServer::handleStopButton ()
{
	m_socket.reset ();
	Log::info ("Stopped server at %s\n", m_name.c_str ());
}

void FtpServer::loop ()
{
	{
		// poll listen socket
		auto const lock = std::scoped_lock (m_lock);
		if (m_socket)
		{
			Socket::PollInfo info{*m_socket, POLLIN, 0};
			if (Socket::poll (&info, 1, 0ms) > 0)
			{
				auto socket = m_socket->accept ();
				if (socket)
					m_sessions.emplace_back (FtpSession::create (std::move (socket)));
				else
					handleStopButton ();
			}
		}
	}

	// remove dead sessions
	for (auto it = std::begin (m_sessions); it != std::end (m_sessions);)
	{
		auto const &session = *it;
		if (session->dead ())
			it = m_sessions.erase (it);
		else
			++it;
	}

	// poll sessions
	if (!m_sessions.empty ())
	{
		if (!FtpSession::poll (m_sessions))
			handleStopButton ();
	}
	// avoid busy polling in background thread
	else
		platform::Thread::sleep (16ms);
}

void FtpServer::threadFunc ()
{
	// bind log for this thread
	Log::bind (m_log);

	while (!m_quit)
		loop ();
}
