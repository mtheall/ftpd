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

#include "platform.h"

#include "fs.h"
#include "ftpServer.h"
#include "log.h"

#include "imgui_deko3d.h"
#include "imgui_nx.h"

#include "imgui.h"

#ifndef CLASSIC
#include <zstd.h>
#endif

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

/// \brief Whether access point is active
bool s_activeAP = false;

/// \brief Access point SSID
std::string s_ssid;

/// \brief Applet hook cookie
AppletHookCookie s_appletHookCookie;

/// \brief Gamepad state
PadState s_padState;

#ifndef CLASSIC
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
constexpr auto LOGO_WIDTH = 504;
/// \brief deko3d logo height
constexpr auto LOGO_HEIGHT = 504;

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

/// \brief Touch screen state
HidTouchScreenState s_touchState;
/// \brief Mouse state
HidMouseState s_mouseState;
/// \brief Keyboard state
HidKeyboardState s_kbState;

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
	hidInitializeTouchScreen ();
	hidInitializeMouse ();
	hidInitializeKeyboard ();

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
		TextureInfo (char const *const path_,
		    DkImageFormat const format_,
		    unsigned const width_,
		    unsigned const height_)
		    : path (path_), format (format_), width (width_), height (height_)
		{
		}

		char const *const path;
		DkImageFormat const format;
		unsigned const width;
		unsigned const height;
	};

	TextureInfo textureInfos[] = {
	    // clang-format off
	    TextureInfo (
	        "romfs:/deko3d.12x12.astc.zst", DkImageFormat_RGBA_ASTC_12x12, LOGO_WIDTH, LOGO_HEIGHT),
	    TextureInfo (
	        "romfs:/battery_icon.rgba.zst", DkImageFormat_RGBA8_Unorm, ICON_WIDTH, ICON_HEIGHT),
	    TextureInfo (
	        "romfs:/charging_icon.rgba.zst", DkImageFormat_RGBA8_Unorm, ICON_WIDTH, ICON_HEIGHT),
	    TextureInfo (
	        "romfs:/eth_none_icon.rgba.zst", DkImageFormat_RGBA8_Unorm, ICON_WIDTH, ICON_HEIGHT),
	    TextureInfo (
	        "romfs:/eth_icon.rgba.zst", DkImageFormat_RGBA8_Unorm, ICON_WIDTH, ICON_HEIGHT),
	    TextureInfo (
	        "romfs:/airplane_icon.rgba.zst", DkImageFormat_RGBA8_Unorm, ICON_WIDTH, ICON_HEIGHT),
	    TextureInfo (
	        "romfs:/wifi_none_icon.rgba.zst", DkImageFormat_RGBA8_Unorm, ICON_WIDTH, ICON_HEIGHT),
	    TextureInfo (
	        "romfs:/wifi1_icon.rgba.zst", DkImageFormat_RGBA8_Unorm, ICON_WIDTH, ICON_HEIGHT),
	    TextureInfo (
	        "romfs:/wifi2_icon.rgba.zst", DkImageFormat_RGBA8_Unorm, ICON_WIDTH, ICON_HEIGHT),
	    TextureInfo (
	        "romfs:/wifi3_icon.rgba.zst", DkImageFormat_RGBA8_Unorm, ICON_WIDTH, ICON_HEIGHT)
	    // clang-format on
	};

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
		    .setFormat (textureInfo.format)
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
		if (appletGetFocusState () == AppletFocusState_InFocus)
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

		PsmChargerType charger = PsmChargerType_Unconnected;
		psmGetChargerType (&charger);

		TextureIndex powerIcon = BATTERY_ICON;
		if (charger != PsmChargerType_Unconnected)
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

		if (s_activeAP)
			netIcon = WIFI3_ICON;
		else
		{
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

	if (s_activeAP)
	{
		auto const size    = ImGui::CalcTextSize (s_ssid.c_str ());
		auto const ssidPos = ImVec2 (
		    io.DisplaySize.x - style.FramePadding.x - size.x, 3 * style.FramePadding.y + size.y);
		ImGui::GetForegroundDrawList ()->AddText (
		    ssidPos, ImGui::GetColorU32 (ImGuiCol_Text), s_ssid.c_str ());
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

	padConfigureInput (1, HidNpadStyleSet_NpadFullCtrl);
	padInitializeDefault (&s_padState);

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

bool platform::enableAP (bool const enableAP_,
    std::string const &ssid_,
    std::string const &passphrase_)
{
	if (s_activeAP == enableAP_)
		return true;

	if (enableAP_)
	{
		auto const ssidError = validateSSID (ssid_);
		if (ssidError)
		{
			error ("Access Point: %s\n", ssidError);
			return false;
		}

		auto const passphraseError = validatePassphrase (passphrase_);
		if (passphraseError)
		{
			error ("Access Point: %s\n", passphraseError);
			return false;
		}

		auto rc = lp2pInitialize (Lp2pServiceType_App);
		if (R_FAILED (rc))
		{
			error ("lp2pInitialize: 0x%x\n", rc);
			return false;
		}

		Lp2pGroupInfo groupInfo;
		lp2pCreateGroupInfo (&groupInfo);

		// set SSID
		lp2pGroupInfoSetServiceName (&groupInfo, ssid_.c_str ());

		// enable WPA2-PSK
		s8 flags = 0;
		lp2pGroupInfoSetFlags (&groupInfo, &flags, 1);

		// passphrase
		rc = lp2pGroupInfoSetPassphrase (&groupInfo, passphrase_.c_str ());
		if (R_FAILED (rc))
		{
			error ("lp2pGroupInfoSetPassphrase: 0x%x\n", rc);
			lp2pExit ();
			return false;
		}

		// create group
		rc = lp2pCreateGroup (&groupInfo);
		if (R_FAILED (rc))
		{
			error ("lp2pCreateGroup: 0x%x\n", rc);
			lp2pExit ();
			return false;
		}

		rc = lp2pGetGroupInfo (&groupInfo);
		if (R_FAILED (rc))
		{
			error ("lp2pGetGroupInfo: 0x%x\n", rc);
			lp2pDestroyGroup ();
			lp2pExit ();
			return false;
		}

		s_ssid = std::string ("SSID: ") + groupInfo.service_name;

		s_activeAP = true;
	}
	else
	{
		lp2pDestroyGroup ();
		lp2pExit ();
		s_activeAP = false;
	}

	return true;
}

char const *platform::validateSSID (std::string const &ssid_)
{
	auto const ssid = std::string_view (ssid_).substr (0, ssid_.find_first_of ('\0'));

	if (ssid.size () > 19)
		return "SSID too long";

	for (auto const &c : ssid)
	{
		if (!std::isalnum (c) && c != '-')
			return "SSID can only contain alphanumeric and -";
	}

	return nullptr;
}

char const *platform::validatePassphrase (std::string const &passphrase_)
{
	auto const passphrase =
	    std::string_view (passphrase_).substr (0, passphrase_.find_first_of ('\0'));

	if (passphrase.size () < 8)
		return "Passphrase too short";
	else if (passphrase.size () > 63)
		return "Passphrase too long";

	return nullptr;
}

bool platform::networkVisible ()
{
	if (s_activeAP)
		return true;

	NifmInternetConnectionType type;
	std::uint32_t wifi;
	NifmInternetConnectionStatus status;
	if (R_FAILED (nifmGetInternetConnectionStatus (&type, &wifi, &status)))
		return false;

	return status == NifmInternetConnectionStatus_Connected;
}

bool platform::networkAddress (SockAddr &addr_)
{
	if (s_activeAP)
	{
		Lp2pIpConfig ipConfig;
		auto const rc = lp2pGetIpConfig (&ipConfig);
		if (R_FAILED (rc))
		{
			error ("lp2pGetIpConfig: 0x%x\n", rc);
			return false;
		}

		addr_ = *reinterpret_cast<struct sockaddr_in *> (ipConfig.ip_addr);
		return true;
	}

	struct sockaddr_in addr;
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = gethostid ();

	addr_ = addr;
	return true;
}

bool platform::loop ()
{
	if (!appletMainLoop ())
		return false;

	padUpdate (&s_padState);

	auto const keys = padGetButtons (&s_padState);

	// check if the user wants to exit
	if (keys & HidNpadButton_Plus)
		return false;

	// check if the user wants to toggle the backlight
	if (keys & HidNpadButton_Minus)
	{
		s_backlight = !s_backlight;
		appletSetLcdBacklightOffEnabled (!s_backlight);
	}

#ifndef CLASSIC
	auto const touchState = hidGetTouchScreenStates (&s_touchState, 1) ? &s_touchState : nullptr;
	auto const mouseState = hidGetMouseStates (&s_mouseState, 1) ? &s_mouseState : nullptr;
	auto const kbState    = hidGetKeyboardStates (&s_kbState, 1) ? &s_kbState : nullptr;

	imgui::nx::newFrame (&s_padState, touchState, mouseState, kbState);
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

	auto const &io = ImGui::GetIO ();

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

	if (s_activeAP)
	{
		lp2pDestroyGroup ();
		lp2pExit ();
		s_activeAP = false;
	}
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
