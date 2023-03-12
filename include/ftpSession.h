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

#include "fs.h"
#include "ftpConfig.h"
#include "ioBuffer.h"
#include "platform.h"
#include "socket.h"

#include <chrono>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

class FtpSession;
using UniqueFtpSession = std::unique_ptr<FtpSession>;

/// \brief FTP session
class FtpSession
{
public:
	~FtpSession ();

	/// \brief Whether session sockets are all inactive
	bool dead ();

	/// \brief Draw session status
	void draw ();

	/// \brief Create session
	/// \param config_ FTP config
	/// \param commandSocket_ Command socket
	static UniqueFtpSession create (FtpConfig &config_, UniqueSocket commandSocket_);

	/// \brief Poll for activity
	/// \param sessions_ Sessions to poll
	static bool poll (std::vector<UniqueFtpSession> const &sessions_);

private:
	/// \brief Command buffer size
	constexpr static auto COMMAND_BUFFERSIZE = 4096;

#ifdef __NDS__
	/// \brief Response buffer size
	constexpr static auto RESPONSE_BUFFERSIZE = 4096;

	/// \brief Transfer buffersize
	constexpr static auto XFER_BUFFERSIZE = 8192;
#else
	/// \brief Response buffer size
	constexpr static auto RESPONSE_BUFFERSIZE = 32768;

	/// \brief Transfer buffersize
	constexpr static auto XFER_BUFFERSIZE = 65536;
#endif

	/// \brief File buffersize
	constexpr static auto FILE_BUFFERSIZE = 4 * XFER_BUFFERSIZE;

#if defined(__NDS__)
	/// \brief Socket buffer size
	constexpr static auto SOCK_BUFFERSIZE = 4096;

	/// \brief Amount of file position history to keep
	constexpr static auto POSITION_HISTORY = 60;
#elif defined(__3DS__)
	/// \brief Socket buffer size
	constexpr static auto SOCK_BUFFERSIZE = 32768;

	/// \brief Amount of file position history to keep
	constexpr static auto POSITION_HISTORY = 100;
#else
	/// \brief Socket buffer size
	constexpr static auto SOCK_BUFFERSIZE = XFER_BUFFERSIZE;

	/// \brief Amount of file position history to keep
	constexpr static auto POSITION_HISTORY = 300;
#endif

	/// \brief Session state
	enum class State
	{
		COMMAND,
		DATA_CONNECT,
		DATA_TRANSFER,
	};

	/// \brief Transfer file mode
	enum class XferFileMode
	{
		RETR,
		STOR,
		APPE,
	};

	/// \brief Transfer directory mode
	enum class XferDirMode
	{
		LIST,
		MLSD,
		MLST,
		NLST,
		STAT,
	};

	/// \brief Parameterized constructor
	/// \param config_ FTP config
	/// \param commandSocket_ Command socket
	FtpSession (FtpConfig &config_, UniqueSocket commandSocket_);

	/// \brief Whether session is authorized
	bool authorized () const;

	/// \brief Set session state
	/// \param state_ State to set
	/// \param closePasv_ Whether to close listening socket
	/// \param closeData_ Whether to close data socket
	void setState (State state_, bool closePasv_, bool closeData_);

	/// \brief Close socket
	/// \param socket_ Socket to close
	void closeSocket (SharedSocket &socket_);
	/// \brief Close command socket
	void closeCommand ();
	/// \brief Close passive socket
	void closePasv ();
	/// \brief Close data socket
	void closeData ();

	/// \brief Change working directory
	bool changeDir (char const *args_);

	/// \brief Accept connection as data socket
	bool dataAccept ();

	/// \brief Connect data socket
	bool dataConnect ();

	/// \brief Fill directory entry
	/// \param st_ Entry status
	/// \param path_ Path name
	/// \param type_ MLST type
	int fillDirent (struct stat const &st_, std::string_view path_, char const *type_ = nullptr);

	/// \brief Fill directory entry
	/// \param path_ Path name
	/// \param type_ MLST type
	int fillDirent (std::string const &path_, char const *type_ = nullptr);

