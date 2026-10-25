// ftpd is a server implementation based on the following:
// - RFC  959 (https://tools.ietf.org/html/rfc959)
// - RFC 3659 (https://tools.ietf.org/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
// - Deflate transmission mode for FTP
//   (https://tools.ietf.org/html/draft-preston-ftpext-deflate-04)
//
// Copyright (C) 2026 Michael Theall
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

#ifndef CLASSIC
#include <imgui.h>

#include <curl/curl.h>
#endif

#include <cstdio>
#include <cstdlib>

int main ()
{
#ifndef CLASSIC
	curl_global_init (CURL_GLOBAL_ALL);
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

	// Keep input frames distinct from window and popup backgrounds.
	style.FrameBorderSize                 = 1.0f;
	style.Colors[ImGuiCol_FrameBg]        = ImVec4 (0.30f, 0.30f, 0.34f, 1.0f);
	style.Colors[ImGuiCol_FrameBgHovered] = ImVec4 (0.34f, 0.42f, 0.52f, 1.0f);
	style.Colors[ImGuiCol_FrameBgActive]  = ImVec4 (0.38f, 0.50f, 0.66f, 1.0f);
#endif

	auto server = FtpServer::create ();

	while (!server->quit () && platform::loop ())
	{
		server->draw ();

		platform::render ();
	}

	// clean up resources before exiting switch/3ds services
	server.reset ();

	platform::exit ();

#ifndef CLASSIC
	ImGui::DestroyContext ();
	curl_global_cleanup ();
#endif
}
