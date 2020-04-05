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

#include "ftpSession.h"

#include "ftpServer.h"

#include "log.h"

#include "imgui.h"

#ifdef _3DS
#include <3ds.h>
#endif

#ifdef __SWITCH__
#include <switch.h>
#endif

#include <arpa/inet.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <mutex>
using namespace std::chrono_literals;

#if defined(_3DS) || defined(__SWITCH__)
#define lstat stat
#endif

namespace
{
std::pair<char *, char *> parseCommand (char *const buffer_, std::size_t const size_)
{
	auto const end = &buffer_[size_];
	for (auto p = buffer_; p < end; ++p)
	{
		if (p[0] == '\r' && p < end - 1 && p[1] == '\n')
			return {p, &p[2]};

		if (p[0] == '\n')
			return {p, &p[1]};
	}

	return {nullptr, nullptr};
}

void decodePath (char *const buffer_, std::size_t const size_)
{
	auto const end = &buffer_[size_];
	for (auto p = buffer_; p < end; ++p)
	{
		// this is an encoded \n
		if (*p == '\0')
			*p = '\n';
	}
}

std::string encodePath (std::string_view const buffer_, bool const quotes_ = false)
{
	// check if the buffer has \n
	bool const lf = std::memchr (buffer_.data (), '\n', buffer_.size ());

	auto end = std::end (buffer_);

	std::size_t numQuotes = 0;
	if (quotes_)
	{
		// check for \" that needs to be encoded
		auto p = buffer_.data ();
		do
		{
			p = static_cast<char const *> (std::memchr (p, '"', end - p));
			if (p)
			{
				++p;
				++numQuotes;
			}
		} while (p);
	}

	if (!lf && !numQuotes)
		return std::string (buffer_);

	std::string path (buffer_.size () + numQuotes, '\0');
	auto in  = buffer_.data ();
	auto out = path.data ();

	while (in < end)
	{
		if (*in == '\n')
		{
			// encoded \n is \0
			*out++ = '\0';
		}
		else if (quotes_ && *in == '"')
		{
			// encoded \" is \"\"
			*out++ = '"';
			*out++ = '"';
		}
		else
			*out++ = *in;
		++in;
	}

	return path;
}

std::string dirName (std::string_view const path_)
{
	// remove last path component
	auto const dir = std::string (path_.substr (0, path_.rfind ('/')));
	if (dir.empty ())
		return "/";

	return dir;
}

std::string resolvePath (std::string_view const path_)
{
	assert (!path_.empty ());
	assert (path_[0] == '/');

	// make sure parent is a directory
	struct stat st;
	if (::stat (dirName (path_).c_str (), &st) != 0)
		return {};

	if (!S_ISDIR (st.st_mode))
	{
		errno = ENOTDIR;
		return {};
	}

	// split path components
	std::vector<std::string_view> components;

	std::size_t pos = 1;
	auto next       = path_.find ('/', pos);
	while (next != std::string::npos)
	{
		if (next != pos)
			components.emplace_back (path_.substr (pos, next - pos));
		pos  = next + 1;
		next = path_.find ('/', pos);
	}

	if (pos != path_.size ())
		components.emplace_back (path_.substr (pos));

	// collapse . and ..
	auto it = std::begin (components);
	while (it != std::end (components))
	{
		if (*it == ".")
		{
			it = components.erase (it);
			continue;
		}

		if (*it == "..")
		{
			if (it != std::begin (components))
				it = components.erase (std::prev (it));
			it = components.erase (it);
			continue;
		}

		++it;
	}

	// join path components
	std::string outPath = "/";
	for (auto const &component : components)
	{
		outPath += component;
		outPath.push_back ('/');
	}

	if (outPath.size () > 1)
		outPath.pop_back ();

	return outPath;
}

std::string buildPath (std::string_view const cwd_, std::string_view const args_)
{
	// absolute path
	if (args_[0] == '/')
		return std::string (args_);

	// root directory
	if (cwd_.size () == 1)
		return std::string (cwd_) + std::string (args_);

	return std::string (cwd_) + '/' + std::string (args_);
}

std::string buildResolvedPath (std::string_view const cwd_, std::string_view const args_)
{
	return resolvePath (buildPath (cwd_, args_));
}
}

///////////////////////////////////////////////////////////////////////////
FtpSession::~FtpSession ()
{
	m_commandSocket.reset ();
	m_pasvSocket.reset ();
	closeData ();
}

FtpSession::FtpSession (UniqueSocket commandSocket_)
    : m_commandSocket (std::move (commandSocket_)),
      m_commandBuffer (COMMAND_BUFFERSIZE),
      m_responseBuffer (RESPONSE_BUFFERSIZE),
      m_xferBuffer (XFER_BUFFERSIZE),
      m_pasv (false),
      m_port (false),
      m_recv (false),
      m_send (false),
      m_urgent (false),
      m_mlstType (true),
      m_mlstSize (true),
      m_mlstModify (true),
      m_mlstPerm (true),
      m_mlstUnixMode (false)
{
	char buffer[32];
	std::sprintf (buffer, "Session#%p", this);
	m_windowName = buffer;

	std::sprintf (buffer, "Plot#%p", this);
	m_plotName = buffer;

	m_commandSocket->setNonBlocking ();

	auto const lock = std::scoped_lock (m_lock);
	sendResponse ("220 Hello!\r\n");
}

bool FtpSession::dead ()
{
	auto const lock = std::scoped_lock (m_lock);
	if (m_commandSocket || m_pasvSocket || m_dataSocket)
		return false;

	return true;
}

void FtpSession::draw ()
{
	auto const lock = std::scoped_lock (m_lock);

	ImGuiIO &io      = ImGui::GetIO ();
	auto const scale = io.DisplayFramebufferScale.y;

	ImGui::BeginChild (m_windowName.c_str (), ImVec2 (0.0f, 50.0f / scale), true);

	if (!m_workItem.empty ())
		ImGui::TextUnformatted (m_workItem.c_str ());
	else
		ImGui::TextUnformatted (m_cwd.c_str ());

	if (m_fileSize)
		ImGui::Text (
		    "%s/%s", fs::printSize (m_filePosition).c_str (), fs::printSize (m_fileSize).c_str ());
	else if (m_filePosition)
		ImGui::Text ("%s/???", fs::printSize (m_filePosition).c_str ());

	if (m_fileSize || m_filePosition)
	{
		// MiB/s plot lines
		for (std::size_t i = 0; i < POSITION_HISTORY - 1; ++i)
		{
			m_filePositionDeltas[i]  = m_filePositionHistory[i + 1] - m_filePositionHistory[i];
			m_filePositionHistory[i] = m_filePositionHistory[i + 1];
		}

		auto const diff = m_filePosition - m_filePositionHistory[POSITION_HISTORY - 1];
		m_filePositionDeltas[POSITION_HISTORY - 1]  = diff;
		m_filePositionHistory[POSITION_HISTORY - 1] = m_filePosition;

		if (m_xferRate == -1.0f)
		{
			m_xferRate         = 0.0f;
			m_filePositionTime = platform::steady_clock::now ();
		}
		else
		{
			auto const now      = platform::steady_clock::now ();
			auto const timeDiff = now - m_filePositionTime;
			m_filePositionTime  = now;

			auto const rate  = diff / std::chrono::duration<float> (timeDiff).count ();
			auto const alpha = 0.01f;
			m_xferRate       = alpha * rate + (1.0f - alpha) * m_xferRate;
		}

		auto const rateString = fs::printSize (m_xferRate) + "/s";

		ImGui::SameLine ();
		ImGui::PlotLines ("Rate",
		    m_filePositionDeltas,
		    IM_ARRAYSIZE (m_filePositionDeltas),
		    0,
		    rateString.c_str ());
	}

	ImGui::EndChild ();
}

UniqueFtpSession FtpSession::create (UniqueSocket commandSocket_)
{
	return UniqueFtpSession (new FtpSession (std::move (commandSocket_)));
}

