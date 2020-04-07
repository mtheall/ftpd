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

#include "fs.h"
#include "log.h"

#include "imgui_citro3d.h"
#include "imgui_ctru.h"

#include "imgui.h"

#include <3ds.h>
#include <citro3d.h>
#include <tex3ds.h>

#include "gfx.h"

#include <cassert>
#include <chrono>
#include <ctime>
#include <malloc.h>

namespace
{
/// \brief Thread stack size
constexpr auto STACK_SIZE = 0x8000;
/// \brief soc:u buffer alignment
constexpr auto SOCU_ALIGN = 0x1000;
/// \brief soc:u buffer size
constexpr auto SOCU_BUFFERSIZE = 0x100000;

static_assert (SOCU_BUFFERSIZE % SOCU_ALIGN == 0);

/// \brief Whether soc:u is active
bool s_socuActive = false;
/// \brief soc:u buffer
u32 *s_socuBuffer = nullptr;

/// \brief Texture atlas
C3D_Tex s_gfxTexture;
/// \brief Texture atlas metadata
Tex3DS_Texture s_gfxT3x;

/// \brief Start network
void startNetwork ()
{
	// check if already active
	if (s_socuActive)
		return;

	// get wifi status
	std::uint32_t wifi = 0;
	if (R_FAILED (ACU_GetWifiStatus (&wifi)) || !wifi)
		return;

	// allocate soc:u buffer
	if (!s_socuBuffer)
		s_socuBuffer = static_cast<u32 *> (::memalign (SOCU_ALIGN, SOCU_BUFFERSIZE));

	if (!s_socuBuffer)
		return;

	// initialize soc:u service
	if (R_FAILED (socInit (s_socuBuffer, SOCU_BUFFERSIZE)))
		return;

	s_socuActive = true;
	Log::info ("Wifi connected\n");
}

/// \brief Draw citro3d logo
void drawLogo ()
{
	// get citro3d logo subtexture
	auto subTex = Tex3DS_GetSubTexture (s_gfxT3x, gfx_c3dlogo_idx);

	// get framebuffer metrics
	ImGuiIO &io             = ImGui::GetIO ();
	auto const screenWidth  = io.DisplaySize.x;
	auto const screenHeight = io.DisplaySize.y;
	auto const logoWidth    = subTex->width;
	auto const logoHeight   = subTex->height;

	// calculate top screen coords
	auto const x1 = (screenWidth - logoWidth) / 2.0f;
	auto const x2 = x1 + logoWidth;
	auto const y1 = (screenHeight / 2.0f - logoHeight) / 2.0f;
	auto const y2 = y1 + logoHeight;

	// calculate uv coords
	auto const uv1 = ImVec2 (subTex->left, subTex->top);
	auto const uv2 = ImVec2 (subTex->right, subTex->bottom);

	// draw to top screen
	ImGui::GetBackgroundDrawList ()->AddImage (
	    &s_gfxTexture, ImVec2 (x1, y1), ImVec2 (x2, y2), uv1, uv2);

	// draw to bottom screen
	ImGui::GetBackgroundDrawList ()->AddImage (&s_gfxTexture,
	    ImVec2 (x1, y1 + screenHeight / 2.0f),
	    ImVec2 (x2, y2 + screenHeight / 2.0f),
	    uv1,
	    uv2);
}

/// \brief Draw status
void drawStatus ()
{
	constexpr unsigned batteryLevels[] = {
	    gfx_battery0_idx,
	    gfx_battery0_idx,
	    gfx_battery1_idx,
	    gfx_battery2_idx,
	    gfx_battery3_idx,
	    gfx_battery4_idx,
	};

	constexpr unsigned wifiLevels[] = {
	    gfx_wifiNull_idx,
	    gfx_wifi1_idx,
	    gfx_wifi2_idx,
	    gfx_wifi3_idx,
	};

	// get battery charging state or level
	static u8 charging = 0;
	static u8 level    = 5;
	PTMU_GetBatteryChargeState (&charging);
	if (!charging)
	{
		PTMU_GetBatteryLevel (&level);
		if (level >= std::extent_v<decltype (batteryLevels)>)
			svcBreak (USERBREAK_PANIC);
	}

	auto const &io    = ImGui::GetIO ();
	auto const &style = ImGui::GetStyle ();

	auto const screenWidth = io.DisplaySize.x;

	// calculate battery icon metrics
	auto const battery =
	    Tex3DS_GetSubTexture (s_gfxT3x, charging ? gfx_batteryCharge_idx : batteryLevels[level]);
	auto const batteryWidth  = battery->width;
	auto const batteryHeight = battery->height;

	// calculate battery icon position
	auto const p1 = ImVec2 (screenWidth - batteryWidth, 0.0f);
	auto const p2 = ImVec2 (screenWidth, batteryHeight);

	// calculate battery icon uv coords
	auto const uv1 = ImVec2 (battery->left, battery->top);
	auto const uv2 = ImVec2 (battery->right, battery->bottom);

	// draw battery icon
	ImGui::GetForegroundDrawList ()->AddImage (&s_gfxTexture, p1, p2, uv1, uv2);

	// get wifi strength
	auto const wifiStrength = osGetWifiStrength ();

	// calculate wifi icon metrics
	auto const wifi       = Tex3DS_GetSubTexture (s_gfxT3x, wifiLevels[wifiStrength]);
	auto const wifiWidth  = wifi->width;
	auto const wifiHeight = wifi->height;

	// calculate wifi icon position
	auto const p3 = ImVec2 (p1.x - wifiWidth - 2.0f, 0.0f);
	auto const p4 = ImVec2 (p1.x - 2.0f, wifiHeight);

	// calculate wifi icon uv coords
	auto const uv3 = ImVec2 (wifi->left, wifi->top);
	auto const uv4 = ImVec2 (wifi->right, wifi->bottom);

	// draw wifi icon
	ImGui::GetForegroundDrawList ()->AddImage (&s_gfxTexture, p3, p4, uv3, uv4);

	// draw current timestamp
	char buffer[64];
	auto const now = std::time (nullptr);
	std::strftime (buffer, sizeof (buffer), "%H:%M:%S", std::localtime (&now));
	ImGui::GetForegroundDrawList ()->AddText (
	    ImVec2 (p3.x - 65.0f, style.FramePadding.y), 0xFFFFFFFF, buffer);
}
}

