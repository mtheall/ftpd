// ftpd is a server implementation based on the following:
// - RFC  959 (https://tools.ietf.org/html/rfc959)
// - RFC 3659 (https://tools.ietf.org/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
//
// Copyright (C) 2022 Michael Theall
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

#include "ftpConfig.h"
#include "ftpSession.h"
#include "platform.h"
#include "socket.h"

#ifndef CLASSIC
#include <curl/curl.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class FtpServer;
using UniqueFtpServer = std::unique_ptr<FtpServer>;

/// \brief FTP server
class FtpServer
{
public:
	~FtpServer ();

	/// \brief Draw server and all of its sessions
	void draw ();

	/// \brief Create server
	static UniqueFtpServer create ();

	/// \brief Get free space
	static std::string getFreeSpace ();

	/// \brief Update free space
	static void updateFreeSpace ();

	/// \brief Server start time
	static std::time_t startTime ();

private:
	/// \brief Paramterized constructor
	/// \param config_ FTP config
	FtpServer (UniqueFtpConfig config_);

	/// \brief Handle when network is found
	void handleNetworkFound ();

	/// \brief Handle when network is lost
	void handleNetworkLost ();

#ifndef CLASSIC
	/// \brief Show menu in the current window
	void showMenu ();

	/// \brief Show settings menu
	void showSettings ();

	/// \brief Show about window
	void showAbout ();
#endif

	/// \brief Server loop
	void loop ();

	/// \brief Thread entry point
	void threadFunc ();

#ifndef NDS
	/// \brief Thread
	platform::Thread m_thread;

	/// \brief Mutex
	platform::Mutex m_lock;
#endif

	/// \brief Config
	UniqueFtpConfig m_config;

	/// \brief Listen socket
	UniqueSocket m_socket;

	/// \brief ImGui window name
	std::string m_name;

	/// \brief Sessions
	std::vector<UniqueFtpSession> m_sessions;

	/// \brief Whether thread should quit
	std::atomic<bool> m_quit;

#ifndef CLASSIC
	/// \brief Log upload cURL context
	CURLM *m_uploadLogCurlM = nullptr;
	/// \brief Log upload mime context
	curl_mime *m_uploadLogMime = nullptr;
	/// \brief Log upload cURL context
	std::atomic<CURL *> m_uploadLogCurl = nullptr;

	/// \brief Log upload data
	std::string m_uploadLogData;
	/// \brief Log upload result
	std::string m_uploadLogResult;
#endif

#ifndef CLASSIC
	/// \brief Whether to show settings menu
	bool m_showSettings = false;

#ifdef __SWITCH__
	/// \brief Whether to show access point menu
	bool m_showAP = false;
#endif

	/// \brief Whether to show about window
	bool m_showAbout = false;

	/// \brief User name setting
	std::string m_userSetting;

	/// \brief Password setting
	std::string m_passSetting;

	/// \brief Port setting
	std::uint16_t m_portSetting;

#ifdef __3DS__
	/// \brief getMTime setting
	bool m_getMTimeSetting;
#endif

#ifdef __SWITCH__
	/// \brief Whether an error occurred enabling access point
	std::atomic<bool> m_apError = false;

	/// \brief Enable access point setting
	bool m_enableAPSetting;

	/// \brief Access point SSID setting
	std::string m_ssidSetting;

	/// \brief Access point passphrase setting
	std::string m_passphraseSetting;
#endif
#endif
};