void FtpSession::poll (std::vector<UniqueFtpSession> const &sessions_)
{
#if 0
	auto const printEvents = [] (int const events_) {
		std::string out;
		if (events_ & POLLIN)
			out += "[IN]";
		if (events_ & POLLPRI)
			out += "[PRI]";
		if (events_ & POLLOUT)
			out += "[OUT]";
		if (events_ & POLLHUP)
			out += "[HUP]";
		if (events_ & POLLERR)
			out += "[ERR]";

		return out;
	};
#endif

	// poll for pending close sockets first
	std::vector<Socket::PollInfo> info;
	for (auto &session : sessions_)
	{
		auto const lock = std::scoped_lock (session->m_lock);
		for (auto &pending : session->m_pendingCloseSocket)
		{
			assert (pending.unique ());
			info.emplace_back (Socket::PollInfo{*pending, POLLIN, 0});
		}
	}

	if (!info.empty ())
	{
		auto const rc = Socket::poll (info.data (), info.size (), 0ms);
		if (rc < 0)
			Log::error ("poll: %s\n", std::strerror (errno));
		else
		{
			for (auto const &i : info)
			{
				if (!i.revents)
					continue;

				for (auto &session : sessions_)
				{
					auto const lock = std::scoped_lock (session->m_lock);
					for (auto it = std::begin (session->m_pendingCloseSocket);
					     it != std::end (session->m_pendingCloseSocket);)
					{
						auto &socket = *it;
						if (&i.socket.get () != socket.get ())
						{
							++it;
							continue;
						}

						it = session->m_pendingCloseSocket.erase (it);
					}
				}
			}
		}
	}

	// poll for everything else
	info.clear ();
	for (auto &session : sessions_)
	{
		auto const lock = std::scoped_lock (session->m_lock);
		if (session->m_commandSocket)
		{
			info.emplace_back (Socket::PollInfo{*session->m_commandSocket, POLLIN | POLLPRI, 0});
			if (session->m_responseBuffer.usedSize () != 0)
				info.back ().events |= POLLOUT;
		}

		switch (session->m_state)
		{
		case State::COMMAND:
			// we are waiting to read a command
			break;

		case State::DATA_CONNECT:
			if (session->m_pasv)
			{
				assert (!session->m_port);
				// we are waiting for a PASV connection
				info.emplace_back (Socket::PollInfo{*session->m_pasvSocket, POLLIN, 0});
			}
			else
			{
				// we are waiting to complete a PORT connection
				info.emplace_back (Socket::PollInfo{*session->m_dataSocket, POLLOUT, 0});
			}
			break;

		case State::DATA_TRANSFER:
			// we need to transfer data
			if (session->m_recv)
			{
				assert (!session->m_send);
				info.emplace_back (Socket::PollInfo{*session->m_dataSocket, POLLIN, 0});
			}
			else
			{
				assert (session->m_send);
				info.emplace_back (Socket::PollInfo{*session->m_dataSocket, POLLOUT, 0});
			}
			break;
		}
	}

	if (info.empty ())
		return;

		// poll for activity
#if MULTITHREADED
	auto const rc = Socket::poll (info.data (), info.size (), 16ms);
#else
	auto const rc = Socket::poll (info.data (), info.size (), 0ms);
#endif
	if (rc < 0)
	{
		Log::error ("poll: %s\n", std::strerror (errno));
		return;
	}

	if (rc == 0)
		return;

	for (auto &session : sessions_)
	{
		auto const lock = std::scoped_lock (session->m_lock);

		for (auto const &i : info)
		{
			if (!i.revents)
				continue;

			// check command socket
			if (&i.socket.get () == session->m_commandSocket.get ())
			{
				if (i.revents & ~(POLLIN | POLLPRI | POLLOUT))
					Log::debug ("Command revents 0x%X\n", i.revents);

				if (i.revents & POLLOUT)
					session->writeResponse ();

				if (i.revents & (POLLIN | POLLPRI))
					session->readCommand (i.revents);

				if (i.revents & (POLLERR | POLLHUP))
					session->m_commandSocket.reset ();
			}

			// check the data socket
			if (&i.socket.get () == session->m_pasvSocket.get () ||
			    &i.socket.get () == session->m_dataSocket.get ())
			{
				switch (session->m_state)
				{
				case State::COMMAND:
					assert (false);
					break;

				case State::DATA_CONNECT:
					if (i.revents & ~(POLLIN | POLLPRI | POLLOUT))
						Log::debug ("Data revents 0x%X\n", i.revents);

					if (i.revents & (POLLERR | POLLHUP))
					{
						session->sendResponse ("426 Data connection failed\r\n");
						session->setState (State::COMMAND, true, true);
					}
					else if (i.revents & POLLIN)
					{
						// we need to accept the PASV connection
						session->dataAccept ();
					}
					else if (i.revents & POLLOUT)
					{
						// PORT connection completed
						auto const &sockName = session->m_dataSocket->peerName ();
						Log::info ("Connected to [%s]:%u\n", sockName.name (), sockName.port ());

						session->sendResponse ("150 Ready\r\n");
						session->setState (State::DATA_TRANSFER, true, false);
					}
					break;

				case State::DATA_TRANSFER:
					if (i.revents & ~(POLLIN | POLLPRI | POLLOUT))
						Log::debug ("Data revents 0x%X\n", i.revents);

					// we need to transfer data
					if (i.revents & (POLLERR | POLLHUP))
					{
						session->sendResponse ("426 Data connection failed\r\n");
						session->setState (State::COMMAND, true, true);
					}
					else if (i.revents & (POLLIN | POLLOUT))
					{
						while (((*session).*(session->m_transfer)) ())
							;
					}
				}
			}
		}
	}
}

void FtpSession::setState (State const state_, bool const closePasv_, bool const closeData_)
{
	m_state = state_;

	if (closePasv_)
		m_pasvSocket.reset ();
	if (closeData_)
		closeData ();

	if (state_ == State::COMMAND)
	{
		m_restartPosition = 0;
		m_fileSize        = 0;
		m_filePosition    = 0;

		for (auto &pos : m_filePositionHistory)
			pos = 0;
		m_xferRate = -1.0f;

		m_workItem.clear ();

		m_file.close ();
		m_dir.close ();
	}
}

void FtpSession::closeData ()
{
	if (m_dataSocket && m_dataSocket.unique ())
	{
		m_dataSocket->shutdown (SHUT_WR);
		m_dataSocket->setLinger (true, 0s);
		m_pendingCloseSocket.emplace_back (std::move (m_dataSocket));
	}
	m_dataSocket.reset ();

	m_recv = false;
	m_send = false;
}

bool FtpSession::changeDir (char const *const args_)
{
	if (std::strcmp (args_, "..") == 0)
	{
		// cd up
		auto const pos = m_cwd.find_last_of ('/');
		assert (pos != std::string::npos);
		if (pos == 0)
			m_cwd = "/";
		else
			m_cwd = m_cwd.substr (0, pos);
		return true;
	}

	auto const path = buildResolvedPath (m_cwd, args_);
	if (path.empty ())
		return false;

	struct stat st;
	if (::stat (path.c_str (), &st) != 0)
		return false;

	if (!S_ISDIR (st.st_mode))
	{
		errno = ENOTDIR;
		return false;
	}

	m_cwd = path;
	return true;
}

bool FtpSession::dataAccept ()
{
	if (!m_pasv)
	{
		sendResponse ("503 Bad sequence of commands\r\n");
		setState (State::COMMAND, true, true);
		return false;
	}

	m_pasv = false;

	m_dataSocket = m_pasvSocket->accept ();
	if (!m_dataSocket)
	{
		sendResponse ("425 Failed to establish connection\r\n");
		setState (State::COMMAND, true, true);
		return false;
	}

#ifndef _3DS
	m_dataSocket->setRecvBufferSize (SOCK_BUFFERSIZE);
	m_dataSocket->setSendBufferSize (SOCK_BUFFERSIZE);
#endif

	if (!m_dataSocket->setNonBlocking ())
	{
		sendResponse ("425 Failed to establish connection\r\n");
		setState (State::COMMAND, true, true);
		return false;
	}

	// we are ready to transfer data
	sendResponse ("150 Ready\r\n");
	setState (State::DATA_TRANSFER, true, false);
	return true;
}

