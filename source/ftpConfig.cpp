// ftpd is a server implementation based on the following:
// - RFC  959 (https://tools.ietf.org/html/rfc959)
// - RFC 3659 (https://tools.ietf.org/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
// - Deflate transmission mode for FTP
//   (https://tools.ietf.org/html/draft-preston-ftpext-deflate-04)
//
// Copyright (C) 2025 Michael Theall
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

#include "ftpConfig.h"

#include "fs.h"
#include "log.h"
#include "platform.h"

#include <gsl/pointers>

#include <zlib.h>

#include <sys/stat.h>
using stat_t = struct stat;

#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace
{
constexpr std::uint16_t DEFAULT_PORT = 5000;
constexpr int DEFAULT_DEFLATE_LEVEL  = 6;

bool mkdirParent (std::string_view const path_)
{
	auto pos = path_.find_first_of ('/');
	while (pos != std::string::npos)
	{
		auto const dir = std::string (path_.substr (0, pos));

		stat_t st{};
		auto const rc = ::stat (dir.c_str (), &st);
		if (rc < 0 && errno != ENOENT)
			return false;

		if (rc < 0 && errno == ENOENT)
		{
			auto const rc = ::mkdir (dir.c_str (), 0755);
			if (rc < 0)
				return false;
		}

		pos = path_.find_first_of ('/', pos + 1);
	}

	return true;
}

std::string_view strip (std::string_view const str_)
{
	auto const start = str_.find_first_not_of (" \t");
	if (start == std::string::npos)
		return {};

	auto const end = str_.find_last_not_of (" \t");
	if (end == std::string::npos)
		return str_.substr (start);

	return str_.substr (start, end + 1 - start);
}

template <typename T>
bool parseInt (T &out_, std::string_view const val_)
{
	auto const rc = std::from_chars (val_.data (), val_.data () + val_.size (), out_);
	if (rc.ec != std::errc{})
	{
		errno = static_cast<int> (rc.ec);
		return false;
	}

	if (rc.ptr != val_.data () + val_.size ())
	{
		errno = EINVAL;
		return false;
	}

	return true;
}
}

///////////////////////////////////////////////////////////////////////////
FtpConfig::~FtpConfig () = default;

FtpConfig::FtpConfig () : m_port (DEFAULT_PORT), m_deflateLevel (DEFAULT_DEFLATE_LEVEL)
{
}

UniqueFtpConfig FtpConfig::create ()
{
	return UniqueFtpConfig (new FtpConfig ());
}

UniqueFtpConfig FtpConfig::load (gsl::not_null<gsl::czstring> const path_)
{
	auto config = create ();

	auto fp = fs::File ();
	if (!fp.open (path_))
		return config;

	std::uint16_t port = DEFAULT_PORT;
	int deflateLevel   = DEFAULT_DEFLATE_LEVEL;

	std::string line;
	while (!(line = fp.readLine ()).empty ())
	{
		auto const pos = line.find_first_of ('=');
		if (pos == std::string::npos)
		{
			error ("Ignoring '%s'\n", line.c_str ());
			continue;
		}

		auto const key = strip (std::string_view (line).substr (0, pos));
		auto const val = strip (std::string_view (line).substr (pos + 1));
		if (key.empty () || val.empty ())
		{
			error ("Ignoring '%s'\n", line.c_str ());
			continue;
		}

		if (key == "user")
			config->m_user = val;
		else if (key == "pass")
			config->m_pass = val;
		else if (key == "hostname")
			config->m_hostname = val;
		else if (key == "port")
			parseInt (port, val);
		else if (key == "deflateLevel")
			parseInt (deflateLevel, val);
#ifdef __3DS__
		else if (key == "mtime")
		{
			if (val == "0")
				config->m_getMTime = false;
			else if (val == "1")
				config->m_getMTime = true;
			else
				error ("Invalid value for mtime: %.*s\n",
				    gsl::narrow_cast<int> (val.size ()),
				    val.data ());
		}
#endif
#ifdef __SWITCH__
		else if (key == "ap")
		{
			if (val == "0")
				config->m_enableAP = false;
			else if (val == "1")
				config->m_enableAP = true;
			else
				error ("Invalid value for ap: %.*s\n",
				    gsl::narrow_cast<int> (val.size ()),
				    val.data ());
		}
		else if (key == "ssid")
			config->m_ssid = val;
		else if (key == "passphrase")
			config->m_passphrase = val;
#endif
	}

	config->setPort (port);
	config->setDeflateLevel (deflateLevel);

	return config;
}

