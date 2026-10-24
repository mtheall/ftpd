// ftpd is a server implementation based on the following:
// - RFC  959 (https://tools.ietf.org/html/rfc959)
// - RFC 3659 (https://tools.ietf.org/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
// - Deflate transmission mode for FTP
//   (https://tools.ietf.org/html/draft-preston-ftpext-deflate-04)
//
// The MIT License (MIT)
//
// Copyright (C) 2026 Michael Theall
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
#include "imgui_nx.h"

#include <imgui.h>
#include <imgui_internal.h>

#include "fs.h"
#include "platform.h"

#include <chrono>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <tuple>
using namespace std::chrono_literals;

namespace
{
/// \brief Last mouse update timestamp
std::chrono::steady_clock::time_point s_lastMouseUpdate;

/// \brief Whether application is focused
bool s_focused = true;

/// \brief Framebuffer width
float s_width = 1280.0f;
/// \brief Framebuffer height
float s_height = 720.0f;

/// \brief Whether to show mouse
float s_showMouse = false;
/// \brief Mouse position
ImVec2 s_mousePos = ImVec2 (0.0f, 0.0f);

/// \brief Clipboard
std::string s_clipboard;

/// \brief Applet hook cookie
AppletHookCookie s_appletHookCookie;

std::map<HidKeyboardKey, ImGuiKey> const s_keyMap = {
    // clang-format off
    { HidKeyboardKey_A,                ImGuiKey_A              },
    { HidKeyboardKey_B,                ImGuiKey_B              },
    { HidKeyboardKey_C,                ImGuiKey_C              },
    { HidKeyboardKey_D,                ImGuiKey_D              },
    { HidKeyboardKey_E,                ImGuiKey_E              },
    { HidKeyboardKey_F,                ImGuiKey_F              },
    { HidKeyboardKey_G,                ImGuiKey_G              },
    { HidKeyboardKey_H,                ImGuiKey_H              },
    { HidKeyboardKey_I,                ImGuiKey_I              },
    { HidKeyboardKey_J,                ImGuiKey_J              },
    { HidKeyboardKey_K,                ImGuiKey_K              },
    { HidKeyboardKey_L,                ImGuiKey_L              },
    { HidKeyboardKey_M,                ImGuiKey_M              },
    { HidKeyboardKey_N,                ImGuiKey_N              },
    { HidKeyboardKey_O,                ImGuiKey_O              },
    { HidKeyboardKey_P,                ImGuiKey_P              },
    { HidKeyboardKey_Q,                ImGuiKey_Q              },
    { HidKeyboardKey_R,                ImGuiKey_R              },
    { HidKeyboardKey_S,                ImGuiKey_S              },
    { HidKeyboardKey_T,                ImGuiKey_T              },
    { HidKeyboardKey_U,                ImGuiKey_U              },
    { HidKeyboardKey_V,                ImGuiKey_V              },
    { HidKeyboardKey_W,                ImGuiKey_W              },
    { HidKeyboardKey_X,                ImGuiKey_X              },
    { HidKeyboardKey_Y,                ImGuiKey_Y              },
    { HidKeyboardKey_Z,                ImGuiKey_Z              },
    { HidKeyboardKey_D1,               ImGuiKey_1              },
    { HidKeyboardKey_D2,               ImGuiKey_2              },
    { HidKeyboardKey_D3,               ImGuiKey_3              },
    { HidKeyboardKey_D4,               ImGuiKey_4              },
    { HidKeyboardKey_D5,               ImGuiKey_5              },
    { HidKeyboardKey_D6,               ImGuiKey_6              },
    { HidKeyboardKey_D7,               ImGuiKey_7              },
    { HidKeyboardKey_D8,               ImGuiKey_8              },
    { HidKeyboardKey_D9,               ImGuiKey_9              },
    { HidKeyboardKey_D0,               ImGuiKey_0              },
    { HidKeyboardKey_Return,           ImGuiKey_Enter          },
    { HidKeyboardKey_Escape,           ImGuiKey_Escape         },
    { HidKeyboardKey_Backspace,        ImGuiKey_Backspace      },
    { HidKeyboardKey_Tab,              ImGuiKey_Tab            },
    { HidKeyboardKey_Space,            ImGuiKey_Space          },
    { HidKeyboardKey_Minus,            ImGuiKey_Minus          },
    { HidKeyboardKey_OpenBracket,      ImGuiKey_LeftBracket    },
    { HidKeyboardKey_CloseBracket,     ImGuiKey_RightBracket   },
    { HidKeyboardKey_Semicolon,        ImGuiKey_Semicolon      },
    { HidKeyboardKey_Quote,            ImGuiKey_Apostrophe     },
    { HidKeyboardKey_Backquote,        ImGuiKey_GraveAccent    },
    { HidKeyboardKey_Comma,            ImGuiKey_Comma          },
    { HidKeyboardKey_Period,           ImGuiKey_Period         },
    { HidKeyboardKey_Slash,            ImGuiKey_Slash          },
    { HidKeyboardKey_CapsLock,         ImGuiKey_CapsLock       },
    { HidKeyboardKey_F1,               ImGuiKey_F1             },
    { HidKeyboardKey_F2,               ImGuiKey_F2             },
    { HidKeyboardKey_F3,               ImGuiKey_F3             },
    { HidKeyboardKey_F4,               ImGuiKey_F4             },
    { HidKeyboardKey_F5,               ImGuiKey_F5             },
    { HidKeyboardKey_F6,               ImGuiKey_F6             },
    { HidKeyboardKey_F7,               ImGuiKey_F7             },
    { HidKeyboardKey_F8,               ImGuiKey_F8             },
    { HidKeyboardKey_F9,               ImGuiKey_F9             },
    { HidKeyboardKey_F10,              ImGuiKey_F10            },
    { HidKeyboardKey_F11,              ImGuiKey_F11            },
    { HidKeyboardKey_F12,              ImGuiKey_F12            },
    { HidKeyboardKey_PrintScreen,      ImGuiKey_PrintScreen    },
    { HidKeyboardKey_ScrollLock,       ImGuiKey_ScrollLock     },
    { HidKeyboardKey_Pause,            ImGuiKey_Pause          },
    { HidKeyboardKey_Insert,           ImGuiKey_Insert         },
    { HidKeyboardKey_Home,             ImGuiKey_Home           },
    { HidKeyboardKey_PageUp,           ImGuiKey_PageUp         },
    { HidKeyboardKey_Delete,           ImGuiKey_Delete         },
    { HidKeyboardKey_End,              ImGuiKey_End            },
    { HidKeyboardKey_PageDown,         ImGuiKey_PageDown       },
    { HidKeyboardKey_RightArrow,       ImGuiKey_RightArrow     },
    { HidKeyboardKey_LeftArrow,        ImGuiKey_LeftArrow      },
    { HidKeyboardKey_DownArrow,        ImGuiKey_DownArrow      },
    { HidKeyboardKey_UpArrow,          ImGuiKey_UpArrow        },
    { HidKeyboardKey_NumLock,          ImGuiKey_NumLock        },
    { HidKeyboardKey_NumPadDivide,     ImGuiKey_KeypadDivide   },
    { HidKeyboardKey_NumPadMultiply,   ImGuiKey_KeypadMultiply },
    { HidKeyboardKey_NumPadSubtract,   ImGuiKey_KeypadSubtract },
    { HidKeyboardKey_NumPadAdd,        ImGuiKey_KeypadAdd      },
    { HidKeyboardKey_NumPadEnter,      ImGuiKey_KeypadEnter    },
    { HidKeyboardKey_NumPad1,          ImGuiKey_Keypad1        },
    { HidKeyboardKey_NumPad2,          ImGuiKey_Keypad2        },
    { HidKeyboardKey_NumPad3,          ImGuiKey_Keypad3        },
    { HidKeyboardKey_NumPad4,          ImGuiKey_Keypad4        },
    { HidKeyboardKey_NumPad5,          ImGuiKey_Keypad5        },
    { HidKeyboardKey_NumPad6,          ImGuiKey_Keypad6        },
    { HidKeyboardKey_NumPad7,          ImGuiKey_Keypad7        },
    { HidKeyboardKey_NumPad8,          ImGuiKey_Keypad8        },
    { HidKeyboardKey_NumPad9,          ImGuiKey_Keypad9        },
    { HidKeyboardKey_NumPad0,          ImGuiKey_Keypad0        },
    { HidKeyboardKey_NumPadDot,        ImGuiKey_KeypadDecimal  },
    { HidKeyboardKey_Backslash,        ImGuiKey_Backslash      },
    { HidKeyboardKey_NumPadEquals,     ImGuiKey_KeypadEqual    },
    { HidKeyboardKey_LeftControl,      ImGuiKey_LeftCtrl       },
    { HidKeyboardKey_LeftShift,        ImGuiKey_LeftShift      },
    { HidKeyboardKey_LeftAlt,          ImGuiKey_LeftAlt        },
    { HidKeyboardKey_LeftGui,          ImGuiKey_LeftSuper      },
    { HidKeyboardKey_RightControl,     ImGuiKey_RightCtrl      },
    { HidKeyboardKey_RightShift,       ImGuiKey_RightShift     },
    { HidKeyboardKey_RightAlt,         ImGuiKey_RightAlt       },
    { HidKeyboardKey_RightGui,         ImGuiKey_RightSuper     },
    //{ HidKeyboardKey_Plus,             ImGuiKey_ },
    //{ HidKeyboardKey_Pipe,             ImGuiKey_ },
    //{ HidKeyboardKey_Tilde,            ImGuiKey_ },
    //{ HidKeyboardKey_Application,      ImGuiKey_ },
    //{ HidKeyboardKey_Power,            ImGuiKey_ },
    //{ HidKeyboardKey_F13,              ImGuiKey_ },
    //{ HidKeyboardKey_F14,              ImGuiKey_ },
    //{ HidKeyboardKey_F15,              ImGuiKey_ },
    //{ HidKeyboardKey_F16,              ImGuiKey_ },
    //{ HidKeyboardKey_F17,              ImGuiKey_ },
    //{ HidKeyboardKey_F18,              ImGuiKey_ },
    //{ HidKeyboardKey_F19,              ImGuiKey_ },
    //{ HidKeyboardKey_F20,              ImGuiKey_ },
    //{ HidKeyboardKey_F21,              ImGuiKey_ },
    //{ HidKeyboardKey_F22,              ImGuiKey_ },
    //{ HidKeyboardKey_F23,              ImGuiKey_ },
    //{ HidKeyboardKey_F24,              ImGuiKey_ },
    //{ HidKeyboardKey_NumPadComma,      ImGuiKey_ },
    //{ HidKeyboardKey_Ro,               ImGuiKey_ },
    //{ HidKeyboardKey_KatakanaHiragana, ImGuiKey_ },
    //{ HidKeyboardKey_Yen,              ImGuiKey_ },
    //{ HidKeyboardKey_Henkan,           ImGuiKey_ },
    //{ HidKeyboardKey_Muhenkan,         ImGuiKey_ },
    //{ HidKeyboardKey_NumPadCommaPc98,  ImGuiKey_ },
    //{ HidKeyboardKey_HangulEnglish,    ImGuiKey_ },
    //{ HidKeyboardKey_Hanja,            ImGuiKey_ },
    //{ HidKeyboardKey_Katakana,         ImGuiKey_ },
    //{ HidKeyboardKey_Hiragana,         ImGuiKey_ },
    //{ HidKeyboardKey_ZenkakuHankaku,   ImGuiKey_ },
    // clang-format on
};

/// \brief Handle applet hook
/// \param hook_ Callback reason
/// \param param_ User param
void handleAppletHook (AppletHookType const hook_, void *const param_)
{
	(void)param_;
	switch (hook_)
	{
	case AppletHookType_OnFocusState:
		// grab focus state
		s_focused = (appletGetFocusState () == AppletFocusState_InFocus);
		break;

	case AppletHookType_OnOperationMode:
		switch (appletGetOperationMode ())
		{
		default:
		case AppletOperationMode_Handheld:
			// use handheld mode resolution (720p)
			s_width  = 1280.0f;
			s_height = 720.0f;
			break;

		case AppletOperationMode_Console:
			// use docked mode resolution (1080p)
			s_width  = 1920.0f;
			s_height = 1080.0f;
			break;
		}
		break;

	default:
		break;
	}
}

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

/// \brief Move mouse cursor
/// \param io_ ImGui IO
/// \param pos_ New mouse position
/// \param force_ Whether to ignore prior mouse position
void moveMouse (ImGuiIO &io_, ImVec2 const &pos_, bool const force_ = false)
{
	(void)io_;

	// get update timestamp
	auto const now = std::chrono::steady_clock::now ();

	// check if the mouse position has been updated
	if (!force_ && pos_.x == s_mousePos.x && pos_.y == s_mousePos.y)
	{
		// stop displaying mouse cursor after inactivity timeout
		if (now - s_lastMouseUpdate > 1s)
			s_showMouse = false;

		return;
	}

	// update mouse position
	s_showMouse       = true;
	s_lastMouseUpdate = now;
	s_mousePos        = pos_;
}

/// \brief Update mouse buttons
/// \param mouseState_ Mouse state
/// \param io_ ImGui IO
void updateMouseButtons (HidMouseState const &mouseState_, ImGuiIO &io_)
{
	static HidMouseState prevMouseState;

	auto const diff = prevMouseState.buttons ^ mouseState_.buttons;

	prevMouseState = mouseState_;

	// read mouse buttons
	for (std::size_t i = 0;
	     i < IM_ARRAYSIZE (io_.MouseDown) && i < sizeof (HidMouseState::buttons) * CHAR_BIT;
	     ++i)
	{
		if (!(diff & BIT (i)))
			continue;

		io_.AddMouseButtonEvent (i, mouseState_.buttons & BIT (i));

		// force mouse cursor to show on click
		if (mouseState_.buttons & BIT (i))
			moveMouse (io_, s_mousePos, true);
	}
}

/// \brief Update mouse position
/// \param mouseState_ Mouse state
/// \param io_ ImGui IO
void updateMousePos (HidMouseState const &mouseState_, ImGuiIO &io_)
{
	if (mouseState_.wheel_delta_x || mouseState_.wheel_delta_y)
		io_.AddMouseWheelEvent (mouseState_.wheel_delta_x, mouseState_.wheel_delta_y);

	moveMouse (io_,
	    ImVec2 (
	        s_mousePos.x + 2.0f * mouseState_.delta_x, s_mousePos.y + 2.0f * mouseState_.delta_y));
}

/// \brief Update touch position
/// \param touchState_ Touch screen state
/// \param io_ ImGui IO
void updateTouch (HidTouchScreenState const &touchState_, ImGuiIO &io_)
{
	static HidTouchScreenState prevTouchState;

	if (prevTouchState.count > 0 && touchState_.count < 1)
		io_.AddMouseButtonEvent (0, false);
	else if (prevTouchState.count < 1 && touchState_.count > 0)
		io_.AddMouseButtonEvent (0, true);

	prevTouchState = touchState_;

	if (touchState_.count < 1)
		return;

	// set mouse position to touch point; force hide mouse cursor
	moveMouse (io_, ImVec2 (touchState_.touches[0].x, touchState_.touches[0].y));
	s_showMouse = false;
}

/// \brief Update gamepad inputs
/// \param padState_ Gamepad state
/// \param io_ ImGui IO
void updateGamepads (PadState const &padState_, ImGuiIO &io_)
{
	auto const buttonMapping = {
	    // clang-format off
	    std::make_pair (HidNpadButton_A,     ImGuiKey_GamepadFaceDown),  // A and B are swapped
	    std::make_pair (HidNpadButton_B,     ImGuiKey_GamepadFaceRight), // this is more intuitive
	    std::make_pair (HidNpadButton_X,     ImGuiKey_GamepadFaceUp),
	    std::make_pair (HidNpadButton_Y,     ImGuiKey_GamepadFaceLeft),
	    std::make_pair (HidNpadButton_L,     ImGuiKey_GamepadL1),
	    std::make_pair (HidNpadButton_R,     ImGuiKey_GamepadR1),
	    std::make_pair (HidNpadButton_Up,    ImGuiKey_GamepadDpadUp),
	    std::make_pair (HidNpadButton_Right, ImGuiKey_GamepadDpadRight),
	    std::make_pair (HidNpadButton_Down,  ImGuiKey_GamepadDpadDown),
	    std::make_pair (HidNpadButton_Left,  ImGuiKey_GamepadDpadLeft),
	    // clang-format on
	};

	// read buttons from primary controller
	auto const keys     = padGetButtons (&padState_);
	auto const keysDown = padGetButtonsDown (&padState_);
	auto const keysUp   = padGetButtonsUp (&padState_);
	for (auto const &[in, out] : buttonMapping)
	{
		if (keysUp & in)
			io_.AddKeyEvent (out, false);
		else if (keysDown & in)
			io_.AddKeyEvent (out, true);
	}

	// use ZR/ZL as left-click/right-click, respectively
	if ((keysDown | keysUp) & HidNpadButton_ZR)
	{
		io_.AddMouseButtonEvent (0, keysDown & HidNpadButton_ZR);
		moveMouse (io_, s_mousePos, true);
	}
	if ((keysDown | keysUp) & HidNpadButton_ZL)
	{
		io_.AddMouseButtonEvent (1, keysDown & HidNpadButton_ZL);
		moveMouse (io_, s_mousePos, true);
	}

	// update joystick
	auto const jsLeft = padGetStickPos (&padState_, 0);

	auto const analogMapping = {
	    // clang-format off
	    std::make_tuple (std::ref (jsLeft.x), ImGuiKey_GamepadLStickLeft,  -0.3f, -0.9f),
	    std::make_tuple (std::ref (jsLeft.x), ImGuiKey_GamepadLStickRight, +0.3f, +0.9f),
	    std::make_tuple (std::ref (jsLeft.y), ImGuiKey_GamepadLStickUp,    +0.3f, +0.9f),
	    std::make_tuple (std::ref (jsLeft.y), ImGuiKey_GamepadLStickDown,  -0.3f, -0.9f),
	    // clang-format on
	};

	// read left joystick from primary controller
	for (auto const &[in, out, min, max] : analogMapping)
	{
		auto const value = std::clamp ((in / JOYSTICK_MAX - min) / (max - min), 0.0f, 1.0f);
		io_.AddKeyAnalogEvent (out, value > 0.1f, value);
	}

	// use right stick as mouse
	auto scale = 5.0f;
	if (keys & HidNpadButton_L)
		scale = 1.0f;
	if (keys & HidNpadButton_R)
		scale = 20.0f;

	auto const jsRight = padGetStickPos (&padState_, 1);

	// move mouse
	moveMouse (io_,
	    ImVec2 (s_mousePos.x + jsRight.x / static_cast<float> (JOYSTICK_MAX) * scale,
	        s_mousePos.y - jsRight.y / static_cast<float> (JOYSTICK_MAX) * scale));
}

/// \brief Update keyboard inputs
/// \param kbState_ Keyboard state
/// \param io_ ImGui IO
void updateKeyboard (HidKeyboardState const &kbState_, ImGuiIO &io_)
{
	static HidKeyboardState prevKbState;

	auto const modDiff = prevKbState.modifiers ^ kbState_.modifiers;

	if (modDiff & HidKeyboardModifier_Control)
		io_.AddKeyEvent (ImGuiMod_Ctrl, kbState_.modifiers & HidKeyboardModifier_Control);

	if (modDiff & HidKeyboardModifier_Shift)
		io_.AddKeyEvent (ImGuiMod_Shift, kbState_.modifiers & HidKeyboardModifier_Shift);

	if (modDiff & (HidKeyboardModifier_LeftAlt | HidKeyboardModifier_RightAlt))
		io_.AddKeyEvent (ImGuiMod_Alt,
		    kbState_.modifiers & (HidKeyboardModifier_LeftAlt | HidKeyboardModifier_RightAlt));

	if (modDiff & HidKeyboardModifier_Gui)
		io_.AddKeyEvent (ImGuiMod_Super, kbState_.modifiers & HidKeyboardModifier_Gui);

	for (std::size_t i = 0; i < sizeof (HidKeyboardState::keys) * CHAR_BIT; ++i)
	{
		auto const key = static_cast<HidKeyboardKey> (i);

		auto const it = s_keyMap.find (key);
		if (it == std::end (s_keyMap))
			continue;

		if (hidKeyboardStateGetKey (&prevKbState, key) != hidKeyboardStateGetKey (&kbState_, key))
			io_.AddKeyEvent (it->second, hidKeyboardStateGetKey (&kbState_, key));
	}

	prevKbState = kbState_;

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

		SwkbdConfig kbd;
		swkbdCreate (&kbd, 0);
		swkbdConfigMakePresetDefault (&kbd);
		swkbdConfigSetInitialText (&kbd,
		    std::string (textState.TextToRevertTo.Data, textState.TextToRevertTo.Size).c_str ());

		char buffer[32];
		if (R_SUCCEEDED (swkbdShow (&kbd, buffer, sizeof (buffer))))
			io_.AddInputCharactersUTF8 (buffer);

		state = KEYBOARD;
		break;
	}

