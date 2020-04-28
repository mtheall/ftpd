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
#include "ftpServer.h"
#include "log.h"

#include "imgui_deko3d.h"
#include "imgui_nx.h"

#include "imgui.h"

#include <zstd.h>

#include <arpa/inet.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <numeric>
#include <thread>

#ifdef CLASSIC
PrintConsole g_statusConsole;
PrintConsole g_logConsole;
PrintConsole g_sessionConsole;
#endif

namespace
{
/// \brief Whether to power backlight
bool s_backlight = true;

/// \brief Applet hook cookie
AppletHookCookie s_appletHookCookie;

#ifdef CLASSIC
in_addr_t s_addr = 0;
#else
/// \brief Texture index
enum TextureIndex
{
	DEKO3D_LOGO = 1,
	BATTERY_ICON,
	CHARGING_ICON,
	ETH_NONE_ICON,
	ETH_ICON,
	AIRPLANE_ICON,
	WIFI_NONE_ICON,
	WIFI1_ICON,
	WIFI2_ICON,
	WIFI3_ICON,

	MAX_TEXTURE,
};

/// \brief deko3d logo width
constexpr auto LOGO_WIDTH = 500;
/// \brief deko3d logo height
constexpr auto LOGO_HEIGHT = 493;

/// \brief icon width
constexpr auto ICON_WIDTH = 24;
/// \brief icon height
constexpr auto ICON_HEIGHT = 24;

/// \brief Maximum number of samplers
constexpr auto MAX_SAMPLERS = 2;
/// \brief Maximum number of images
constexpr auto MAX_IMAGES = MAX_TEXTURE;

/// \brief Number of framebuffers
constexpr auto FB_NUM = 2u;

/// \brief Command buffer size
constexpr auto CMDBUF_SIZE = 1024 * 1024;

/// \brief Framebuffer width
unsigned s_width = 1920;
/// \brief Framebuffer height
unsigned s_height = 1080;

/// \brief deko3d device
dk::UniqueDevice s_device;

/// \brief Depth buffer memblock
dk::UniqueMemBlock s_depthMemBlock;
/// \brief Depth buffer image
dk::Image s_depthBuffer;

/// \brief Framebuffer memblock
dk::UniqueMemBlock s_fbMemBlock;
/// \brief Framebuffer images
dk::Image s_frameBuffers[FB_NUM];

/// \brief Command buffer memblock
dk::UniqueMemBlock s_cmdMemBlock[FB_NUM];
/// \brief Command buffers
dk::UniqueCmdBuf s_cmdBuf[FB_NUM];

/// \brief Image memblock
dk::UniqueMemBlock s_imageMemBlock;

/// \brief Image/Sampler descriptor memblock
dk::UniqueMemBlock s_descriptorMemBlock;
/// \brief Sample descriptors
dk::SamplerDescriptor *s_samplerDescriptors = nullptr;
/// \brief Image descriptors
dk::ImageDescriptor *s_imageDescriptors = nullptr;

/// \brief deko3d queue
dk::UniqueQueue s_queue;

/// \brief deko3d swapchain
dk::UniqueSwapchain s_swapchain;

/// \brief Rebuild swapchain
/// \param width_ Framebuffer width
/// \param height_ Framebuffer height
/// \note This assumes the first call is the largest a framebuffer will ever be
void rebuildSwapchain (unsigned const width_, unsigned const height_)
{
	// destroy old swapchain
	s_swapchain = nullptr;

	// create new depth buffer image layout
	dk::ImageLayout depthLayout;
	dk::ImageLayoutMaker{s_device}
	    .setFlags (DkImageFlags_UsageRender | DkImageFlags_HwCompression)
	    .setFormat (DkImageFormat_Z24S8)
	    .setDimensions (width_, height_)
	    .initialize (depthLayout);

	auto const depthAlign = depthLayout.getAlignment ();
	auto const depthSize  = depthLayout.getSize ();

	// create depth buffer memblock
	if (!s_depthMemBlock)
	{
		s_depthMemBlock = dk::MemBlockMaker{s_device,
		    imgui::deko3d::align (
		        depthSize, std::max<unsigned> (depthAlign, DK_MEMBLOCK_ALIGNMENT))}
		                      .setFlags (DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
		                      .create ();
	}

	s_depthBuffer.initialize (depthLayout, s_depthMemBlock, 0);

	// create framebuffer image layout
	dk::ImageLayout fbLayout;
	dk::ImageLayoutMaker{s_device}
	    .setFlags (
	        DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression)
	    .setFormat (DkImageFormat_RGBA8_Unorm)
	    .setDimensions (width_, height_)
	    .initialize (fbLayout);

	auto const fbAlign = fbLayout.getAlignment ();
	auto const fbSize  = fbLayout.getSize ();

	// create framebuffer memblock
	if (!s_fbMemBlock)
	{
		s_fbMemBlock = dk::MemBlockMaker{s_device,
		    imgui::deko3d::align (
		        FB_NUM * fbSize, std::max<unsigned> (fbAlign, DK_MEMBLOCK_ALIGNMENT))}
		                   .setFlags (DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
		                   .create ();
	}

	// initialize swapchain images
	std::array<DkImage const *, FB_NUM> swapchainImages;
	for (unsigned i = 0; i < FB_NUM; ++i)
	{
		swapchainImages[i] = &s_frameBuffers[i];
		s_frameBuffers[i].initialize (fbLayout, s_fbMemBlock, i * fbSize);
	}

	// create swapchain
	s_swapchain = dk::SwapchainMaker{s_device, nwindowGetDefault (), swapchainImages}.create ();
}

/// \brief Initialize deko3d
void deko3dInit ()
{
	// create deko3d device
	s_device = dk::DeviceMaker{}.create ();

	// initialize swapchain with maximum resolution
	rebuildSwapchain (1920, 1080);

	// create memblocks for each image slot
	for (std::size_t i = 0; i < FB_NUM; ++i)
	{
		// create command buffer memblock
		s_cmdMemBlock[i] =
		    dk::MemBlockMaker{s_device, imgui::deko3d::align (CMDBUF_SIZE, DK_MEMBLOCK_ALIGNMENT)}
		        .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
		        .create ();

		// create command buffer
		s_cmdBuf[i] = dk::CmdBufMaker{s_device}.create ();
		s_cmdBuf[i].addMemory (s_cmdMemBlock[i], 0, s_cmdMemBlock[i].getSize ());
	}

	// create image/sampler memblock
	static_assert (sizeof (dk::ImageDescriptor) == DK_IMAGE_DESCRIPTOR_ALIGNMENT);
	static_assert (sizeof (dk::SamplerDescriptor) == DK_SAMPLER_DESCRIPTOR_ALIGNMENT);
	static_assert (DK_IMAGE_DESCRIPTOR_ALIGNMENT == DK_SAMPLER_DESCRIPTOR_ALIGNMENT);
	s_descriptorMemBlock = dk::MemBlockMaker{s_device,
	    imgui::deko3d::align (
	        (MAX_SAMPLERS + MAX_IMAGES) * sizeof (dk::ImageDescriptor), DK_MEMBLOCK_ALIGNMENT)}
	                           .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
	                           .create ();

	// get cpu address for descriptors
	s_samplerDescriptors =
	    static_cast<dk::SamplerDescriptor *> (s_descriptorMemBlock.getCpuAddr ());
	s_imageDescriptors =
	    reinterpret_cast<dk::ImageDescriptor *> (&s_samplerDescriptors[MAX_SAMPLERS]);

	// create queue
	s_queue = dk::QueueMaker{s_device}.setFlags (DkQueueFlags_Graphics).create ();

	auto &cmdBuf = s_cmdBuf[0];

	// bind image/sampler descriptors
	cmdBuf.bindSamplerDescriptorSet (s_descriptorMemBlock.getGpuAddr (), MAX_SAMPLERS);
	cmdBuf.bindImageDescriptorSet (
	    s_descriptorMemBlock.getGpuAddr () + MAX_SAMPLERS * sizeof (dk::SamplerDescriptor),
	    MAX_IMAGES);
	s_queue.submitCommands (cmdBuf.finishList ());
	s_queue.waitIdle ();

	cmdBuf.clear ();
}

/// \brief Load textures
void loadTextures ()
{
	struct TextureInfo
	{
		TextureInfo (char const *const path_, unsigned const width_, unsigned const height_)
		    : path (path_), width (width_), height (height_)
		{
		}

		char const *const path;
		unsigned width;
		unsigned height;
	};

	TextureInfo textureInfos[] = {TextureInfo ("romfs:/deko3d.rgba.zst", LOGO_WIDTH, LOGO_HEIGHT),
	    TextureInfo ("romfs:/battery_icon.rgba.zst", ICON_WIDTH, ICON_HEIGHT),
	    TextureInfo ("romfs:/charging_icon.rgba.zst", ICON_WIDTH, ICON_HEIGHT),
	    TextureInfo ("romfs:/eth_none_icon.rgba.zst", ICON_WIDTH, ICON_HEIGHT),
	    TextureInfo ("romfs:/eth_icon.rgba.zst", ICON_WIDTH, ICON_HEIGHT),
	    TextureInfo ("romfs:/airplane_icon.rgba.zst", ICON_WIDTH, ICON_HEIGHT),
	    TextureInfo ("romfs:/wifi_none_icon.rgba.zst", ICON_WIDTH, ICON_HEIGHT),
	    TextureInfo ("romfs:/wifi1_icon.rgba.zst", ICON_WIDTH, ICON_HEIGHT),
	    TextureInfo ("romfs:/wifi2_icon.rgba.zst", ICON_WIDTH, ICON_HEIGHT),
	    TextureInfo ("romfs:/wifi3_icon.rgba.zst", ICON_WIDTH, ICON_HEIGHT)};

	// create memblock for transfer (large enough for the largest source file)
	dk::UniqueMemBlock memBlock =
	    dk::MemBlockMaker{s_device, imgui::deko3d::align (1048576, DK_MEMBLOCK_ALIGNMENT)}
	        .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
	        .create ();

	// create image memblock (large enough to hold all the images)
	s_imageMemBlock = dk::MemBlockMaker{s_device,
	    imgui::deko3d::align (1048576 + (MAX_TEXTURE - 2) * 4096, DK_MEMBLOCK_ALIGNMENT)}
	                      .setFlags (DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
	                      .create ();

	auto &cmdBuf = s_cmdBuf[0];

	unsigned imageIndex  = 1;
	unsigned imageOffset = 0;
	for (auto const &textureInfo : textureInfos)
	{
		struct stat st;
		if (::stat (textureInfo.path, &st) != 0)
		{
			std::fprintf (stderr, "stat(%s): %s\n", textureInfo.path, std::strerror (errno));
			std::abort ();
		}

		fs::File fp;
		if (!fp.open (textureInfo.path))
		{
			std::fprintf (stderr, "open(%s): %s\n", textureInfo.path, std::strerror (errno));
			std::abort ();
		}

		// read file into memory
		std::vector<char> buffer (st.st_size);
		if (!fp.readAll (buffer.data (), buffer.size ()))
		{
			std::fprintf (stderr, "read(%s): %s\n", textureInfo.path, std::strerror (errno));
			std::abort ();
		}

		// get uncompressed size
		auto const size = ZSTD_getFrameContentSize (buffer.data (), buffer.size ());
		if (ZSTD_isError (size))
		{
			std::fprintf (stderr, "ZSTD_getFrameContentSize: %s\n", ZSTD_getErrorName (size));
			std::abort ();
		}
		assert (size <= memBlock.getSize ());

		// wait for previous transfer to complete
		s_queue.waitIdle ();

		// decompress into transfer memblock
		auto const decoded =
		    ZSTD_decompress (memBlock.getCpuAddr (), size, buffer.data (), buffer.size ());
		if (ZSTD_isError (decoded))
		{
			std::fprintf (stderr, "ZSTD_decompress: %s\n", ZSTD_getErrorName (decoded));
			std::abort ();
		}

		// initialize texture image layout
		dk::ImageLayout layout;
		dk::ImageLayoutMaker{s_device}
		    .setFlags (0)
		    .setFormat (DkImageFormat_RGBA8_Unorm)
		    .setDimensions (textureInfo.width, textureInfo.height)
		    .initialize (layout);

		// calculate image offset
		imageOffset = imgui::deko3d::align (imageOffset, layout.getAlignment ());
		assert (imageOffset < s_imageMemBlock.getSize ());
		assert (s_imageMemBlock.getSize () - imageOffset >= layout.getSize ());

		// initialize image descriptor
		dk::Image image;
		image.initialize (layout, s_imageMemBlock, imageOffset);
		s_imageDescriptors[imageIndex++].initialize (image);

		// copy texture to image
		dk::ImageView imageView (image);
		cmdBuf.copyBufferToImage ({memBlock.getGpuAddr ()},
		    imageView,
		    {0, 0, 0, textureInfo.width, textureInfo.height, 1});
		s_queue.submitCommands (cmdBuf.finishList ());

		imageOffset += imgui::deko3d::align (layout.getSize (), layout.getAlignment ());
	}

	// initialize sampler descriptor
	s_samplerDescriptors[1].initialize (
	    dk::Sampler{}
	        .setFilter (DkFilter_Linear, DkFilter_Linear)
	        .setWrapMode (DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge));

	// wait for commands to complete before releasing memblocks
	s_queue.waitIdle ();
}

/// \brief Deinitialize deko3d
void deko3dExit ()
{
	// clean up all of the deko3d objects
	s_imageMemBlock      = nullptr;
	s_descriptorMemBlock = nullptr;

	for (unsigned i = 0; i < FB_NUM; ++i)
	{
		s_cmdBuf[i]      = nullptr;
		s_cmdMemBlock[i] = nullptr;
	}

	s_queue         = nullptr;
	s_swapchain     = nullptr;
	s_fbMemBlock    = nullptr;
	s_depthMemBlock = nullptr;
	s_device        = nullptr;
}
#endif

/// \brief Handle applet hook
/// \param hook_ Callback reason
/// \param param_ User param
void handleAppletHook (AppletHookType const hook_, void *const param_)
{
	(void)param_;
	switch (hook_)
	{
	case AppletHookType_OnFocusState:
		if (appletGetFocusState () == AppletFocusState_Focused)
			appletSetLcdBacklightOffEnabled (!s_backlight);
		break;

	default:
		break;
	}
}

/// \brief Draw status
void drawStatus ()
{
#ifndef CLASSIC
	auto const &io    = ImGui::GetIO ();
	auto const &style = ImGui::GetStyle ();

	auto pos = ImVec2 (io.DisplaySize.x, style.FramePadding.y);

	{
		std::uint32_t batteryCharge = 0;
		psmGetBatteryChargePercentage (&batteryCharge);

		ChargerType charger = ChargerType_None;
		psmGetChargerType (&charger);

		TextureIndex powerIcon = BATTERY_ICON;
		if (charger != ChargerType_None)
			powerIcon = CHARGING_ICON;

		// draw battery/charging icon
		pos.x -= ICON_WIDTH + style.FramePadding.x;
		ImGui::GetForegroundDrawList ()->AddImage (
		    imgui::deko3d::makeTextureID (dkMakeTextureHandle (powerIcon, 1)),
		    pos,
		    ImVec2 (pos.x + ICON_WIDTH, pos.y + ICON_HEIGHT),
		    ImVec2 (0, 0),
		    ImVec2 (1, 1),
		    ImGui::GetColorU32 (ImGuiCol_Text));

		char buffer[16];
		std::sprintf (buffer, "%u%%", batteryCharge);

		// draw battery percentage
		auto const fullWidth = ImGui::CalcTextSize ("100%").x;
		auto const partWidth = ImGui::CalcTextSize (buffer).x;
		auto const diffWidth = fullWidth - partWidth;

		// janky right-align
		pos.x -= partWidth + style.FramePadding.x;
		ImGui::GetForegroundDrawList ()->AddText (pos, ImGui::GetColorU32 (ImGuiCol_Text), buffer);
		pos.x -= diffWidth;
	}

	{
		TextureIndex netIcon = AIRPLANE_ICON;

		NifmInternetConnectionType type;
		std::uint32_t wifiStrength;
		NifmInternetConnectionStatus status;
		if (R_SUCCEEDED (nifmGetInternetConnectionStatus (&type, &wifiStrength, &status)))
		{
			if (type == NifmInternetConnectionType_Ethernet)
			{
				if (status == NifmInternetConnectionStatus_Connected)
					netIcon = ETH_ICON;
				else
					netIcon = ETH_NONE_ICON;
			}
			else
			{
				if (wifiStrength >= 3)
					netIcon = WIFI3_ICON;
				else if (wifiStrength == 2)
					netIcon = WIFI3_ICON;
				else if (wifiStrength == 1)
					netIcon = WIFI3_ICON;
				else if (wifiStrength == 0)
					netIcon = WIFI_NONE_ICON;
			}
		}

		// draw network icon
		pos.x -= ICON_WIDTH + style.FramePadding.x;
		ImGui::GetForegroundDrawList ()->AddImage (
		    imgui::deko3d::makeTextureID (dkMakeTextureHandle (netIcon, 1)),
		    pos,
		    ImVec2 (pos.x + ICON_WIDTH, pos.y + ICON_HEIGHT),
		    ImVec2 (0, 0),
		    ImVec2 (1, 1),
		    ImGui::GetColorU32 (ImGuiCol_Text));
	}

	{
		// draw free space
		auto const freeSpace = FtpServer::getFreeSpace ();
		pos.x -= ImGui::CalcTextSize (freeSpace.c_str ()).x + style.FramePadding.x;
		ImGui::GetForegroundDrawList ()->AddText (
		    pos, ImGui::GetColorU32 (ImGuiCol_Text), freeSpace.c_str ());
	}

	{
		// get current timestamp
		char timeBuffer[16];
		auto const now = std::time (nullptr);
		std::strftime (timeBuffer, sizeof (timeBuffer), "%H:%M:%S", std::localtime (&now));

		// draw time (centered)
		pos.x = (io.DisplaySize.x - ImGui::CalcTextSize (timeBuffer).x) / 2.0f;
		ImGui::GetForegroundDrawList ()->AddText (
		    pos, ImGui::GetColorU32 (ImGuiCol_Text), timeBuffer);
	}
#endif
}
}



bool platform::init ()
{
#ifdef CLASSIC
	consoleInit (&g_statusConsole);
	consoleInit (&g_logConsole);
	consoleInit (&g_sessionConsole);

	consoleSetWindow (&g_statusConsole, 0, 0, 80, 1);
	consoleSetWindow (&g_logConsole, 0, 1, 80, 36);
	consoleSetWindow (&g_sessionConsole, 0, 37, 80, 8);
#endif

#ifndef NDEBUG
	std::setvbuf (stderr, nullptr, _IOLBF, 0);
#endif

#ifndef CLASSIC
	if (!imgui::nx::init ())
		return false;

	deko3dInit ();
	loadTextures ();
	imgui::deko3d::init (s_device,
	    s_queue,
	    s_cmdBuf[0],
	    s_samplerDescriptors[0],
	    s_imageDescriptors[0],
	    dkMakeTextureHandle (0, 0),
	    FB_NUM);
#endif

	appletHook (&s_appletHookCookie, handleAppletHook, nullptr);

	return true;
}

bool platform::networkVisible ()
{
	NifmInternetConnectionType type;
	std::uint32_t wifi;
	NifmInternetConnectionStatus status;
	if (R_FAILED (nifmGetInternetConnectionStatus (&type, &wifi, &status)))
		return false;

#ifdef CLASSIC
	if (!s_addr)
		s_addr = gethostid ();
#endif

	return status == NifmInternetConnectionStatus_Connected;
}

bool platform::loop ()
{
	if (!appletMainLoop ())
		return false;

	hidScanInput ();

	auto const keysDown = hidKeysDown (CONTROLLER_P1_AUTO);

	// check if the user wants to exit
	if (keysDown & KEY_PLUS)
		return false;

	// check if the user wants to toggle the backlight
	if (keysDown & KEY_MINUS)
	{
		s_backlight = !s_backlight;
		appletSetLcdBacklightOffEnabled (!s_backlight);
	}

#ifndef CLASSIC
	imgui::nx::newFrame ();
	ImGui::NewFrame ();

	auto const &io = ImGui::GetIO ();

	auto const width  = io.DisplaySize.x;
	auto const height = io.DisplaySize.y;

	auto const x1 = (width - LOGO_WIDTH) * 0.5f;
	auto const x2 = x1 + LOGO_WIDTH;
	auto const y1 = (height - LOGO_HEIGHT) * 0.5f;
	auto const y2 = y1 + LOGO_HEIGHT;

	ImGui::GetBackgroundDrawList ()->AddImage (
	    imgui::deko3d::makeTextureID (dkMakeTextureHandle (1, 1)),
	    ImVec2 (x1, y1),
	    ImVec2 (x2, y2));
#endif

	drawStatus ();

	return true;
}

void platform::render ()
{
#ifdef CLASSIC
	consoleUpdate (&g_statusConsole);
	consoleUpdate (&g_logConsole);
	consoleUpdate (&g_sessionConsole);
#else
	ImGui::Render ();

	auto &io = ImGui::GetIO ();

	if (s_width != io.DisplaySize.x || s_height != io.DisplaySize.y)
	{
		s_width  = io.DisplaySize.x;
		s_height = io.DisplaySize.y;
		rebuildSwapchain (s_width, s_height);
	}

	// get image from queue
	auto const slot = s_queue.acquireImage (s_swapchain);
	auto &cmdBuf    = s_cmdBuf[slot];

	cmdBuf.clear ();

	// bind frame/depth buffers and clear them
	dk::ImageView colorTarget{s_frameBuffers[slot]};
	dk::ImageView depthTarget{s_depthBuffer};
	cmdBuf.bindRenderTargets (&colorTarget, &depthTarget);
	cmdBuf.clearColor (0, DkColorMask_RGBA, 0.125f, 0.294f, 0.478f, 1.0f);
	cmdBuf.clearDepthStencil (true, 1.0f, 0xFF, 0);
	s_queue.submitCommands (cmdBuf.finishList ());

	imgui::deko3d::render (s_device, s_queue, cmdBuf, slot);

	// wait for fragments to be completed before discarding depth/stencil buffer
	cmdBuf.barrier (DkBarrier_Fragments, 0);
	cmdBuf.discardDepthStencil ();

	// present image
	s_queue.presentImage (s_swapchain, slot);
#endif
}

void platform::exit ()
{
#ifdef CLASSIC
	consoleExit (&g_sessionConsole);
	consoleExit (&g_logConsole);
	consoleExit (&g_statusConsole);
#else
	imgui::nx::exit ();

	// wait for queue to be idle
	s_queue.waitIdle ();

	imgui::deko3d::exit ();
	deko3dExit ();
#endif

	appletUnhook (&s_appletHookCookie);

	if (!s_backlight)
		appletSetLcdBacklightOffEnabled (false);
}

///////////////////////////////////////////////////////////////////////////
/// \brief Platform thread pimpl
class platform::Thread::privateData_t
{
public:
	privateData_t () = default;

	/// \brief Parameterized constructor
	/// \param func_ Thread entry point
	privateData_t (std::function<void ()> &&func_) : thread (std::move (func_))
	{
	}

	/// \brief Underlying thread
	std::thread thread;
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
	m_d->thread.join ();
}

void platform::Thread::sleep (std::chrono::milliseconds const timeout_)
{
	std::this_thread::sleep_for (timeout_);
}

///////////////////////////////////////////////////////////////////////////
#define USE_STD_MUTEX 1

/// \brief Platform mutex pimpl
class platform::Mutex::privateData_t
{
public:
#if USE_STD_MUTEX
	/// \brief Underlying mutex
	std::mutex mutex;
#else
	/// \brief Underlying mutex
	::Mutex mutex;
#endif
};

///////////////////////////////////////////////////////////////////////////
platform::Mutex::~Mutex () = default;

platform::Mutex::Mutex () : m_d (new privateData_t ())
{
#if !USE_STD_MUTEX
	mutexInit (&m_d->mutex);
#endif
}

void platform::Mutex::lock ()
{
#if USE_STD_MUTEX
	m_d->mutex.lock ();
#else
	mutexLock (&m_d->mutex);
#endif
}

void platform::Mutex::unlock ()
{
#if USE_STD_MUTEX
	m_d->mutex.unlock ();
#else
	mutexUnlock (&m_d->mutex);
#endif
}