	/// \brief Transfer file
	/// \param args_ Command arguments
	/// \param mode_ Transfer file mode
	void xferFile (char const *args_, XferFileMode mode_);

	/// \brief Transfer directory
	/// \param args_ Command arguments
	/// \param mode_ Transfer directory mode
	/// \param workaround_ Workaround broken clients who use LIST -a/-l
	void xferDir (char const *args_, XferDirMode mode_, bool workaround_);

	/// \brief Read command
	/// \param events_ Poll events
	void readCommand (int events_);

	/// \brief Write response
	void writeResponse ();

	/// \brief Send response
	/// \param fmt_ Message format
	__attribute__ ((format (printf, 2, 3))) void sendResponse (char const *fmt_, ...);

	/// \brief Send response
	/// \param response_ Response message
	void sendResponse (std::string_view response_);

	/// \brief Transfer function
	bool (FtpSession::*m_transfer) () = nullptr;

	/// \brief Transfer directory list
	bool listTransfer ();

	/// \brief Transfer download
	bool retrieveTransfer ();

	/// \brief Transfer upload
	bool storeTransfer ();

#ifndef __NDS__
	/// \brief Mutex
	platform::Mutex m_lock;
#endif

	/// \brief FTP config
	FtpConfig &m_config;

	/// \brief Command socket
	SharedSocket m_commandSocket;

	/// \brief Data listen socker
	UniqueSocket m_pasvSocket;

	/// \brief Data socket
	SharedSocket m_dataSocket;

	/// \brief Sockets pending close
	std::vector<SharedSocket> m_pendingCloseSocket;

	/// \brief Command buffer
	IOBuffer m_commandBuffer;

	/// \brief Response buffer
	IOBuffer m_responseBuffer;

	/// \brief Transfer buffer
	IOBuffer m_xferBuffer;

	/// \brief Address from last PORT command
	SockAddr m_portAddr;

	/// \brief Current working directory
	std::string m_cwd = "/";

	/// \brief List working directory
	std::string m_lwd;

	/// \brief Path from RNFR command
	std::string m_rename;

	/// \brief Current work item
	std::string m_workItem;

	/// \brief ImGui window name
	std::string m_windowName;

	/// \brief ImGui plot widget name
	std::string m_plotName;

	/// \brief Position from REST command
	std::uint64_t m_restartPosition = 0;

	/// \brief Current file position
	std::uint64_t m_filePosition = 0;

	/// \brief File size of current transfer
	std::uint64_t m_fileSize = 0;

	/// \brief Last file position update timestamp
	platform::steady_clock::time_point m_filePositionTime;

	/// \brief File position history
	std::uint64_t m_filePositionHistory[POSITION_HISTORY];

	/// \brief File position history deltas
	float m_filePositionDeltas[POSITION_HISTORY];

	/// \brief Transfer rate (EWMA low-pass filtered)
	float m_xferRate;

	/// \brief Session state
	State m_state = State::COMMAND;

	/// \brief File being transferred
	fs::File m_file;

	/// \brief Directory being transferred
	fs::Dir m_dir;

	/// \brief Directory transfer mode
	XferDirMode m_xferDirMode;

	/// \brief Last command timestamp
	time_t m_timestamp;

	/// \brief Whether user has been authorized
	bool m_authorizedUser : 1;
	/// \brief Whether password has been authorized
	bool m_authorizedPass : 1;
	/// \brief Whether previous command was PASV
	bool m_pasv : 1;
	/// \brief Whether previous command was PORT
	bool m_port : 1;
	/// \brief Whether receiving data
	bool m_recv : 1;
	/// \brief Whether sending data
	bool m_send : 1;
	/// \brief Whether urgent (out-of-band) data is on the way
	bool m_urgent : 1;

	/// \brief Whether MLST type fact is enabled
	bool m_mlstType : 1;
	/// \brief Whether MLST size fact is enabled
	bool m_mlstSize : 1;
	/// \brief Whether MLST modify fact is enabled
	bool m_mlstModify : 1;
	/// \brief Whether MLST perm fact is enabled
	bool m_mlstPerm : 1;
	/// \brief Whether MLST unix.mode fact is enabled
	bool m_mlstUnixMode : 1;