bool FtpSession::dataConnect ()
{
	assert (m_port);

	m_port = false;

	m_dataSocket = Socket::create ();
	if (!m_dataSocket)
		return false;

	m_dataSocket->setRecvBufferSize (SOCK_BUFFERSIZE);
	m_dataSocket->setSendBufferSize (SOCK_BUFFERSIZE);

	if (!m_dataSocket->setNonBlocking ())
		return false;

	if (!m_dataSocket->connect (m_portAddr))
	{
		if (errno != EINPROGRESS)
		{
			m_dataSocket.reset ();
			return false;
		}

		return true;
	}

	// we are ready to transfer data
	sendResponse ("150 Ready\r\n");
	setState (State::DATA_TRANSFER, true, false);
	return true;
}

int FtpSession::fillDirent (struct stat const &st_, std::string_view const path_, char const *type_)
{
	auto const buffer = m_xferBuffer.freeArea ();
	auto const size   = m_xferBuffer.freeSize ();

	std::size_t pos = 0;

	if (m_xferDirMode == XferDirMode::MLSD || m_xferDirMode == XferDirMode::MLST)
	{
		if (m_xferDirMode == XferDirMode::MLST)
		{
			if (pos >= size)
				return EAGAIN;
			buffer[pos++] = ' ';
		}

		// type fact
		if (m_mlstType)
		{
			if (!type_)
			{
				type_ = "???";
				if (S_ISREG (st_.st_mode))
					type_ = "file";
				else if (S_ISDIR (st_.st_mode))
					type_ = "dir";
#if !defined(_3DS) && !defined(__SWITCH__)
				else if (S_ISLNK (st_.st_mode))
					type_ = "os.unix=symlink";
				else if (S_ISCHR (st_.st_mode))
					type_ = "os.unix=character";
				else if (S_ISBLK (st_.st_mode))
					type_ = "os.unix=block";
				else if (S_ISFIFO (st_.st_mode))
					type_ = "os.unix=fifo";
				else if (S_ISSOCK (st_.st_mode))
					type_ = "os.unix=socket";
#endif
			}

			auto const rc = std::snprintf (&buffer[pos], size - pos, "Type=%s;", type_);
			if (rc < 0)
				return errno;
			if (static_cast<std::size_t> (rc) > size - pos)
				return EAGAIN;

			pos += rc;
		}

		// size fact
		if (m_mlstSize)
		{
			auto const rc = std::snprintf (&buffer[pos],
			    size - pos,
			    "Size=%llu;",
			    static_cast<unsigned long long> (st_.st_size));
			if (rc < 0)
				return errno;
			if (static_cast<std::size_t> (rc) > size - pos)
				return EAGAIN;

			pos += rc;
		}

		// mtime fact
		if (m_mlstModify)
		{
			auto const tm = std::gmtime (&st_.st_mtime);
			if (!tm)
				return errno;

			auto const rc = std::strftime (&buffer[pos], size - pos, "Modify=%Y%m%d%H%M%S;", tm);
			if (rc == 0)
				return EAGAIN;

			pos += rc;
		}

		// permission fact
		if (m_mlstPerm)
		{
			auto const header = "Perm=";
			if (size - pos < std::strlen (header))
				return EAGAIN;

			std::strcpy (&buffer[pos], header);
			pos += std::strlen (header);

			// append permission
			if (S_ISREG (st_.st_mode) && (st_.st_mode & S_IWUSR))
			{
				if (pos >= size)
					return EAGAIN;
				buffer[pos++] = 'a';
			}

			// create permission
			if (S_ISDIR (st_.st_mode) && (st_.st_mode & S_IWUSR))
			{
				if (pos >= size)
					return EAGAIN;
				buffer[pos++] = 'c';
			}

			// delete permission
			if (pos >= size)
				return EAGAIN;
			buffer[pos++] = 'd';

			// chdir permission
			if (S_ISDIR (st_.st_mode) && (st_.st_mode & S_IXUSR))
			{
				if (pos >= size)
					return EAGAIN;
				buffer[pos++] = 'e';
			}

			// rename permission
			if (pos >= size)
				return EAGAIN;
			buffer[pos++] = 'f';

			// list permission
			if (S_ISDIR (st_.st_mode) && (st_.st_mode & S_IRUSR))
			{
				if (pos >= size)
					return EAGAIN;
				buffer[pos++] = 'l';
			}

			// mkdir permission
			if (S_ISDIR (st_.st_mode) && (st_.st_mode & S_IWUSR))
			{
				if (pos >= size)
					return EAGAIN;
				buffer[pos++] = 'm';
			}

			// purge permission
			if (S_ISDIR (st_.st_mode) && (st_.st_mode & S_IWUSR))
			{
				if (pos >= size)
					return EAGAIN;
				buffer[pos++] = 'p';
			}

			// read permission
			if (S_ISREG (st_.st_mode) && (st_.st_mode & S_IRUSR))
			{
				if (pos >= size)
					return EAGAIN;
				buffer[pos++] = 'r';
			}

			// write permission
			if (S_ISREG (st_.st_mode) && (st_.st_mode & S_IWUSR))
			{
				if (pos >= size)
					return EAGAIN;
				buffer[pos++] = 'w';
			}

			if (pos >= size)
				return EAGAIN;
			buffer[pos++] = ';';
		}

		// unix mode fact
		if (m_mlstUnixMode)
		{
			auto const mask = S_IRWXU | S_IRWXG | S_IRWXO | S_ISVTX | S_ISGID | S_ISUID;

			auto const rc = std::snprintf (&buffer[pos],
			    size - pos,
			    "UNIX.mode=0%lo;",
			    static_cast<unsigned long> (st_.st_mode & mask));
			if (rc < 0)
				return errno;
			if (static_cast<std::size_t> (rc) > size - pos)
				return EAGAIN;

			pos += rc;
		}

		// make sure space precedes name
		if (buffer[pos - 1] != ' ')
		{
			if (pos >= size)
				return EAGAIN;

			buffer[pos++] = ' ';
		}
	}
	else if (m_xferDirMode != XferDirMode::NLST)
	{
		if (m_xferDirMode == XferDirMode::STAT)
		{
			if (pos >= size)
				return EAGAIN;

			buffer[pos++] = ' ';
		}

#ifdef _3DS
		auto const owner = "3DS";
		auto const group = "3DS";
#elif defined(__SWITCH__)
		auto const owner = "Switch";
		auto const group = "Switch";
#else
		char owner[32];
		char group[32];
		std::sprintf (owner, "%d", st_.st_uid);
		std::sprintf (group, "%d", st_.st_gid);
#endif
		// perms nlinks owner group size
		auto rc = std::snprintf (&buffer[pos],
		    size - pos,
		    "%c%c%c%c%c%c%c%c%c%c %lu %s %s %llu ",
		    // clang-format off
		    S_ISREG (st_.st_mode)  ? '-' :
		    S_ISDIR (st_.st_mode)  ? 'd' :
#if !defined(_3DS) && !defined(__SWITCH__)
		    S_ISLNK (st_.st_mode)  ? 'l' :
		    S_ISCHR (st_.st_mode)  ? 'c' :
		    S_ISBLK (st_.st_mode)  ? 'b' :
		    S_ISFIFO (st_.st_mode) ? 'p' :
		    S_ISSOCK (st_.st_mode) ? 's' :
#endif
		    '?',
		    // clang-format on
		    st_.st_mode & S_IRUSR ? 'r' : '-',
		    st_.st_mode & S_IWUSR ? 'w' : '-',
		    st_.st_mode & S_IXUSR ? 'x' : '-',
		    st_.st_mode & S_IRGRP ? 'r' : '-',
		    st_.st_mode & S_IWGRP ? 'w' : '-',
		    st_.st_mode & S_IXGRP ? 'x' : '-',
		    st_.st_mode & S_IROTH ? 'r' : '-',
		    st_.st_mode & S_IWOTH ? 'w' : '-',
		    st_.st_mode & S_IXOTH ? 'x' : '-',
		    static_cast<unsigned long> (st_.st_nlink),
		    owner,
		    group,
		    static_cast<unsigned long long> (st_.st_size));
		if (rc < 0)
			return errno;

		if (static_cast<std::size_t> (rc) > size - pos)
			return EAGAIN;

		pos += rc;

		// timestamp
		auto const tm = std::gmtime (&st_.st_mtime);
		if (!tm)
			return errno;

		auto fmt = "%b %e %H:%M ";
		rc       = std::strftime (&buffer[pos], size - pos, fmt, tm);
		if (rc < 0)
			return errno;
		if (static_cast<std::size_t> (rc) > size - pos)
			return EAGAIN;

		pos += rc;
	}

	if (size - pos < path_.size () + 2)
		return EAGAIN;

	// path
	std::memcpy (&buffer[pos], path_.data (), path_.size ());
	pos += path_.size ();
	buffer[pos++] = '\r';
	buffer[pos++] = '\n';

	m_xferBuffer.markUsed (pos);

	return 0;
}