	case KEYBOARD:
		// need to skip a frame for active id to really be cleared
		ImGui::ClearActiveID ();
		state = CLEARED;
		break;

	case CLEARED:
		state = INACTIVE;
		break;
	}
}
}

bool imgui::nx::init ()
{
	auto &io = ImGui::GetIO ();

	// get system language
	u64 languageCode;
	auto rc = setInitialize ();
	if (R_FAILED (rc))
	{
		std::fprintf (stderr, "setInitialize: 0x%x\n", rc);
		return false;
	}

	rc = setGetSystemLanguage (&languageCode);
	if (R_FAILED (rc))
	{
		std::fprintf (stderr, "setGetSystemLanguage: 0x%x\n", rc);
		setExit ();
		return false;
	}
	setExit ();

	// get fonts for system language
	std::vector<PlFontData> fonts (PlSharedFontType_Total);
	s32 numFonts = 0;
	rc           = plGetSharedFont (languageCode, fonts.data (), fonts.size (), &numFonts);
	if (R_FAILED (rc))
	{
		std::fprintf (stderr, "plGetSharedFont: 0x%x\n", rc);
		return false;
	}
	fonts.resize (numFonts);

	// AddFontFromMemoryTTF creates the atlas, so choose its format first.
	io.Fonts->TexDesiredFormat = ImTextureFormat_Alpha8;
	io.Fonts->Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;

	// add fonts
	ImFontConfig config;
	config.MergeMode            = false;
	config.FontDataOwnedByAtlas = false;
	for (auto const &font : fonts)
	{
		io.Fonts->AddFontFromMemoryTTF (font.address, font.size, 24.0f, &config);
		config.MergeMode = true;
	}

	// initialize applet hooks
	appletSetFocusHandlingMode (AppletFocusHandlingMode_NoSuspend);
	appletHook (&s_appletHookCookie, handleAppletHook, nullptr);
	handleAppletHook (AppletHookType_OnFocusState, nullptr);
	handleAppletHook (AppletHookType_OnOperationMode, nullptr);

	// disable imgui.ini file
	io.IniFilename = nullptr;

	// setup config flags
	io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	// setup platform backend
	io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
	io.BackendPlatformName = "Switch";

	// initially disable mouse cursor
	io.MouseDrawCursor = false;

	auto &platformIO = ImGui::GetPlatformIO ();

	// clipboard callbacks
	platformIO.Platform_SetClipboardTextFn = &setClipboardText;
	platformIO.Platform_GetClipboardTextFn = &getClipboardText;
	platformIO.Platform_ClipboardUserData  = nullptr;

	return true;
}