	/// \brief Whether emulating /dev/zero
	bool m_devZero : 1;

	/// \brief Abort a transfer
	/// \param args_ Command arguments
	void ABOR (char const *args_);

	/// \brief Allocate space
	/// \param args_ Command arguments
	void ALLO (char const *args_);

	/// \brief Append data to a file
	/// \param args_ Command arguments
	void APPE (char const *args_);

	/// \brief CWD to parent directory
	/// \param args_ Command arguments
	void CDUP (char const *args_);

	/// \brief Change working directory
	/// \param args_ Command arguments
	void CWD (char const *args_);

	/// \brief Delete a file
	/// \param args_ Command arguments
	void DELE (char const *args_);

	/// \brief List server features
	/// \param args_ Command arguments
	void FEAT (char const *args_);

	/// \brief Print server help
	/// \param args_ Command arguments
	void HELP (char const *args_);

	/// \brief List directory
	/// \param args_ Command arguments
	void LIST (char const *args_);

	/// \brief Last modification time
	/// \param args_ Command arguments
	void MDTM (char const *args_);

	/// \brief Create a directory
	/// \param args_ Command arguments
	void MKD (char const *args_);

	/// \brief Machine list directory
	/// \param args_ Command arguments
	void MLSD (char const *args_);

	/// \brief Machine list
	/// \param args_ Command arguments
	void MLST (char const *args_);

	/// \brief Set transfer mode
	/// \param args_ Command arguments
	void MODE (char const *args_);

	/// \brief Name list
	/// \param args_ Command arguments
	void NLST (char const *args_);

	/// \brief No-op
	/// \param args_ Command arguments
	void NOOP (char const *args_);

	/// \brief Set server options
	/// \param args_ Command arguments
	void OPTS (char const *args_);

	/// \brief Password
	/// \param args_ Command arguments
	void PASS (char const *args_);

	/// \brief Request an address to connect to for data transfers
	/// \param args_ Command arguments
	void PASV (char const *args_);

	/// \brief Provide an address to connect to for data transfers
	/// \param args_ Command arguments
	void PORT (char const *args_);

	/// \brief Print working directory
	/// \param args_ Command arguments
	void PWD (char const *args_);

	/// \brief Terminate session
	/// \param args_ Command arguments
	void QUIT (char const *args_);

	/// \brief Restart a file transfer
	/// \param args_ Command arguments
	void REST (char const *args_);

	/// \brief Retrieve a file
	/// \param args_ Command arguments
	/// \note Requires a PASV or PORT connection
	void RETR (char const *args_);

	/// \brief Remove a directory
	/// \param args_ Command arguments
	void RMD (char const *args_);

	/// \brief Rename from
	/// \param args_ Command arguments
	void RNFR (char const *args_);

	/// \brief Rename to
	/// \param args_ Command arguments
	void RNTO (char const *args_);

	/// \brief Site command
	/// \param args_ Command arguments
	void SITE (char const *args_);

	/// \brief Get file size
	/// \param args_ Command arguments
	void SIZE (char const *args_);

	/// \brief Get status
	/// \param args_ Command arguments
	/// \note If no argument is supplied, and a transfer is occurring, get the current transfer
	/// status. If no argument is supplied, and no transfer is occurring, get the server status. If
	/// an argument is supplied, this is equivalent to LIST, except the data is sent over the
	/// command socket.
	void STAT (char const *args_);

	/// \brief Store a file
	/// \param args_ Command arguments
	void STOR (char const *args_);

	/// \brief Store a unique file
	/// \param args_ Command arguments
	void STOU (char const *args_);

	/// \brief Set file structure
	/// \param args_ Command arguments
	void STRU (char const *args_);

	/// \brief Identify system
	/// \param args_ Command arguments
	void SYST (char const *args_);

	/// \brief Set representation type
	/// \param args_ Command arguments
	void TYPE (char const *args_);

	/// \brief User name
	/// \param args_ Command arguments
	void USER (char const *args_);

	/// \brief Map of command handlers
	static std::vector<std::pair<std::string_view, void (FtpSession::*) (char const *)>> const
	    handlers;
};