int FtpSession::fillDirent (std::string const &path_, char const *type_)
{
	struct stat st;
	if (::stat (path_.c_str (), &st) != 0)
		return errno;

	return fillDirent (st, encodePath (path_), type_);
}

void FtpSession::xferFile (char const *const args_, XferFileMode const mode_)
{
	m_xferBuffer.clear ();

	// build the path of the file to transfer
	auto const path = buildResolvedPath (m_cwd, args_);
	if (path.empty ())
	{
		sendResponse ("553 %s\r\n", std::strerror (errno));
		setState (State::COMMAND, true, true);
		return;
	}

	if (mode_ == XferFileMode::RETR)
	{
		// stat the file
		struct stat st;
		if (::stat (path.c_str (), &st) != 0)
		{
			sendResponse ("450 %s\r\n", std::strerror (errno));
			return;
		}

		// open the file in read mode
		if (!m_file.open (path.c_str (), "rb"))
		{
			sendResponse ("450 %s\r\n", std::strerror (errno));
			return;
		}

		m_fileSize = st.st_size;

		m_file.setBufferSize (FILE_BUFFERSIZE);

		if (m_restartPosition != 0)
		{
			if (m_file.seek (m_restartPosition, SEEK_SET) != 0)
			{
				sendResponse ("450 %s\r\n", std::strerror (errno));
				return;
			}
		}

		m_filePosition = m_restartPosition;
	}
	else
	{
		auto const append = mode_ == XferFileMode::APPE;

		char const *mode = "wb";
		if (append)
			mode = "ab";
		else if (m_restartPosition != 0)
			mode = "r+b";

		// open file in write mode
		if (!m_file.open (path.c_str (), mode))
		{
			sendResponse ("450 %s\r\n", std::strerror (errno));
			return;
		}

		FtpServer::updateFreeSpace ();

		m_file.setBufferSize (FILE_BUFFERSIZE);

		// check if this had REST but not APPE
		if (m_restartPosition != 0 && !append)
		{
			// seek to the REST offset
			if (m_file.seek (m_restartPosition, SEEK_SET) != 0)
			{
				sendResponse ("450 %s\r\n", std::strerror (errno));
				return;
			}
		}

		m_filePosition = m_restartPosition;
	}

	if (!m_port && !m_pasv)
	{
		sendResponse ("503 Bad sequence of commands\r\n");
		setState (State::COMMAND, true, true);
		return;
	}

	setState (State::DATA_CONNECT, false, true);

	// setup connection
	if (m_port && !dataConnect ())
	{
		sendResponse ("425 Can't open data connection\r\n");
		setState (State::COMMAND, true, true);
		return;
	}

	// set up the transfer
	if (mode_ == XferFileMode::RETR)
	{
		m_recv     = false;
		m_send     = true;
		m_transfer = &FtpSession::retrieveTransfer;
	}
	else
	{
		m_recv     = true;
		m_send     = false;
		m_transfer = &FtpSession::storeTransfer;
	}

	m_xferBuffer.clear ();

	m_workItem = path;
}

void FtpSession::xferDir (char const *const args_, XferDirMode const mode_, bool const workaround_)
{
	// set up the transfer
	m_xferDirMode = mode_;
	m_recv        = false;
	m_send        = true;

	m_xferBuffer.clear ();

	m_transfer = &FtpSession::listTransfer;

	if (std::strlen (args_) > 0)
	{
		// an argument was provided
		auto const path = buildResolvedPath (m_cwd, args_);
		if (path.empty ())
		{
			sendResponse ("550 %s\r\n", std::strerror (errno));
			setState (State::COMMAND, true, true);
			return;
		}

		struct stat st;
		if (::stat (path.c_str (), &st) != 0)
		{
			auto const rc = errno;

			// work around broken clients that think LIST -a/-l is valid
			if (workaround_ && mode_ == XferDirMode::LIST)
			{
				if (args_[0] == '-' && (args_[1] == 'a' || args_[1] == 'l'))
				{
					char const *args = &args_[2];
					if (*args == '\0' || *args == ' ')
					{
						if (*args == ' ')
							++args;

						xferDir (args, mode_, false);
						return;
					}
				}
			}

			sendResponse ("550 %s\r\n", std::strerror (rc));
			setState (State::COMMAND, true, true);
			return;
		}

		if (S_ISDIR (st.st_mode))
		{
			if (!m_dir.open (path.c_str ()))
			{
				sendResponse ("550 %s\r\n", std::strerror (errno));
				setState (State::COMMAND, true, true);
				return;
			}

			// set as lwd
			m_lwd = std::move (path);

			if (mode_ == XferDirMode::MLSD && m_mlstType)
			{
				// send this directory as type=cdir
				auto const rc = fillDirent (m_lwd, "cdir");
				if (rc != 0)
				{
					sendResponse ("550 %s\r\n", std::strerror (rc));
					setState (State::COMMAND, true, true);
					return;
				}
			}

			m_workItem = m_lwd;
		}
		else if (mode_ == XferDirMode::MLSD)
		{
			// specified file instead of directory for MLSD
			sendResponse ("501 %s\r\n", std::strerror (ENOTDIR));
			setState (State::COMMAND, true, true);
			return;
		}
		else
		{
			std::string name;
			if (mode_ == XferDirMode::NLST)
			{
				// NLST uses full path name
				name = encodePath (path);
			}
			else
			{
				// everything else uses basename
				auto const pos = path.find_last_of ('/');
				assert (pos != std::string::npos);
				name = encodePath (std::string_view (path).substr (pos));
			}

			auto const rc = fillDirent (st, name);
			if (rc != 0)
			{
				sendResponse ("550 %s\r\n", std::strerror (rc));
				setState (State::COMMAND, true, true);
				return;
			}

			m_workItem = path;
		}
	}
	else if (!m_dir.open (m_cwd.c_str ()))
	{
		// no argument, but opening cwd failed
		sendResponse ("550 %s\r\n", std::strerror (errno));
		setState (State::COMMAND, true, true);
		return;
	}
	else
	{
		// set the cwd as the lwd
		m_lwd = m_cwd;

		if (mode_ == XferDirMode::MLSD && m_mlstType)
		{
			// send this directory as type=cdir
			auto const rc = fillDirent (m_lwd, "cdir");
			if (rc != 0)
			{
				sendResponse ("550 %s\r\n", std::strerror (rc));
				setState (State::COMMAND, true, true);
				return;
			}
		}

		m_workItem = m_lwd;
	}

	if (mode_ == XferDirMode::MLST || mode_ == XferDirMode::STAT)
	{
		// this is a little different; we have to send the data over the command socket
		sendResponse ("213-Status\r\n");
		setState (State::DATA_TRANSFER, true, true);
		m_dataSocket = m_commandSocket;
		m_send       = true;
		return;
	}

	if (!m_port && !m_pasv)
	{
		// Prior PORT or PASV required
		sendResponse ("503 Bad sequence of commands\r\n");
		setState (State::COMMAND, true, true);
		return;
	}

	setState (State::DATA_CONNECT, false, true);
	m_send = true;

	// setup connection
	if (m_port && !dataConnect ())
	{
		sendResponse ("425 Can't open data connection\r\n");
		setState (State::COMMAND, true, true);
	}
}

