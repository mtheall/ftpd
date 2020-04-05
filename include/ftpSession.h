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

#include "fs.h"
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

class FtpSession
{
public:
	~FtpSession ();

	bool dead ();

	void draw ();

	static UniqueFtpSession create (UniqueSocket commandSocket_);

	static void poll (std::vector<UniqueFtpSession> const &sessions_);

private:
	constexpr static auto COMMAND_BUFFERSIZE  = 4096;
	constexpr static auto RESPONSE_BUFFERSIZE = 32768;
	constexpr static auto XFER_BUFFERSIZE     = 65536;
	constexpr static auto FILE_BUFFERSIZE     = 4 * XFER_BUFFERSIZE;

#ifdef _3DS
	constexpr static auto SOCK_BUFFERSIZE  = 32768;
	constexpr static auto POSITION_HISTORY = 100;
#else
	constexpr static auto SOCK_BUFFERSIZE  = XFER_BUFFERSIZE;
	constexpr static auto POSITION_HISTORY = 300;
#endif

	enum class State
	{
		COMMAND,
		DATA_CONNECT,
		DATA_TRANSFER,
	};

	enum class XferFileMode
	{
		RETR,
		STOR,
		APPE,
	};

	enum class XferDirMode
	{
		LIST,
		MLSD,
		MLST,
		NLST,
		STAT,
	};

	FtpSession (UniqueSocket commandSocket_);

	void setState (State state_, bool closePasv_, bool closeData_);

	void closeData ();

	bool changeDir (char const *args_);

	bool dataAccept ();
	bool dataConnect ();

	void updateFreeSpace ();

	int fillDirent (struct stat const &st_, std::string_view path_, char const *type_ = nullptr);
	int fillDirent (std::string const &path_, char const *type_ = nullptr);
	void xferFile (char const *args_, XferFileMode mode_);
	void xferDir (char const *args_, XferDirMode mode_, bool workaround_);

	void readCommand (int events_);
	void writeResponse ();

	__attribute__ ((format (printf, 2, 3))) void sendResponse (char const *fmt_, ...);
	void sendResponse (std::string_view response_);

	bool (FtpSession::*m_transfer) () = nullptr;

	bool listTransfer ();
	bool retrieveTransfer ();
	bool storeTransfer ();

	platform::Mutex m_lock;

	SharedSocket m_commandSocket;
	UniqueSocket m_pasvSocket;
	SharedSocket m_dataSocket;
	std::vector<SharedSocket> m_pendingCloseSocket;

	IOBuffer m_commandBuffer;
	IOBuffer m_responseBuffer;
	IOBuffer m_xferBuffer;

	SockAddr m_pasvAddr;
	SockAddr m_portAddr;

	std::string m_cwd = "/";
	std::string m_lwd;
	std::string m_rename;
	std::string m_workItem;

	std::string m_windowName;
	std::string m_plotName;

	std::uint64_t m_restartPosition = 0;
	std::uint64_t m_filePosition    = 0;
	std::uint64_t m_fileSize        = 0;

	platform::steady_clock::time_point m_filePositionTime;
	std::uint64_t m_filePositionHistory[POSITION_HISTORY];
	float m_filePositionDeltas[POSITION_HISTORY];
	float m_xferRate;

	State m_state = State::COMMAND;

	fs::File m_file;
	fs::Dir m_dir;

	XferDirMode m_xferDirMode;

	bool m_pasv : 1;
	bool m_port : 1;
	bool m_recv : 1;
	bool m_send : 1;
	bool m_urgent : 1;

	bool m_mlstType : 1;
	bool m_mlstSize : 1;
	bool m_mlstModify : 1;
	bool m_mlstPerm : 1;
	bool m_mlstUnixMode : 1;

	void ABOR (char const *args_);
	void ALLO (char const *args_);
	void APPE (char const *args_);
	void CDUP (char const *args_);
	void CWD (char const *args_);
	void DELE (char const *args_);
	void FEAT (char const *args_);
	void HELP (char const *args_);
	void LIST (char const *args_);
	void MDTM (char const *args_);
	void MKD (char const *args_);
	void MLSD (char const *args_);
	void MLST (char const *args_);
	void MODE (char const *args_);
	void NLST (char const *args_);
	void NOOP (char const *args_);
	void OPTS (char const *args_);
	void PASS (char const *args_);
	void PASV (char const *args_);
	void PORT (char const *args_);
	void PWD (char const *args_);
	void QUIT (char const *args_);
	void REST (char const *args_);
	void RETR (char const *args_);
	void RMD (char const *args_);
	void RNFR (char const *args_);
	void RNTO (char const *args_);
	void SIZE (char const *args_);
	void STAT (char const *args_);
	void STOR (char const *args_);
	void STOU (char const *args_);
	void STRU (char const *args_);
	void SYST (char const *args_);
	void TYPE (char const *args_);
	void USER (char const *args_);

	static std::vector<std::pair<std::string_view, void (FtpSession::*) (char const *)>> const
	    handlers;
};
