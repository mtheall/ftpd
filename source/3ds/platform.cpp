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
constexpr auto STACK_SIZE      = 32768;
constexpr auto SOCU_ALIGN      = 0x1000;
constexpr auto SOCU_BUFFERSIZE = 0x100000;

static_assert (SOCU_BUFFERSIZE % SOCU_ALIGN == 0);

bool s_socuActive = false;
u32 *s_socuBuffer = nullptr;

C3D_Tex s_gfxTexture;
Tex3DS_Texture s_gfxT3x;

void startNetwork ()
{
	if (s_socuActive)
		return;

	std::uint32_t wifi = 0;
	if (R_FAILED (ACU_GetWifiStatus (&wifi)) || !wifi)
		return;

	if (!s_socuBuffer)
		s_socuBuffer = static_cast<u32 *> (::memalign (SOCU_ALIGN, SOCU_BUFFERSIZE));

	if (!s_socuBuffer)
		return;

	if (R_FAILED (socInit (s_socuBuffer, SOCU_BUFFERSIZE)))
		return;

	s_socuActive = true;
	Log::info ("Wifi connected\n");
}

void drawLogo ()
{
	auto subTex = Tex3DS_GetSubTexture (s_gfxT3x, gfx_c3dlogo_idx);

	ImGuiIO &io             = ImGui::GetIO ();
	auto const screenWidth  = io.DisplaySize.x;
	auto const screenHeight = io.DisplaySize.y;
	auto const logoWidth    = subTex->width / io.DisplayFramebufferScale.x;
	auto const logoHeight   = subTex->height / io.DisplayFramebufferScale.y;

	auto const x1 = (screenWidth - logoWidth) / 2.0f;
	auto const x2 = x1 + logoWidth;
	auto const y1 = (screenHeight / 2.0f - logoHeight) / 2.0f;
	auto const y2 = y1 + logoHeight;

	auto const uv1 = ImVec2 (subTex->left, subTex->top);
	auto const uv2 = ImVec2 (subTex->right, subTex->bottom);

	ImGui::GetBackgroundDrawList ()->AddImage (
	    &s_gfxTexture, ImVec2 (x1, y1), ImVec2 (x2, y2), uv1, uv2);

	ImGui::GetBackgroundDrawList ()->AddImage (&s_gfxTexture,
	    ImVec2 (x1, y1 + screenHeight / 2.0f),
	    ImVec2 (x2, y2 + screenHeight / 2.0f),
	    uv1,
	    uv2);
}

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

	auto const battery =
	    Tex3DS_GetSubTexture (s_gfxT3x, charging ? gfx_batteryCharge_idx : batteryLevels[level]);
	auto const batteryWidth  = battery->width / io.DisplayFramebufferScale.x;
	auto const batteryHeight = battery->height / io.DisplayFramebufferScale.y;

	auto const p1 = ImVec2 (screenWidth - batteryWidth, 0.0f);
	auto const p2 = ImVec2 (screenWidth, batteryHeight);

	auto const uv1 = ImVec2 (battery->left, battery->top);
	auto const uv2 = ImVec2 (battery->right, battery->bottom);

	ImGui::GetForegroundDrawList ()->AddImage (&s_gfxTexture, p1, p2, uv1, uv2);

	auto const wifiStrength = osGetWifiStrength ();

	auto const wifi       = Tex3DS_GetSubTexture (s_gfxT3x, wifiLevels[wifiStrength]);
	auto const wifiWidth  = wifi->width / io.DisplayFramebufferScale.x;
	auto const wifiHeight = wifi->height / io.DisplayFramebufferScale.y;

	auto const p3 = ImVec2 (p1.x - wifiWidth - 4.0f, 0.0f);
	auto const p4 = ImVec2 (p1.x - 4.0f, wifiHeight);

	auto const uv3 = ImVec2 (wifi->left, wifi->top);
	auto const uv4 = ImVec2 (wifi->right, wifi->bottom);

	ImGui::GetForegroundDrawList ()->AddImage (&s_gfxTexture, p3, p4, uv3, uv4);

	char buffer[64];
	auto const now = std::time (nullptr);
	std::strftime (buffer, sizeof (buffer), "%H:%M:%S", std::localtime (&now));
	ImGui::GetForegroundDrawList ()->AddText (
	    ImVec2 (p3.x - 130.0f, style.FramePadding.y), 0xFFFFFFFF, buffer);
}
}

bool platform::init ()
{
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

	IMGUI_CHECKVERSION ();
	ImGui::CreateContext ();

	if (!imgui::ctru::init ())
	{
		ImGui::DestroyContext ();
		return false;
	}

	imgui::citro3d::init ();

	{
		fs::File file;
		if (!file.open ("romfs:/gfx.t3x"))
			svcBreak (USERBREAK_PANIC);

		s_gfxT3x = Tex3DS_TextureImportStdio (file, &s_gfxTexture, nullptr, true);
		if (!s_gfxT3x)
			svcBreak (USERBREAK_PANIC);
	}

	C3D_TexSetFilter (&s_gfxTexture, GPU_LINEAR, GPU_LINEAR);

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

	ImGui::DestroyContext ();

	imgui::citro3d::exit ();
	imgui::ctru::exit ();

	socExit ();
	s_socuActive = false;
	std::free (s_socuBuffer);

	gfxExit ();
	ptmuExit ();
	acExit ();
}

///////////////////////////////////////////////////////////////////////////
class platform::Thread::privateData_t
{
public:
	privateData_t ()
	{
		if (thread)
			threadFree (thread);
	}

	privateData_t (std::function<void ()> func_) : thread (nullptr)
	{
		s32 priority = 0x30;
		svcGetThreadPriority (&priority, CUR_THREAD_HANDLE);

		thread = threadCreate (&privateData_t::threadFunc, this, STACK_SIZE, priority, -1, false);
		assert (thread);
	}

	static void threadFunc (void *const arg_)
	{
		auto const t = static_cast<privateData_t *> (arg_);
		t->func ();
	}

	::Thread thread = nullptr;
	std::function<void ()> func;
};

///////////////////////////////////////////////////////////////////////////
platform::Thread::~Thread () = default;

platform::Thread::Thread () : m_d (new privateData_t ())
{
}

platform::Thread::Thread (std::function<void ()> func_) : m_d (new privateData_t (func_))
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
class platform::Mutex::privateData_t
{
public:
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