void FtpSession::readCommand (int const events_)
{
	// check out-of-band data
	if (events_ & POLLPRI)
	{
		m_urgent = true;

		// check if we are at the urgent marker
		auto const atMark = m_commandSocket->atMark ();
		if (atMark < 0)
		{
			m_commandSocket.reset ();
			return;
		}

		if (!atMark)
		{
			// discard in-band data
			m_commandBuffer.clear ();
			m_lock.unlock ();
			auto const rc = m_commandSocket->read (m_commandBuffer);
			m_lock.lock ();
			if (rc < 0 && errno != EWOULDBLOCK)
				m_commandSocket.reset ();

			return;
		}

		// retrieve the urgent data
		m_commandBuffer.clear ();
		m_lock.unlock ();
		auto const rc = m_commandSocket->read (m_commandBuffer, true);
		m_lock.lock ();
		if (rc < 0)
		{
			// EWOULDBLOCK means out-of-band data is on the way
			if (errno != EWOULDBLOCK)
				m_commandSocket.reset ();
			return;
		}

		// reset the command buffer
		m_commandBuffer.clear ();
		return;
	}

	if (events_ & POLLIN)
	{
		// prepare to receive data
		if (m_commandBuffer.freeSize () == 0)
		{
			Log::error ("Exceeded command buffer size\n");
			m_commandSocket.reset ();
			return;
		}

		m_lock.unlock ();
		auto const rc = m_commandSocket->read (m_commandBuffer);
		m_lock.lock ();
		if (rc < 0)
		{
			m_commandSocket.reset ();
			return;
		}

		if (rc == 0)
		{
			// peer closed connection
			Log::info ("Peer closed connection\n");
			m_commandSocket.reset ();
			return;
		}

		if (m_urgent)
		{
			// look for telnet data mark
			auto const buffer = m_commandBuffer.usedArea ();
			auto const size   = m_commandBuffer.usedSize ();
			auto const mark   = static_cast<char const *> (std::memchr (buffer, 0xF2, size));
			if (!mark)
				return;

			// ignore all data that precedes the data mark
			m_commandBuffer.markFree (mark + 1 - buffer);
			m_commandBuffer.coalesce ();
			m_urgent = false;
		}
	}

	// loop through commands
	while (true)
	{
		// must have at least enough data for the delimiter
		auto const size = m_commandBuffer.usedSize ();
		if (size < 1)
			return;

		auto const buffer        = m_commandBuffer.usedArea ();
		auto const [delim, next] = parseCommand (buffer, size);
		if (!next)
			return;

		*delim = '\0';
		decodePath (buffer, delim - buffer);
		Log::command ("%s\n", buffer);

		char const *const command = buffer;

		char *args = buffer;
		while (*args && !std::isspace (*args))
			++args;
		if (*args)
			*args++ = 0;

		auto const it = std::lower_bound (std::begin (handlers),
		    std::end (handlers),
		    command,
		    [] (auto const &lhs_, auto const &rhs_) {
			    return ::strcasecmp (lhs_.first.data (), rhs_) < 0;
		    });

		if (it == std::end (handlers) || ::strcasecmp (it->first.data (), command) != 0)
		{
			std::string response = "502 Invalid command \"";
			response += encodePath (command);

			if (*args)
			{
				response.push_back (' ');
				response += encodePath (args);
			}

			response += "\"\r\n";

			sendResponse (response);
		}
		else if (m_state != State::COMMAND)
		{
			// only some commands are available during data transfer
			if (::strcasecmp (command, "ABOR") != 0 && ::strcasecmp (command, "STAT") != 0 &&
			    ::strcasecmp (command, "QUIT") != 0)
			{
				sendResponse ("503 Invalid command during transfer\r\n");
				setState (State::COMMAND, true, true);
				m_commandSocket.reset ();
			}
			else
			{
				auto const handler = it->second;
				(this->*handler) (args);
			}
		}
		else
		{
			// clear rename for all commands except RNTO
			if (::strcasecmp (command, "RNTO") != 0)
				m_rename.clear ();

			auto const handler = it->second;
			(this->*handler) (args);
		}

		m_commandBuffer.markFree (next - buffer);
		m_commandBuffer.coalesce ();
	}
}

void FtpSession::writeResponse ()
{
	m_lock.unlock ();
	auto const rc = m_commandSocket->write (m_responseBuffer);
	m_lock.lock ();
	if (rc <= 0)
	{
		m_commandSocket.reset ();
		return;
	}

	m_responseBuffer.coalesce ();
}

void FtpSession::sendResponse (char const *fmt_, ...)
{
	if (!m_commandSocket)
		return;

	auto const buffer = m_responseBuffer.freeArea ();
	auto const size   = m_responseBuffer.freeSize ();

	va_list ap;

	va_start (ap, fmt_);
	Log::log (Log::RESPONSE, fmt_, ap);
	va_end (ap);

	va_start (ap, fmt_);
	auto const rc = std::vsnprintf (buffer, size, fmt_, ap);
	va_end (ap);

	if (rc < 0)
	{
		Log::error ("vsnprintf: %s\n", std::strerror (errno));
		m_commandSocket.reset ();
		return;
	}

	if (static_cast<std::size_t> (rc) > size)
	{
		Log::error ("Not enough space for response\n");
		m_commandSocket.reset ();
		return;
	}

	m_responseBuffer.markUsed (rc);

	// try to write data immediately
	assert (m_commandSocket);
	m_lock.unlock ();
	auto const bytes =
	    m_commandSocket->write (m_responseBuffer.usedArea (), m_responseBuffer.usedSize ());
	m_lock.lock ();
	if (bytes < 0 && errno != EWOULDBLOCK)
		m_commandSocket.reset ();
	else if (bytes > 0)
	{
		m_responseBuffer.markFree (bytes);
		m_responseBuffer.coalesce ();
	}
}

void FtpSession::sendResponse (std::string_view const response_)
{
	if (!m_commandSocket)
		return;

	Log::log (Log::RESPONSE, response_);

	auto const buffer = m_responseBuffer.freeArea ();
	auto const size   = m_responseBuffer.freeSize ();

	if (response_.size () > size)
	{
		Log::error ("Not enough space for response\n");
		m_commandSocket.reset ();
		return;
	}

	std::memcpy (buffer, response_.data (), response_.size ());
	m_responseBuffer.markUsed (response_.size ());
}