bool platform::init ()
{
	// enable New 3DS speedup
	osSetSpeedupEnable (true);

	acInit ();
	ptmuInit ();
	romfsInit ();
	gfxInitDefault ();
	gfxSet3D (false);
	sdmcWriteSafe (false);

#ifndef NDEBUG
	consoleDebugInit (debugDevice_SVC);
	std::setvbuf (stderr, nullptr, _IOLBF, 0);
#endif

	if (!imgui::ctru::init ())
		return false;

	imgui::citro3d::init ();

	{
		// load texture atlas
		fs::File file;
		if (!file.open ("romfs:/gfx.t3x"))
			svcBreak (USERBREAK_PANIC);

		s_gfxT3x = Tex3DS_TextureImportStdio (file, &s_gfxTexture, nullptr, true);
		if (!s_gfxT3x)
			svcBreak (USERBREAK_PANIC);

		C3D_TexSetFilter (&s_gfxTexture, GPU_LINEAR, GPU_LINEAR);
	}

	// citro3d logo doesn't quite show with the default transparency
	auto &style                       = ImGui::GetStyle ();
	style.Colors[ImGuiCol_WindowBg].w = 0.8f;
	style.ScaleAllSizes (0.5f);

	return true;
}

bool platform::loop ()
{
	if (!aptMainLoop ())
		return false;

	startNetwork ();

	hidScanInput ();

	if (hidKeysDown () & KEY_START)
		return false;

	imgui::ctru::newFrame ();
	imgui::citro3d::newFrame ();
	ImGui::NewFrame ();

	return true;
}

void platform::render ()
{
	drawLogo ();
	drawStatus ();

	ImGui::Render ();

	imgui::citro3d::render ();
}

void platform::exit ()
{
	Tex3DS_TextureFree (s_gfxT3x);
	C3D_TexDelete (&s_gfxTexture);

	imgui::citro3d::exit ();
	imgui::ctru::exit ();

	if (s_socuActive)
		socExit ();
	s_socuActive = false;
	std::free (s_socuBuffer);

	gfxExit ();
	ptmuExit ();
	acExit ();
}

///////////////////////////////////////////////////////////////////////////
/// \brief Platform thread pimpl
class platform::Thread::privateData_t
{
public:
	privateData_t ()
	{
		if (thread)
			threadFree (thread);
	}

	/// \brief Parameterized constructor
	/// \param func_ Thread entry point
	privateData_t (std::function<void ()> &&func_) : thread (nullptr), func (std::move (func_))
	{
		s32 priority = 0x30;
		svcGetThreadPriority (&priority, CUR_THREAD_HANDLE);

		thread = threadCreate (&privateData_t::threadFunc, this, STACK_SIZE, priority, -1, false);
		assert (thread);
	}

	/// \brief Underlying thread entry point
	/// \param arg_ Thread pimpl object
	static void threadFunc (void *const arg_)
	{
		// call passed-in entry point
		auto const t = static_cast<privateData_t *> (arg_);
		t->func ();
	}

	/// \brief Underlying thread
	::Thread thread = nullptr;
	/// \brief Thread entry point
	std::function<void ()> func;
};

///////////////////////////////////////////////////////////////////////////
platform::Thread::~Thread () = default;

platform::Thread::Thread () : m_d (new privateData_t ())
{
}

platform::Thread::Thread (std::function<void ()> &&func_)
    : m_d (new privateData_t (std::move (func_)))
{
}

platform::Thread::Thread (Thread &&that_) : m_d (new privateData_t ())
{
	std::swap (m_d, that_.m_d);
}

platform::Thread &platform::Thread::operator= (Thread &&that_)
{
	std::swap (m_d, that_.m_d);
	return *this;
}

void platform::Thread::join ()
{
	threadJoin (m_d->thread, UINT64_MAX);
}

void platform::Thread::sleep (std::chrono::milliseconds const timeout_)
{
	svcSleepThread (std::chrono::nanoseconds (timeout_).count ());
}

///////////////////////////////////////////////////////////////////////////
/// \brief Platform mutex pimpl
class platform::Mutex::privateData_t
{
public:
	/// \brief Underlying mutex
	LightLock mutex;
};

///////////////////////////////////////////////////////////////////////////
platform::Mutex::~Mutex () = default;

platform::Mutex::Mutex () : m_d (new privateData_t ())
{
	LightLock_Init (&m_d->mutex);
}

void platform::Mutex::lock ()
{
	LightLock_Lock (&m_d->mutex);
}

void platform::Mutex::unlock ()
{
	LightLock_Unlock (&m_d->mutex);
}