void imgui::nx::newFrame (PadState const *const padState_,
    HidTouchScreenState const *const touchState_,
    HidMouseState const *const mouseState_,
    HidKeyboardState const *const kbState_)
{
	auto &io = ImGui::GetIO ();

	// setup display metrics
	io.DisplaySize             = ImVec2 (s_width, s_height);
	io.DisplayFramebufferScale = ImVec2 (1.0f, 1.0f);

	// time step
	static auto const start = platform::steady_clock::now ();
	static auto prev        = start;
	auto const now          = platform::steady_clock::now ();

	io.DeltaTime = std::chrono::duration<float> (now - prev).count ();
	prev         = now;

	if (s_focused)
	{
		// update inputs
		if (mouseState_)
		{
			updateMouseButtons (*mouseState_, io);
			updateMousePos (*mouseState_, io);
		}

		if (touchState_)
			updateTouch (*touchState_, io);

		if (padState_)
			updateGamepads (*padState_, io);

		if (kbState_)
			updateKeyboard (*kbState_, io);
	}

	// whether to draw mouse cursor
	io.MouseDrawCursor = s_showMouse;

	// clamp mouse to screen
	s_mousePos.x = std::clamp (s_mousePos.x, 0.0f, s_width);
	s_mousePos.y = std::clamp (s_mousePos.y, 0.0f, s_height);
	io.MousePos  = s_mousePos;
}

void imgui::nx::exit ()
{
	// deinitialize applet hooks
	appletUnhook (&s_appletHookCookie);
}
#endif