bool FtpSession::listTransfer ()
{
	// check if we sent all available data
	if (m_xferBuffer.empty ())
	{
		m_xferBuffer.clear ();

		// check xfer dir type
		int rc = 226;
		if (m_xferDirMode == XferDirMode::STAT)
			rc = 213;

		// check if this was for a file
		if (!m_dir)
		{
			// we already sent the file's listing
			sendResponse ("%d OK\r\n", rc);
			setState (State::COMMAND, true, true);
			return false;
		}

		// get the next directory entry
		m_lock.unlock ();
		auto const dent = m_dir.read ();
		m_lock.lock ();
		if (!dent)
		{
			// we have exhausted the directory listing
			sendResponse ("%d OK\r\n", rc);
			setState (State::COMMAND, true, true);
			return false;
		}

		// I think we are supposed to return entries for . and ..
		if (std::strcmp (dent->d_name, ".") == 0 || std::strcmp (dent->d_name, "..") == 0)
			return true;

		// check if this was NLST
		if (m_xferDirMode == XferDirMode::NLST)
		{
			// NLST gives the whole path name
			auto const path = encodePath (buildPath (m_lwd, dent->d_name));
			if (m_xferBuffer.freeSize () < path.size ())
			{
				sendResponse ("501 %s\r\n", std::strerror (ENOMEM));
				setState (State::COMMAND, true, true);
				return false;
			}
		}
		else
		{
			// build the path
			auto const fullPath = buildPath (m_lwd, dent->d_name);
			struct stat st;

#ifdef _3DS
			// the sdmc directory entry already has the type and size, so no need to do a slow stat
			auto const dp    = static_cast<DIR *> (m_dir);
			auto const magic = *reinterpret_cast<u32 *> (dp->dirData->dirStruct);

			if (magic == SDMC_DIRITER_MAGIC)
			{
				auto const dir   = reinterpret_cast<sdmc_dir_t const *> (dp->dirData->dirStruct);
				auto const entry = &dir->entry_data[dir->index];

				if (entry->attributes & FS_ATTRIBUTE_DIRECTORY)
					st.st_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH;
				else
					st.st_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH;

				if (!(entry->attributes & FS_ATTRIBUTE_READ_ONLY))
					st.st_mode |= S_IWUSR | S_IWGRP | S_IWOTH;

				st.st_size  = entry->fileSize;
				st.st_mtime = 0;

				bool getmtime = true;
				if (m_xferDirMode == XferDirMode::MLSD || m_xferDirMode == XferDirMode::MLST)
				{
					if (!m_mlstModify)
						getmtime = false;
				}
				else if (m_xferDirMode == XferDirMode::NLST)
					getmtime = false;

				if (getmtime)
				{
					std::uint64_t mtime = 0;
					auto const rc       = sdmc_getmtime (fullPath.c_str (), &mtime);
					if (rc != 0)
						Log::error ("sdmc_getmtime %s 0x%lx\n", fullPath.c_str (), rc);
					else
						st.st_mtime = mtime;
				}
			}
			else
#endif
			    // lstat the entry
			    if (::lstat (fullPath.c_str (), &st) != 0)
			{
				sendResponse ("550 %s\r\n", std::strerror (errno));
				setState (State::COMMAND, true, true);
				return false;
			}

			auto const path = encodePath (dent->d_name);
			auto const rc   = fillDirent (st, path);
			if (rc != 0)
			{
				sendResponse ("425 %s\r\n", std::strerror (errno));
				setState (State::COMMAND, true, true);
				return false;
			}
		}
	}

	// send any pending data
	m_lock.unlock ();
	auto const rc = m_dataSocket->write (m_xferBuffer.usedArea (), m_xferBuffer.usedSize ());
	m_lock.lock ();
	if (rc <= 0)
	{
		// error sending data
		if (rc < 0 && errno == EWOULDBLOCK)
			return false;

		sendResponse ("426 Connection broken during transfer\r\n");
		setState (State::COMMAND, true, true);
		return false;
	}

	// we can try to send more data
	m_xferBuffer.markFree (rc);
	return true;
}

bool FtpSession::retrieveTransfer ()
{
	if (m_xferBuffer.empty ())
	{
		m_xferBuffer.clear ();

		auto const buffer = m_xferBuffer.freeArea ();
		auto const size   = m_xferBuffer.freeSize ();

		// we have sent all the data, so read some more
		m_lock.unlock ();
		auto const rc = m_file.read (buffer, size);
		m_lock.lock ();
		if (rc < 0)
		{
			// failed to read data
			sendResponse ("451 %s\r\n", std::strerror (errno));
			setState (State::COMMAND, true, true);
			return false;
		}

		if (rc == 0)
		{
			// reached end of file
			sendResponse ("226 OK\r\n");
			setState (State::COMMAND, true, true);
			return false;
		}

		// we read some data
		m_xferBuffer.markUsed (rc);
	}

	// send any pending data
	m_lock.unlock ();
	auto const rc = m_dataSocket->write (m_xferBuffer.usedArea (), m_xferBuffer.usedSize ());
	m_lock.lock ();
	if (rc <= 0)
	{
		// error sending data
		if (rc < 0 && errno == EWOULDBLOCK)
			return false;

		sendResponse ("426 Connection broken during transfer\r\n");
		setState (State::COMMAND, true, true);
		return false;
	}

	// we can try to read/send more data
	m_filePosition += rc;
	m_xferBuffer.markFree (rc);
	return true;
}

bool FtpSession::storeTransfer ()
{
	if (m_xferBuffer.empty ())
	{
		m_xferBuffer.clear ();

		auto const buffer = m_xferBuffer.freeArea ();
		auto const size   = m_xferBuffer.freeSize ();

		// we have written all the received data, so try to get some more
		m_lock.unlock ();
		auto const rc = m_dataSocket->read (buffer, size);
		m_lock.lock ();
		if (rc < 0)
		{
			// failed to read data
			if (errno == EWOULDBLOCK)
				return false;

			sendResponse ("451 %s\r\n", std::strerror (errno));
			setState (State::COMMAND, true, true);
			return false;
		}

		if (rc == 0)
		{
			// reached end of file
			sendResponse ("226 OK\r\n");
			setState (State::COMMAND, true, true);
			return false;
		}

		// we received some data
		m_xferBuffer.markUsed (rc);
	}

	// write any pending data
	m_lock.unlock ();
	auto const rc = m_file.write (m_xferBuffer.usedArea (), m_xferBuffer.usedSize ());
	m_lock.lock ();
	if (rc <= 0)
	{
		// error writing data
		sendResponse ("426 %s\r\n", rc < 0 ? std::strerror (errno) : "Failed to write data");
		setState (State::COMMAND, true, true);
		return false;
	}

	// we can try to recv/write more data
	m_filePosition += rc;
	m_xferBuffer.markFree (rc);
	return true;
}

///////////////////////////////////////////////////////////////////////////
void FtpSession::ABOR (char const *args_)
{
	if (m_state == State::COMMAND)
	{
		sendResponse ("225 No transfer to abort\r\n");
		return;
	}

	// abort the transfer
	sendResponse ("225 Aborted\r\n");
	sendResponse ("425 Transfer aborted\r\n");
	setState (State::COMMAND, true, true);
}

void FtpSession::ALLO (char const *args_)
{
	sendResponse ("202 Superfluous command\r\n");
	setState (State::COMMAND, false, false);
}

void FtpSession::APPE (char const *args_)
{
	// open the file in append mode
	xferFile (args_, XferFileMode::APPE);
}

void FtpSession::CDUP (char const *args_)
{
	setState (State::COMMAND, false, false);

	if (!changeDir (".."))
	{
		sendResponse ("550 %s\r\n", std::strerror (errno));
		return;
	}

	sendResponse ("200 OK\r\n");
}

void FtpSession::CWD (char const *args_)
{
	setState (State::COMMAND, false, false);

	if (!changeDir (args_))
	{
		sendResponse ("550 %s\r\n", std::strerror (errno));
		return;
	}

	sendResponse ("200 OK\r\n");
}

void FtpSession::DELE (char const *args_)
{
	setState (State::COMMAND, false, false);

	// build the path to remove
	auto const path = buildResolvedPath (m_cwd, args_);
	if (path.empty ())
	{
		sendResponse ("553 %s\r\n", std::strerror (errno));
		return;
	}

	// unlink the path
	if (::unlink (path.c_str ()) != 0)
	{
		sendResponse ("550 %s\r\n", std::strerror (errno));
		return;
	}

	FtpServer::updateFreeSpace ();
	sendResponse ("250 OK\r\n");
}
void FtpSession::FEAT (char const *args_)
{
	setState (State::COMMAND, false, false);
	sendResponse ("211-\r\n"
	              " MDTM\r\n"
	              " MLST Type%s;Size%s;Modify%s;Perm%s;UNIX.mode%s;\r\n"
	              " PASV\r\n"
	              " SIZE\r\n"
	              " TVFS\r\n"
	              " UTF8\r\n"
	              "\r\n"
	              "211 End\r\n",
	    m_mlstType ? "*" : "",
	    m_mlstSize ? "*" : "",
	    m_mlstModify ? "*" : "",
	    m_mlstPerm ? "*" : "",
	    m_mlstUnixMode ? "*" : "");
}

void FtpSession::HELP (char const *args_)
{
	setState (State::COMMAND, false, false);
	sendResponse ("214-\r\n"
	              "The following commands are recognized\r\n"
	              " ABOR ALLO APPE CDUP CWD DELE FEAT HELP LIST MDTM MKD MLSD MLST MODE\r\n"
	              " NLST NOOP OPTS PASS PASV PORT PWD QUIT REST RETR RMD RNFR RNTO STAT\r\n"
	              " STOR STOU STRU SYST TYPE USER XCUP XCWD XMKD XPWD XRMD\r\n"
	              "214 End\r\n");
}

void FtpSession::LIST (char const *args_)
{
	// open the path in LIST mode
	xferDir (args_, XferDirMode::LIST, true);
}

