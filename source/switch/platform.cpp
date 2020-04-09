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

#include "imgui_deko3d.h"
#include "imgui_nx.h"

#include "imgui.h"

#include <zstd.h>

#include <switch.h>

#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

namespace
{
/// \brief deko3d logo width
constexpr auto LOGO_WIDTH = 500;
/// \brief deko3d logo height
constexpr auto LOGO_HEIGHT = 493;

/// \brief Maximum number of samplers
constexpr auto MAX_SAMPLERS = 2;
/// \brief Maximum number of images
constexpr auto MAX_IMAGES = 2;

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

void loadLogo ()
{
	// read the deko3d logo
	auto const path = "romfs:/deko3d.rgba.zst";

	struct stat st;
	if (stat (path, &st) != 0)
	{
		std::fprintf (stderr, "stat(%s): %s\n", path, std::strerror (errno));
		std::abort ();
	}

	fs::File fp;
	if (!fp.open (path))
	{
		std::fprintf (stderr, "open(%s): %s\n", path, std::strerror (errno));
		std::abort ();
	}

	std::vector<char> buffer (st.st_size);
	if (!fp.readAll (buffer.data (), st.st_size))
	{
		std::fprintf (stderr, "read(%s): %s\n", path, std::strerror (errno));
		std::abort ();
	}

	fp.close ();

	auto const size = ZSTD_getFrameContentSize (buffer.data (), st.st_size);
	if (ZSTD_isError (size))
	{
		std::fprintf (stderr, "ZSTD_getFrameContentSize: %s\n", ZSTD_getErrorName (size));
		std::abort ();
	}

	// create memblock for transfer
	dk::UniqueMemBlock memBlock =
	    dk::MemBlockMaker{s_device, imgui::deko3d::align (size, DK_MEMBLOCK_ALIGNMENT)}
	        .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
	        .create ();

	// decompress into transfer memblock
	auto const decoded = ZSTD_decompress (memBlock.getCpuAddr (), size, buffer.data (), st.st_size);
	if (ZSTD_isError (decoded))
	{
		std::fprintf (stderr, "ZSTD_decompress: %s\n", ZSTD_getErrorName (decoded));
		std::abort ();
	}

	// initialize deko3d logo texture image layout
	dk::ImageLayout layout;
	dk::ImageLayoutMaker{s_device}
	    .setFlags (0)
	    .setFormat (DkImageFormat_RGBA8_Unorm)
	    .setDimensions (LOGO_WIDTH, LOGO_HEIGHT)
	    .initialize (layout);

	auto const logoAlign = layout.getAlignment ();
	auto const logoSize  = layout.getSize ();

	// create image memblock
	s_imageMemBlock = dk::MemBlockMaker{s_device,
	    imgui::deko3d::align (logoSize, std::max<unsigned> (logoAlign, DK_MEMBLOCK_ALIGNMENT))}
	                      .setFlags (DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
	                      .create ();

	// initialize sampler descriptor
	s_samplerDescriptors[1].initialize (
	    dk::Sampler{}
	        .setFilter (DkFilter_Linear, DkFilter_Linear)
	        .setWrapMode (DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge));

	// initialize deko3d logo texture image descriptor
	dk::Image logoTexture;
	logoTexture.initialize (layout, s_imageMemBlock, 0);
	s_imageDescriptors[1].initialize (logoTexture);

	auto &cmdBuf = s_cmdBuf[0];

	// copy deko3d logo texture to image view
	dk::ImageView imageView{logoTexture};
	cmdBuf.copyBufferToImage (
	    {memBlock.getGpuAddr ()}, imageView, {0, 0, 0, LOGO_WIDTH, LOGO_HEIGHT, 1});

	// submit commands to transfer deko3d logo texture
	s_queue.submitCommands (cmdBuf.finishList ());

	// wait for commands to complete before releasing memblocks
	s_queue.waitIdle ();
}

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
}

bool platform::init ()
{
#ifndef NDEBUG
	std::setvbuf (stderr, nullptr, _IOLBF, 0);
#endif

	if (!imgui::nx::init ())
		return false;

	deko3dInit ();
	loadLogo ();
	imgui::deko3d::init (s_device,
	    s_queue,
	    s_cmdBuf[0],
	    s_samplerDescriptors[0],
	    s_imageDescriptors[0],
	    dkMakeTextureHandle (0, 0),
	    FB_NUM);

	return true;
}

bool platform::networkVisible ()
{
	NifmInternetConnectionType type;
	std::uint32_t wifi;
	NifmInternetConnectionStatus status;
	if (R_FAILED (nifmGetInternetConnectionStatus (&type, &wifi, &status)))
		return false;

	return status == NifmInternetConnectionStatus_Connected;
}

bool platform::loop ()
{
	if (!appletMainLoop ())
		return false;

	hidScanInput ();

	auto const keysDown = hidKeysDown (CONTROLLER_P1_AUTO);
	if (keysDown & KEY_PLUS)
		return false;

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

	return true;
}

void platform::render ()
{
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
}

void platform::exit ()
{
	imgui::nx::exit ();

	// wait for queue to be idle
	s_queue.waitIdle ();

	imgui::deko3d::exit ();
	deko3dExit ();
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
