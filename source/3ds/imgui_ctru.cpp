// ftpd is a server implementation based on the following:
// - RFC  959 (https://tools.ietf.org/html/rfc959)
// - RFC 3659 (https://tools.ietf.org/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
// - Deflate transmission mode for FTP
//   (https://tools.ietf.org/html/draft-preston-ftpext-deflate-04)
//
// The MIT License (MIT)
//
// Copyright (C) 2025 Michael Theall
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef CLASSIC
#include "imgui_ctru.h"

#include <imgui.h>

#include <imgui_internal.h>

#include "fs.h"
#include "platform.h"

#include <chrono>
#include <cstring>
#include <functional>
#include <string>
#include <tuple>
using namespace std::chrono_literals;

#undef keysDown
#undef keysUp

namespace
{
/// \brief Clipboard
std::string s_clipboard;

/// \brief Get clipboard text callback
/// \param context_ ImGui context
char const *getClipboardText (ImGuiContext *const context_)
{
	(void)context_;
	return s_clipboard.c_str ();
}

/// \brief Set clipboard text callback
/// \param context_ ImGui context
/// \param text_ Clipboard text
void setClipboardText (ImGuiContext *const context_, char const *const text_)
{
	(void)context_;
	s_clipboard = text_;
}

/// \brief Update touch position
/// \param io_ ImGui IO
void updateTouch (ImGuiIO &io_)
{
	// check if touchpad was released
	if (hidKeysUp () & KEY_TOUCH)
	{
		// keep mouse position for one frame for release event
		io_.AddMouseButtonEvent (0, false);
		return;
	}

	// check if touchpad is touched
	if (!(hidKeysHeld () & KEY_TOUCH))
	{
		// set mouse cursor off-screen
		io_.AddMousePosEvent (-10.0f, -10.0f);
		io_.AddMouseButtonEvent (0, false);
		return;
	}

	// read touch position
	touchPosition pos;
	hidTouchRead (&pos);

	// transform to bottom-screen space
	io_.AddMousePosEvent (pos.px + 40.0f, pos.py + 240.0f);
	io_.AddMouseButtonEvent (0, true);
}

/// \brief Update gamepad inputs
/// \param io_ ImGui IO
void updateGamepads (ImGuiIO &io_)
{
	auto const buttonMapping = {
	    // clang-format off
	    std::make_pair (KEY_A,      ImGuiKey_GamepadFaceDown),  // A and B are swapped
	    std::make_pair (KEY_B,      ImGuiKey_GamepadFaceRight), // this is more intuitive
	    std::make_pair (KEY_X,      ImGuiKey_GamepadFaceUp),
	    std::make_pair (KEY_Y,      ImGuiKey_GamepadFaceLeft),
	    std::make_pair (KEY_L,      ImGuiKey_GamepadL1),
	    std::make_pair (KEY_ZL,     ImGuiKey_GamepadL1),
	    std::make_pair (KEY_R,      ImGuiKey_GamepadR1),
	    std::make_pair (KEY_ZR,     ImGuiKey_GamepadR1),
	    std::make_pair (KEY_DUP,    ImGuiKey_GamepadDpadUp),
	    std::make_pair (KEY_DRIGHT, ImGuiKey_GamepadDpadRight),
	    std::make_pair (KEY_DDOWN,  ImGuiKey_GamepadDpadDown),
	    std::make_pair (KEY_DLEFT,  ImGuiKey_GamepadDpadLeft),
	    // clang-format on
	};

	// read buttons from 3DS
	auto const keysDown = hidKeysDown ();
	auto const keysUp   = hidKeysUp ();
	for (auto const &[in, out] : buttonMapping)
	{
		if (keysUp & in)
			io_.AddKeyEvent (out, false);
		else if (keysDown & in)
			io_.AddKeyEvent (out, true);
	}

	// update joystick
	circlePosition cpad;
	auto const analogMapping = {
	    // clang-format off
	    std::make_tuple (std::ref (cpad.dx), ImGuiKey_GamepadLStickLeft,  -0.3f, -0.9f),
	    std::make_tuple (std::ref (cpad.dx), ImGuiKey_GamepadLStickRight, +0.3f, +0.9f),
	    std::make_tuple (std::ref (cpad.dy), ImGuiKey_GamepadLStickUp,    +0.3f, +0.9f),
	    std::make_tuple (std::ref (cpad.dy), ImGuiKey_GamepadLStickDown,  -0.3f, -0.9f),
	    // clang-format on
	};

	// read left joystick from circle pad
	hidCircleRead (&cpad);
	for (auto const &[in, out, min, max] : analogMapping)
	{
		auto const value = std::clamp ((in / 156.0f - min) / (max - min), 0.0f, 1.0f);
		io_.AddKeyAnalogEvent (out, value > 0.1f, value);
	}
}

/// \brief Update keyboard inputs
/// \param io_ ImGui IO
void updateKeyboard (ImGuiIO &io_)
{
	static enum {
		INACTIVE,
		KEYBOARD,
		CLEARED,
	} state = INACTIVE;

	switch (state)
	{
	case INACTIVE:
	{
		if (!io_.WantTextInput)
			return;

		auto &textState = ImGui::GetCurrentContext ()->InputTextState;

		SwkbdState kbd;

		swkbdInit (&kbd, SWKBD_TYPE_NORMAL, 2, -1);
		swkbdSetButton (&kbd, SWKBD_BUTTON_LEFT, "Cancel", false);
		swkbdSetButton (&kbd, SWKBD_BUTTON_RIGHT, "OK", true);
		swkbdSetInitialText (&kbd, textState.TextToRevertTo.Data);

		if (textState.Flags & ImGuiInputTextFlags_Password)
			swkbdSetPasswordMode (&kbd, SWKBD_PASSWORD_HIDE_DELAY);

		char buffer[32]   = {0};
		auto const button = swkbdInputText (&kbd, buffer, sizeof (buffer));
		if (button == SWKBD_BUTTON_RIGHT)
		{
			if (!buffer[0])
			{
				io_.AddKeyEvent (ImGuiKey_Backspace, true);
				io_.AddKeyEvent (ImGuiKey_Backspace, false);
			}
			else
				io_.AddInputCharactersUTF8 (buffer);
		}

		state = KEYBOARD;
		break;
	}

	case KEYBOARD:
		// need to wait until input events are completed
		if (io_.Ctx->InputEventsQueue.Size > 0)
			break;

		ImGui::ClearActiveID ();
		state = CLEARED;
		break;

	case CLEARED:
		state = INACTIVE;
		break;
	}
}
}

bool imgui::ctru::init ()
{
	auto &io = ImGui::GetIO ();

	// setup config flags
	io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

	// setup platform backend
	io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
	io.BackendPlatformName = "3DS";

	// disable mouse cursor
	io.MouseDrawCursor = false;

	auto &platformIO = ImGui::GetPlatformIO ();

	// clipboard callbacks
	platformIO.Platform_SetClipboardTextFn = &setClipboardText;
	platformIO.Platform_GetClipboardTextFn = &getClipboardText;
	platformIO.Platform_ClipboardUserData  = nullptr;

	return true;
}

void imgui::ctru::newFrame ()
{
	auto &io = ImGui::GetIO ();

	// check that font was built
	IM_ASSERT (io.Fonts->IsBuilt () &&
	           "Font atlas not built! It is generally built by the renderer back-end. Missing call "
	           "to renderer _NewFrame() function?");

	// time step
	static auto const start = platform::steady_clock::now ();
	static auto prev        = start;
	auto const now          = platform::steady_clock::now ();

	io.DeltaTime = std::chrono::duration<float> (now - prev).count ();
	prev         = now;

	updateTouch (io);
	updateGamepads (io);
	updateKeyboard (io);
}
#endif