void FtpSession::MDTM (char const *args_)
{
	setState (State::COMMAND, false, false);
	sendResponse ("502 Command not implemented\r\n");
}

void FtpSession::MKD (char const *args_)
{
	setState (State::COMMAND, false, false);

	// build the path to create
	auto const path = buildResolvedPath (m_cwd, args_);
	if (path.empty ())
	{
		sendResponse ("553 %s\r\n", std::strerror (errno));
		return;
	}

	// create the directory
	if (::mkdir (path.c_str (), 0755) != 0)
	{
		sendResponse ("550 %s\r\n", std::strerror (errno));
		return;
	}

	FtpServer::updateFreeSpace ();
	sendResponse ("250 OK\r\n");
}

void FtpSession::MLSD (char const *args_)
{
	// open the path in MLSD mode
	xferDir (args_, XferDirMode::MLSD, true);
}

void FtpSession::MLST (char const *args_)
{
	setState (State::COMMAND, false, false);

	// build the path to list
	auto const path = buildResolvedPath (m_cwd, args_);
	if (path.empty ())
	{
		sendResponse ("501 %s\r\n", std::strerror (errno));
		return;
	}

	// stat path
	struct stat st;
	if (::lstat (path.c_str (), &st) != 0)
	{
		sendResponse ("550 %s\r\n", std::strerror (errno));
		return;
	}

	// encode path
	auto const encodedPath = encodePath (path);

	m_xferDirMode = XferDirMode::MLST;
	auto const rc = fillDirent (st, path);
	if (rc != 0)
	{
		sendResponse ("550 %s\r\n", std::strerror (rc));
		return;
	}

	sendResponse ("250-Status\r\n"
	              " %s\r\n"
	              "250 End\r\n",
	    encodedPath.c_str ());
}

void FtpSession::MODE (char const *args_)
{
	setState (State::COMMAND, false, false);

	// we only accept S (stream) mode
	if (::strcasecmp (args_, "S") == 0)
	{
		sendResponse ("200 OK\r\n");
		return;
	}

	sendResponse ("504 Unavailable\r\n");
}

void FtpSession::NLST (char const *args_)
{
	// open the path in NLST mode
	xferDir (args_, XferDirMode::NLST, false);
}

void FtpSession::NOOP (char const *args_)
{
	sendResponse ("200 OK\r\n");
}

void FtpSession::OPTS (char const *args_)
{
	setState (State::COMMAND, false, false);

	// check UTF8 options
	if (::strcasecmp (args_, "UTF8") == 0 || ::strcasecmp (args_, "UTF8 ON") == 0 ||
	    ::strcasecmp (args_, "UTF8 NLST") == 0)
	{
		sendResponse ("200 OK\r\n");
		return;
	}

	// check MLST options
	if (::strncasecmp (args_, "MLST ", 5) == 0)
	{
		m_mlstType     = false;
		m_mlstSize     = false;
		m_mlstModify   = false;
		m_mlstPerm     = false;
		m_mlstUnixMode = false;

		auto p = args_ + 5;
		while (*p)
		{
			auto const match = [] (auto const &name_, auto const &arg_) {
				return ::strncasecmp (name_, arg_, std::strlen (name_)) == 0;
			};

			if (match ("Type;", p))
				m_mlstType = true;
			else if (match ("Size;", p))
				m_mlstSize = true;
			else if (match ("Modify;", p))
				m_mlstModify = true;
			else if (match ("Perm;", p))
				m_mlstPerm = true;
			else if (match ("UNIX.mode;", p))
				m_mlstUnixMode = true;

			p = std::strchr (p, ';');
			if (!p)
				break;

			++p;
		}

		sendResponse ("200 MLST OPTS%s%s%s%s%s%s\r\n",
		    m_mlstType || m_mlstSize || m_mlstModify || m_mlstPerm || m_mlstUnixMode ? " " : "",
		    m_mlstType ? "Type;" : "",
		    m_mlstSize ? "Size;" : "",
		    m_mlstModify ? "Modify;" : "",
		    m_mlstPerm ? "Perm;" : "",
		    m_mlstUnixMode ? "UNIX.mode;" : "");
		return;
	}

	sendResponse ("504 %s\r\n", std::strerror (EINVAL));
}

void FtpSession::PASS (char const *args_)
{
	setState (State::COMMAND, false, false);
	sendResponse ("230 OK\r\n");
}

void FtpSession::PASV (char const *args_)
{
	// reset state
	setState (State::COMMAND, true, true);
	m_pasv = false;
	m_port = false;

	// create a socket to listen on
	m_pasvSocket = Socket::create ();
	if (!m_pasvSocket)
	{
		sendResponse ("451 Failed to create listening socket\r\n");
		return;
	}

	// set the socket options
	m_pasvSocket->setRecvBufferSize (SOCK_BUFFERSIZE);
	m_pasvSocket->setSendBufferSize (SOCK_BUFFERSIZE);

	// create an address to bind
	struct sockaddr_in addr = m_commandSocket->sockName ();
#ifdef _3DS
	static std::uint16_t ephemeralPort = 5001;
	if (ephemeralPort > 10000)
		ephemeralPort = 5001;
	addr.sin_port = htons (ephemeralPort++);
#else
	addr.sin_port = htons (0);
#endif

	// bind to the address
	if (!m_pasvSocket->bind (addr))
	{
		m_pasvSocket.reset ();
		sendResponse ("451 Failed to bind address\r\n");
		return;
	}

	// listen on the socket
	if (!m_pasvSocket->listen (1))
	{
		m_pasvSocket.reset ();
		sendResponse ("451 Failed to listen on socket\r\n");
		return;
	}

	// we are now listening on the socket
	auto const &sockName = m_pasvSocket->sockName ();
	std::string name     = sockName.name ();
	auto const port      = sockName.port ();
	Log::info ("Listening on [%s]:%u\n", name.c_str (), port);

	// send the address in the ftp format
	for (auto &c : name)
	{
		if (c == '.')
			c = ',';
	}

	m_pasv = true;
	sendResponse ("227 %s,%u,%u\r\n", name.c_str (), port >> 8, port & 0xFF);
}

void FtpSession::PORT (char const *args_)
{
	// reset state
	setState (State::COMMAND, true, true);
	m_pasv = false;
	m_port = false;

	std::string addrString = args_;

	// convert a,b,c,d,e,f with a.b.c.d\0e.f
	unsigned commas        = 0;
	char const *portString = nullptr;
	for (auto &p : addrString)
	{
		if (p == ',')
		{
			if (commas++ != 3)
				p = '.';
			else
			{
				p          = '\0';
				portString = &p + 1;
			}
		}
	}

	// check for the expected number of fields
	if (commas != 5)
	{
		sendResponse ("501 %s\r\n", std::strerror (EINVAL));
		return;
	}

	struct sockaddr_in addr = {};

	// parse the address
	if (!inet_aton (addrString.data (), &addr.sin_addr))
	{
		sendResponse ("501 %s\r\n", std::strerror (EINVAL));
		return;
	}

	// parse the port
	int val            = 0;
	std::uint16_t port = 0;
	for (auto p = portString; *p; ++p)
	{
		if (!std::isdigit (*p))
		{
			if (p == portString || *p != '.' || val > 0xFF)
			{
				sendResponse ("501 %s\r\n", std::strerror (EINVAL));
				return;
			}

			port <<= 8;
			port += val;
			val = 0;
		}
		else
		{
			val *= 10;
			val += *p - '0';
		}
	}

	if (val > 0xFF || port > 0xFF)
	{
		sendResponse ("501 %s\r\n", std::strerror (EINVAL));
		return;
	}

	port <<= 8;
	port += val;

	addr.sin_family = AF_INET;
	addr.sin_port   = htons (port);

	// we are ready to connect to the client
	m_portAddr = addr;
	m_port     = true;
	sendResponse ("200 OK\r\n");
}

void FtpSession::PWD (char const *args_)
{
	setState (State::COMMAND, false, false);

	auto const path = encodePath (m_cwd);

	std::string response = "257 \"";
	response += encodePath (m_cwd, true);
	response += "\"\r\n";

	sendResponse (response);
}

