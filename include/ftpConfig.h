// ftpd is a server implementation based on the following:
// - RFC  959 (https://tools.ietf.org/html/rfc959)
// - RFC 3659 (https://tools.ietf.org/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
//
// Copyright (C) 2023 Michael Theall
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

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

class FtpConfig;
using UniqueFtpConfig = std::unique_ptr<FtpConfig>;

/// \brief FTP config
class FtpConfig
{
public:
	~FtpConfig ();

	/// \brief Create config
	static UniqueFtpConfig create ();

	/// \brief Load config
	/// \param path_ Path to config file
	static UniqueFtpConfig load (char const *path_);

#ifndef __NDS__
	std::scoped_lock<platform::Mutex> lockGuard ();
#endif

	/// \brief Save config
	/// \param path_ Path to config file
	bool save (char const *path_);

	/// \brief Get user
	std::string const &user () const;

	/// \brief Get password
	std::string const &pass () const;

	/// \brief Get port
	std::uint16_t port () const;

#ifdef __3DS__
	/// \brief Whether to get mtime
	/// \note only effective on 3DS
	bool getMTime () const;
#endif

#ifdef __SWITCH__
	/// \brief Whether to enable access point
	bool enableAP () const;

	/// \brief Access point SSID
	std::string const &ssid () const;

	/// \brief Access point passphrase
	std::string const &passphrase () const;
#endif

	/// \brief Set user
	/// \param user_ User
	void setUser (std::string const &user_);

	/// \brief Set password
	/// \param pass_ Password
	void setPass (std::string const &pass_);

	/// \brief Set listen port
	/// \param port_ Listen port
	bool setPort (std::string const &port_);

	/// \brief Set listen port
	/// \param port_ Listen port
	bool setPort (std::uint16_t port_);

#ifdef __3DS__
	/// \brief Set whether to get mtime
	/// \param getMTime_ Whether to get mtime
	void setGetMTime (bool getMTime_);
#endif

#ifdef __SWITCH__
	/// \brief Set whether to enable access point
	/// \param enable_ Whether to enable access point
	void setEnableAP (bool enable_);

	/// \brief Set access point SSID
	/// \param ssid_ SSID
	void setSSID (std::string const &ssid_);

	/// \brief Set access point passphrase
	/// \param passphrase_ Passphrase
	void setPassphrase (std::string const &passphrase_);
#endif

private:
	FtpConfig ();

#ifndef __NDS__
	/// \brief Mutex
	mutable platform::Mutex m_lock;
#endif

	/// \brief Username
	std::string m_user;

	/// \brief Password
	std::string m_pass;

	/// \brief Listen port
	std::uint16_t m_port;

#ifdef __3DS__
	/// \brief Whether to get mtime
	bool m_getMTime = true;
#endif

#ifdef __SWITCH__
	/// \brief Whether to enable access point
	bool m_enableAP = false;

	/// \brief Access point SSID
	std::string m_ssid = "ftpd";

	/// \brief Access point passphrase
	std::string m_passphrase;
#endif
};
