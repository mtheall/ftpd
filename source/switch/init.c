// ftpd is a server implementation based on the following:
// - RFC  959 (https://tools.ietf.org/html/rfc959)
// - RFC 3659 (https://tools.ietf.org/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
// - Deflate transmission mode for FTP
//   (https://tools.ietf.org/html/draft-preston-ftpext-deflate-04)
//
// Copyright (C) 2024 Michael Theall
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

#include <switch.h>

#include <unistd.h>

#ifndef NDEBUG
/// \brief nxlink socket fd
static int s_fd = -1;
#endif

/// \brief Socket initialization configuration
static SocketInitConfig const s_socketInitConfig = {
    .tcp_tx_buf_size     = 1 * 1024 * 1024,
    .tcp_rx_buf_size     = 1 * 1024 * 1024,
    .tcp_tx_buf_max_size = 4 * 1024 * 1024,
    .tcp_rx_buf_max_size = 4 * 1024 * 1024,

    .udp_tx_buf_size = 0x2400,
    .udp_rx_buf_size = 0xA500,

    .sb_efficiency = 8,

    .num_bsd_sessions = 2,
    .bsd_service_type = BsdServiceType_User,
};

/// \brief Number of FS sessions
u32 __nx_fs_num_sessions = 1;

/// \brief Called before main ()
void userAppInit ()
{
	// disable immediate app close
	appletLockExit ();
	// disable auto-sleep
	appletSetAutoSleepDisabled (true);

	romfsInit ();
	plInitialize (PlServiceType_User);
	psmInitialize ();
	nifmInitialize (NifmServiceType_User);
	socketInitialize (&s_socketInitConfig);

#ifndef NDEBUG
	// s_fd = nxlinkStdioForDebug ();
#endif
}

void userAppExit ()
{
#ifndef NDEBUG
	if (s_fd >= 0)
	{
		close (s_fd);
		s_fd = -1;
	}
#endif

	socketExit ();
	nifmExit ();
	psmExit ();
	plExit ();
	romfsExit ();
	// Restore auto-sleep
	appletSetAutoSleepDisabled (false);
	appletUnlockExit ();
}