void FtpSession::QUIT (char const *args_)
{
	sendResponse ("221 Disconnecting\r\n");
	m_commandSocket.reset ();
}

void FtpSession::REST (char const *args_)
{
	setState (State::COMMAND, false, false);

	// parse the offset
	std::uint64_t pos = 0;
	for (auto p = args_; *p; ++p)
	{
		if (!std::isdigit (*p) || UINT64_MAX / 10 < pos)
		{
			sendResponse ("504 %s\r\n", std::strerror (errno));
			return;
		}

		pos *= 10;

		if (UINT64_MAX - (*p - '0') < pos)
		{
			sendResponse ("504 %s\r\n", std::strerror (errno));
			return;
		}

		pos += (*p - '0');
	}

	// set the restart offset
	m_restartPosition = pos;
	sendResponse ("200 OK\r\n");
}

void FtpSession::RETR (char const *args_)
{
	// open the file to retrieve
	xferFile (args_, XferFileMode::RETR);
}

void FtpSession::RMD (char const *args_)
{
	setState (State::COMMAND, false, false);

	// build the path to remove
	auto const path = buildResolvedPath (m_cwd, args_);
	if (path.empty ())
	{
		sendResponse ("553 %s\r\n", std::strerror (errno));
		return;
	}

	// remove the directory
	if (::rmdir (path.c_str ()) != 0)
	{
		sendResponse ("550 %s\r\n", std::strerror (errno));
		return;
	}

	FtpServer::updateFreeSpace ();
	sendResponse ("250 OK\r\n");
}

void FtpSession::RNFR (char const *args_)
{
	setState (State::COMMAND, false, false);

	// build the path to rename from
	auto const path = buildResolvedPath (m_cwd, args_);
	if (path.empty ())
	{
		sendResponse ("553 %s\r\n", std::strerror (errno));
		return;
	}

	// make sure the path exists
	struct stat st;
	if (::lstat (path.c_str (), &st) != 0)
	{
		sendResponse ("450 %s\r\n", std::strerror (errno));
		return;
	}

	// we are ready for RNTO
	m_rename = path;
	sendResponse ("350 OK\r\n");
}

void FtpSession::RNTO (char const *args_)
{
	setState (State::COMMAND, false, false);

	// make sure the previous command was RNFR
	if (m_rename.empty ())
	{
		sendResponse ("503 Bad sequence of commands\r\n");
		return;
	}

	// build the path to rename to
	auto const path = buildResolvedPath (m_cwd, args_);
	if (path.empty ())
	{
		m_rename.clear ();
		sendResponse ("554 %s\r\n", std::strerror (errno));
		return;
	}

	// rename the file
	if (::rename (m_rename.c_str (), path.c_str ()) != 0)
	{
		m_rename.clear ();
		sendResponse ("550 %s\r\n", std::strerror (errno));
		return;
	}

	// clear the rename state
	m_rename.clear ();

	FtpServer::updateFreeSpace ();
	sendResponse ("250 OK\r\n");
}

void FtpSession::SIZE (char const *args_)
{
	setState (State::COMMAND, false, false);

	// build the path to stat
	auto const path = buildResolvedPath (m_cwd, args_);
	if (path.empty ())
	{
		sendResponse ("553 %s\r\n", std::strerror (errno));
		return;
	}

	// stat the path
	struct stat st;
	if (::stat (path.c_str (), &st) != 0)
	{
		sendResponse ("550 %s\r\n", std::strerror (errno));
		return;
	}

	if (!S_ISREG (st.st_mode))
	{
		sendResponse ("550 Not a file\r\n");
		return;
	}

	sendResponse ("213 %" PRIu64 "\r\n", static_cast<std::uint64_t> (st.st_size));
}

void FtpSession::STAT (char const *args_)
{
	if (m_state == State::DATA_CONNECT)
	{
		sendResponse ("211-FTP server status\r\n"
		              " Waitin for data connection\r\n"
		              "211 End\r\n");
		return;
	}

	if (m_state == State::DATA_TRANSFER)
	{
		sendResponse ("211-FTP server status\r\n"
		              " Transferred %" PRIu64 " bytes\r\n"
		              "211 End\r\n",
		    m_filePosition);
		return;
	}

	if (std::strlen (args_) == 0)
	{
		// TODO keep track of start time
		auto const uptime =
		    std::chrono::system_clock::to_time_t (std::chrono::system_clock::now ()) -
		    FtpServer::startTime ();
		unsigned const hours   = uptime / 3600;
		unsigned const minutes = (uptime / 60) % 60;
		unsigned const seconds = uptime % 60;

		sendResponse ("211-FTP server status\r\n"
		              " Uptime: %02u:%02u:%02u\r\n"
		              "211 End\r\n",
		    hours,
		    minutes,
		    seconds);
		return;
	}

	xferDir (args_, XferDirMode::STAT, false);
}

void FtpSession::STOR (char const *args_)
{
	// open the file to store
	xferFile (args_, XferFileMode::STOR);
}

void FtpSession::STOU (char const *args_)
{
	setState (State::COMMAND, false, false);
	sendResponse ("502 Command not implemented\r\n");
}

void FtpSession::STRU (char const *args_)
{
	setState (State::COMMAND, false, false);

	// we only support F (no structure) mode
	if (::strcasecmp (args_, "F") == 0)
	{
		sendResponse ("200 OK\r\n");
		return;
	}

	sendResponse ("504 Unavailable\r\n");
}

void FtpSession::SYST (char const *args_)
{
	setState (State::COMMAND, false, false);
	sendResponse ("215 UNIX Type: L8\r\n");
}

void FtpSession::TYPE (char const *args_)
{
	setState (State::COMMAND, false, false);

	// we always transfer in binary mode
	sendResponse ("200 OK\r\n");
}

void FtpSession::USER (char const *args_)
{
	setState (State::COMMAND, false, false);
	sendResponse ("230 OK\r\n");
}

// clang-format off
std::vector<std::pair<std::string_view, void (FtpSession::*) (char const *)>> const
    FtpSession::handlers =
{
	{"ABOR", &FtpSession::ABOR}, 
	{"ALLO", &FtpSession::ALLO}, 
	{"APPE", &FtpSession::APPE}, 
	{"CDUP", &FtpSession::CDUP}, 
	{"CWD",  &FtpSession::CWD},
	{"DELE", &FtpSession::DELE}, 
	{"FEAT", &FtpSession::FEAT}, 
	{"HELP", &FtpSession::HELP}, 
	{"LIST", &FtpSession::LIST}, 
	{"MDTM", &FtpSession::MDTM}, 
	{"MKD",  &FtpSession::MKD},
	{"MLSD", &FtpSession::MLSD}, 
	{"MLST", &FtpSession::MLST}, 
	{"MODE", &FtpSession::MODE}, 
	{"NLST", &FtpSession::NLST}, 
	{"NOOP", &FtpSession::NOOP}, 
	{"OPTS", &FtpSession::OPTS}, 
	{"PASS", &FtpSession::PASS}, 
	{"PASV", &FtpSession::PASV}, 
	{"PORT", &FtpSession::PORT}, 
	{"PWD",  &FtpSession::PWD},
	{"QUIT", &FtpSession::QUIT}, 
	{"REST", &FtpSession::REST}, 
	{"RETR", &FtpSession::RETR}, 
	{"RMD",  &FtpSession::RMD},
	{"RNFR", &FtpSession::RNFR}, 
	{"RNTO", &FtpSession::RNTO}, 
	{"SIZE", &FtpSession::SIZE}, 
	{"STAT", &FtpSession::STAT}, 
	{"STOR", &FtpSession::STOR}, 
	{"STOU", &FtpSession::STOU}, 
	{"STRU", &FtpSession::STRU}, 
	{"SYST", &FtpSession::SYST}, 
	{"TYPE", &FtpSession::TYPE}, 
	{"USER", &FtpSession::USER}, 
	{"XCUP", &FtpSession::CDUP},
	{"XCWD", &FtpSession::CWD},
	{"XMKD", &FtpSession::MKD},
	{"XPWD", &FtpSession::PWD},
	{"XRMD", &FtpSession::RMD},
};
// clang-format on
