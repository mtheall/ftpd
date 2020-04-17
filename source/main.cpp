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

#include "platform.h"

#include "ftpServer.h"

#include "imgui.h"

#include <cstdio>
#include <cstdlib>

int main (int argc_, char *argv_[])
{
#ifndef CLASSIC
	IMGUI_CHECKVERSION ();
	ImGui::CreateContext ();
#endif

	if (!platform::init ())
	{
#ifndef CLASSIC
		ImGui::DestroyContext ();
#endif
		return EXIT_FAILURE;
	}

#ifndef CLASSIC
	auto &style = ImGui::GetStyle ();

	// turn off window rounding
	style.WindowRounding = 0.0f;
#endif

	auto server = FtpServer::create (5000);

	while (platform::loop ())
	{
		server->draw ();

		platform::render ();
	}

	// clean up resources before exiting switch/3ds services
	server.reset ();

	platform::exit ();

#ifndef CLASSIC
	ImGui::DestroyContext ();
#endif
}