#ifndef __NDS__
std::scoped_lock<platform::Mutex> FtpConfig::lockGuard ()
{
	return std::scoped_lock<platform::Mutex> (m_lock);
}
#endif

bool FtpConfig::save (gsl::not_null<gsl::czstring> const path_)
{
	if (!mkdirParent (path_.get ()))
		return false;

	auto fp = fs::File ();
	if (!fp.open (path_, "wb"))
		return false;

	if (!m_user.empty ())
		(void)std::fprintf (fp, "user=%s\n", m_user.c_str ());
	if (!m_pass.empty ())
		(void)std::fprintf (fp, "pass=%s\n", m_pass.c_str ());
	if (!m_hostname.empty ())
		(void)std::fprintf (fp, "hostname=%s\n", m_hostname.c_str ());
	(void)std::fprintf (fp, "port=%u\n", m_port);
	(void)std::fprintf (fp, "deflateLevel=%u", m_deflateLevel);

#ifdef __3DS__
	(void)std::fprintf (fp, "mtime=%u\n", m_getMTime);
#endif

#ifdef __SWITCH__
	(void)std::fprintf (fp, "ap=%u\n", m_enableAP);
	if (!m_ssid.empty ())
		(void)std::fprintf (fp, "ssid=%s\n", m_ssid.c_str ());
	if (!m_passphrase.empty ())
		(void)std::fprintf (fp, "passphrase=%s\n", m_passphrase.c_str ());
#endif

	return true;
}

std::string const &FtpConfig::user () const
{
	return m_user;
}

std::string const &FtpConfig::pass () const
{
	return m_pass;
}

std::string const &FtpConfig::hostname () const
{
	return m_hostname;
}

std::uint16_t FtpConfig::port () const
{
	return m_port;
}

int FtpConfig::deflateLevel () const
{
	return m_deflateLevel;
}

#ifdef __3DS__
bool FtpConfig::getMTime () const
{
	return m_getMTime;
}
#endif

#ifdef __SWITCH__
bool FtpConfig::enableAP () const
{
	return m_enableAP;
}

std::string const &FtpConfig::ssid () const
{
	return m_ssid;
}

std::string const &FtpConfig::passphrase () const
{
	return m_passphrase;
}
#endif

void FtpConfig::setUser (std::string user_)
{
	m_user = std::move (user_);
}

void FtpConfig::setPass (std::string pass_)
{
	m_pass = std::move (pass_);
}

void FtpConfig::setHostname (std::string hostname_)
{
	m_hostname = std::move (hostname_);
}

bool FtpConfig::setPort (std::string_view const port_)
{
	std::uint16_t parsed{};
	if (!parseInt (parsed, port_))
		return false;

	return setPort (parsed);
}

bool FtpConfig::setPort (std::uint16_t const port_)
{
#ifdef __SWITCH__
	// Switch is not allowed < 1024, except 0
	if (port_ < 1024 && port_ != 0)
	{
		errno = EPERM;
		return false;
	}
#elif defined(__NDS__) || defined(__3DS__)
	// 3DS is allowed < 1024, but not 0
	// NDS is allowed < 1024, but 0 crashes the app
	if (port_ == 0)
	{
		errno = EPERM;
		return false;
	}
#endif

	m_port = port_;
	return true;
}

bool FtpConfig::setDeflateLevel (std::string_view const level_)
{
	int parsed;
	if (!parseInt (parsed, level_))
		return false;

	return setDeflateLevel (parsed);
}

bool FtpConfig::setDeflateLevel (int const level_)
{
	if (level_ < Z_NO_COMPRESSION || level_ > Z_BEST_COMPRESSION)
		return false;

	m_deflateLevel = level_;
	return true;
}

#ifdef __3DS__
void FtpConfig::setGetMTime (bool const getMTime_)
{
	m_getMTime = getMTime_;
}
#endif

#ifdef __SWITCH__
void FtpConfig::setEnableAP (bool const enable_)
{
	m_enableAP = enable_;
}

void FtpConfig::setSSID (std::string_view const ssid_)
{
	m_ssid = ssid_.substr (0, ssid_.find_first_of ('\0'));
}

void FtpConfig::setPassphrase (std::string_view const passphrase_)
{
	m_passphrase = passphrase_.substr (0, passphrase_.find_first_of ('\0'));
}
#endif
