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

#include "imgui_deko3d.h"

#include "imgui.h"

#include "fs.h"
#include "log.h"

#include <deko3d.hpp>
#include <switch.h>

#include <zstd.h>

#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_INTRINSICS
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>

namespace
{
/// \brief deko3d logo width
constexpr auto LOGO_WIDTH = 500;
/// \brief deko3d logo height
constexpr auto LOGO_HEIGHT = 493;

/// \brief Number of framebuffers
constexpr auto FB_NUM = 2u;

/// \brief Command buffer size
constexpr auto CMDBUF_SIZE = 1024 * 1024;
/// \brief Data buffer size
constexpr auto DATABUF_SIZE = 1024 * 1024;
/// \brief Index buffer size
constexpr auto INDEXBUF_SIZE = 1024 * 1024;
/// \brief Image buffer size
constexpr auto IMAGEBUF_SIZE = 16 * 1024 * 1024;

/// \brief Vertex shader UBO
struct VertUBO
{
	/// \brief Projection matrix
	glm::mat4 projMtx;
};

/// \brief Fragment shader UBO
struct FragUBO
{
	/// \brief Whether drawing a font or not
	std::uint32_t font;
};

/// \brief Vertex attribute state
constexpr std::array VERTEX_ATTRIB_STATE = {
    // clang-format off
    DkVtxAttribState{0, 0, offsetof (ImDrawVert, pos), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
    DkVtxAttribState{0, 0, offsetof (ImDrawVert, uv),  DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
    DkVtxAttribState{0, 0, offsetof (ImDrawVert, col), DkVtxAttribSize_4x8,  DkVtxAttribType_Unorm, 0},
    // clang-format on
};

/// \brief Vertex buffer state
constexpr std::array VERTEX_BUFFER_STATE = {
    DkVtxBufferState{sizeof (ImDrawVert), 0},
};

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

/// \brief Font image
dk::Image s_fontTexture;
/// \brief deko3d logo image
dk::Image s_logoTexture;

/// \brief deko3d swapchain
dk::UniqueSwapchain s_swapchain;

/// \brief Shader code memblock
dk::UniqueMemBlock s_codeMemBlock;
/// \brief Shaders (vertex, fragment)
dk::Shader s_shaders[2];

/// \brief UBO memblock
dk::UniqueMemBlock s_uboMemBlock;

/// \brief Vertex data memblock
dk::UniqueMemBlock s_vtxMemBlock[FB_NUM];
/// \brief Index data memblock
dk::UniqueMemBlock s_idxMemBlock[FB_NUM];
/// \brief Command buffer memblock
dk::UniqueMemBlock s_cmdMemBlock[FB_NUM];
/// \brief Command buffers
dk::UniqueCmdBuf s_cmdBuf[FB_NUM];

/// \brief Image memblock
dk::UniqueMemBlock s_imageMemBlock;
/// \brief Image/Sampler descriptor memblock
dk::UniqueMemBlock s_descriptorMemBlock;

/// \brief deko3d queue
dk::UniqueQueue s_queue;

/// \brief Maximum number of samplers
constexpr auto MAX_SAMPLERS = 1;
/// \brief Maximum number of images
constexpr auto MAX_IMAGES = 2;
/// \brief Sample descriptors
dk::SamplerDescriptor *s_samplerDescriptors = nullptr;
/// \brief Image descriptors
dk::ImageDescriptor *s_imageDescriptors = nullptr;

/// \brief Currently bound image descriptor
std::uintptr_t s_boundDescriptor = 0;

/// \brief Framebuffer width
unsigned s_width = 0;
/// \brief Framebuffer height
unsigned s_height = 0;

/// \brief Align value
/// \tparam T Value type
/// \tparam U Alignment type
/// \param size_ Value to align
/// \param align_ Alignment
template <typename T, typename U>
constexpr inline std::uint32_t align (T const &size_, U const &align_)
{
	return static_cast<std::uint32_t> (size_ + align_ - 1) & ~(align_ - 1);
}

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
		    align (depthSize, std::max<unsigned> (depthAlign, DK_MEMBLOCK_ALIGNMENT))}
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
		    align (FB_NUM * fbSize, std::max<unsigned> (fbAlign, DK_MEMBLOCK_ALIGNMENT))}
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

/// \brief Load shader code
void loadShaders ()
{
	/// \brief Shader file descriptor
	struct ShaderFile
	{
		/// \brief Parameterized constructor
		/// \param shader_ Shader object
		/// \param path_ Path to source code
		ShaderFile (dk::Shader &shader_, char const *const path_)
		    : shader (shader_), path (path_), size (getSize (path_))
		{
		}

		/// \brief Get size of a file
		/// \param path_ Path to file
		static std::size_t getSize (char const *const path_)
		{
			struct stat st;
			auto const rc = stat (path_, &st);
			if (rc != 0)
			{
				std::fprintf (stderr, "stat(%s): %s\n", path_, std::strerror (errno));
				std::abort ();
			}

			return st.st_size;
		}

		/// \brief Shader object
		dk::Shader &shader;
		/// \brief Path to source code
		char const *const path;
		/// \brief Source code file size
		std::size_t const size;
	};

	auto shaderFiles = {ShaderFile{s_shaders[0], "romfs:/shaders/imgui_vsh.dksh"},
	    ShaderFile{s_shaders[1], "romfs:/shaders/imgui_fsh.dksh"}};

	// calculate total size of shaders
	auto const codeSize = std::accumulate (std::begin (shaderFiles),
	    std::end (shaderFiles),
	    DK_SHADER_CODE_UNUSABLE_SIZE,
	    [] (auto const sum_, auto const &file_) {
		    return sum_ + align (file_.size, DK_SHADER_CODE_ALIGNMENT);
	    });

	// create shader code memblock
	s_codeMemBlock = dk::MemBlockMaker{s_device, align (codeSize, DK_MEMBLOCK_ALIGNMENT)}
	                     .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached |
	                                DkMemBlockFlags_Code)
	                     .create ();

	auto const addr    = static_cast<std::uint8_t *> (s_codeMemBlock.getCpuAddr ());
	std::size_t offset = 0;

	// read shaders into memblock
	for (auto &file : shaderFiles)
	{
		std::uint32_t const codeOffset = offset;

		fs::File fp;
		if (!fp.open (file.path))
		{
			std::fprintf (stderr, "open(%s): %s\n", file.path, std::strerror (errno));
			std::abort ();
		}

		if (!fp.readAll (&addr[offset], file.size))
		{
			std::fprintf (stderr, "read(%s): %s\n", file.path, std::strerror (errno));
			std::abort ();
		}

		dk::ShaderMaker{s_codeMemBlock, codeOffset}.initialize (file.shader);

		offset = align (offset + file.size, DK_SHADER_CODE_ALIGNMENT);
	}
}

/// \brief Setup render state
/// \param slot_ Swapchain slot
/// \param drawData_ Data to draw
/// \param width_ Framebuffer width
/// \param height_ Framebuffer height
DkCmdList setupRenderState (int const slot_,
    ImDrawData *const drawData_,
    unsigned const width_,
    unsigned const height_)
{
	// setup viewport, orthographic projection matrix
	// our visible imgui space lies from drawData_->DisplayPos (top left) to
	// drawData_->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single
	// viewport apps.
	auto const L = drawData_->DisplayPos.x;
	auto const R = drawData_->DisplayPos.x + drawData_->DisplaySize.x;
	auto const T = drawData_->DisplayPos.y;
	auto const B = drawData_->DisplayPos.y + drawData_->DisplaySize.y;

	VertUBO vertUBO;
	vertUBO.projMtx = glm::orthoRH_ZO (L, R, B, T, -1.0f, 1.0f);

	// create command buffer to initialize/reset render state
	s_cmdBuf[slot_].setViewports (0, DkViewport{0.0f, 0.0f, width_, height_});
	s_cmdBuf[slot_].bindShaders (DkStageFlag_GraphicsMask, {&s_shaders[0], &s_shaders[1]});
	s_cmdBuf[slot_].bindUniformBuffer (DkStage_Vertex,
	    0,
	    s_uboMemBlock.getGpuAddr (),
	    align (sizeof (VertUBO), DK_UNIFORM_BUF_ALIGNMENT));
	s_cmdBuf[slot_].pushConstants (s_uboMemBlock.getGpuAddr (),
	    align (sizeof (VertUBO), DK_UNIFORM_BUF_ALIGNMENT),
	    0,
	    sizeof (VertUBO),
	    &vertUBO);
	s_cmdBuf[slot_].bindUniformBuffer (DkStage_Fragment,
	    0,
	    s_uboMemBlock.getGpuAddr () + align (sizeof (VertUBO), DK_UNIFORM_BUF_ALIGNMENT),
	    align (sizeof (FragUBO), DK_UNIFORM_BUF_ALIGNMENT));
	s_cmdBuf[slot_].bindRasterizerState (dk::RasterizerState{}.setCullMode (DkFace_None));
	s_cmdBuf[slot_].bindColorState (dk::ColorState{}.setBlendEnable (0, true));
	s_cmdBuf[slot_].bindColorWriteState (dk::ColorWriteState{});
	s_cmdBuf[slot_].bindDepthStencilState (dk::DepthStencilState{}.setDepthTestEnable (false));
	s_cmdBuf[slot_].bindBlendStates (0,
	    dk::BlendState{}.setFactors (DkBlendFactor_SrcAlpha,
	        DkBlendFactor_InvSrcAlpha,
	        DkBlendFactor_InvSrcAlpha,
	        DkBlendFactor_Zero));
	s_cmdBuf[slot_].bindVtxAttribState (VERTEX_ATTRIB_STATE);
	s_cmdBuf[slot_].bindVtxBufferState (VERTEX_BUFFER_STATE);

	return s_cmdBuf[slot_].finishList ();
}
}

void imgui::deko3d::init ()
{
	auto &io = ImGui::GetIO ();

	// setup back-end capabilities flags
	io.BackendRendererName = "deko3d";
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

	// defer initialization to first newFrame ()
}

void imgui::deko3d::exit ()
{
	// wait for queue to be idle
	s_queue.waitIdle ();

	// clean up all of the deko3d objects
	s_queue              = nullptr;
	s_descriptorMemBlock = nullptr;
	s_imageMemBlock      = nullptr;

	for (unsigned i = 0; i < FB_NUM; ++i)
	{
		s_cmdBuf[i]      = nullptr;
		s_cmdMemBlock[i] = nullptr;
		s_idxMemBlock[i] = nullptr;
		s_vtxMemBlock[i] = nullptr;
	}

	s_uboMemBlock   = nullptr;
	s_codeMemBlock  = nullptr;
	s_swapchain     = nullptr;
	s_fbMemBlock    = nullptr;
	s_depthMemBlock = nullptr;
	s_device        = nullptr;
}

void imgui::deko3d::newFrame ()
{
	if (s_device)
		return;

	// create deko3d device
	s_device = dk::DeviceMaker{}.create ();

	// initialize swapchain with maximum resolution
	rebuildSwapchain (1920, 1080);

	// load shader code
	loadShaders ();

	// create UBO memblock
	s_uboMemBlock = dk::MemBlockMaker{s_device,
	    align (align (sizeof (VertUBO), DK_UNIFORM_BUF_ALIGNMENT) +
	               align (sizeof (FragUBO), DK_UNIFORM_BUF_ALIGNMENT),
	        DK_MEMBLOCK_ALIGNMENT)}
	                    .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
	                    .create ();

	// create memblocks for each framebuffer slot
	for (std::size_t i = 0; i < FB_NUM; ++i)
	{
		// create vertex data memblock
		s_vtxMemBlock[i] = dk::MemBlockMaker{s_device, align (DATABUF_SIZE, DK_MEMBLOCK_ALIGNMENT)}
		                       .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
		                       .create ();

		// create index data memblock
		s_idxMemBlock[i] = dk::MemBlockMaker{s_device, align (INDEXBUF_SIZE, DK_MEMBLOCK_ALIGNMENT)}
		                       .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
		                       .create ();

		// create command buffer memblock
		s_cmdMemBlock[i] = dk::MemBlockMaker{s_device, align (CMDBUF_SIZE, DK_MEMBLOCK_ALIGNMENT)}
		                       .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
		                       .create ();

		// create command buffer
		s_cmdBuf[i] = dk::CmdBufMaker{s_device}.create ();
		s_cmdBuf[i].addMemory (s_cmdMemBlock[i], 0, s_cmdMemBlock[i].getSize ());
	}

	// create queue
	s_queue = dk::QueueMaker{s_device}.setFlags (DkQueueFlags_Graphics).create ();

	// create image memblock
	s_imageMemBlock = dk::MemBlockMaker{s_device, align (IMAGEBUF_SIZE, DK_MEMBLOCK_ALIGNMENT)}
	                      .setFlags (DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
	                      .create ();

	// get texture atlas
	auto &io = ImGui::GetIO ();
	io.Fonts->SetTexID (nullptr);
	unsigned char *pixels;
	int width;
	int height;
	io.Fonts->GetTexDataAsAlpha8 (&pixels, &width, &height);

	// create memblock for transfer
	dk::UniqueMemBlock memBlock =
	    dk::MemBlockMaker{s_device, align (width * height, DK_MEMBLOCK_ALIGNMENT)}
	        .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
	        .create ();
	std::memcpy (memBlock.getCpuAddr (), pixels, width * height);

	// create image/sampler memblock
	static_assert (sizeof (dk::ImageDescriptor) == DK_IMAGE_DESCRIPTOR_ALIGNMENT);
	static_assert (sizeof (dk::SamplerDescriptor) == DK_SAMPLER_DESCRIPTOR_ALIGNMENT);
	static_assert (DK_IMAGE_DESCRIPTOR_ALIGNMENT == DK_SAMPLER_DESCRIPTOR_ALIGNMENT);
	s_descriptorMemBlock = dk::MemBlockMaker{s_device,
	    align ((MAX_SAMPLERS + MAX_IMAGES) * sizeof (dk::ImageDescriptor), DK_MEMBLOCK_ALIGNMENT)}
	                           .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
	                           .create ();

	// get cpu address for descriptors
	s_samplerDescriptors =
	    static_cast<dk::SamplerDescriptor *> (s_descriptorMemBlock.getCpuAddr ());
	s_imageDescriptors =
	    reinterpret_cast<dk::ImageDescriptor *> (&s_samplerDescriptors[MAX_SAMPLERS]);

	// initialize sampler descriptor
	s_samplerDescriptors[0].initialize (
	    dk::Sampler{}
	        .setFilter (DkFilter_Linear, DkFilter_Linear)
	        .setWrapMode (DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge));

	// use command buffer 0 for initialization
	auto &cmdBuf = s_cmdBuf[0];

	// initialize texture atlas image layout
	dk::ImageLayout layout;
	dk::ImageLayoutMaker{s_device}
	    .setFlags (0)
	    .setFormat (DkImageFormat_R8_Unorm)
	    .setDimensions (width, height)
	    .initialize (layout);

	// initialize font texture atlas image descriptor
	s_fontTexture.initialize (layout, s_imageMemBlock, 0);
	s_imageDescriptors[0].initialize (s_fontTexture);

	// copy font texture atlas to image view
	dk::ImageView imageView{s_fontTexture};
	cmdBuf.copyBufferToImage ({memBlock.getGpuAddr ()}, imageView, {0, 0, 0, width, height, 1});

	// bind image/sampler descriptors
	cmdBuf.bindSamplerDescriptorSet (s_descriptorMemBlock.getGpuAddr (), MAX_SAMPLERS);
	cmdBuf.bindImageDescriptorSet (
	    s_descriptorMemBlock.getGpuAddr () + MAX_SAMPLERS * sizeof (dk::SamplerDescriptor),
	    MAX_IMAGES);

	// submit commands while we get the next image ready to transfer
	s_queue.submitCommands (cmdBuf.finishList ());

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
		    dk::MemBlockMaker{s_device, align (size, DK_MEMBLOCK_ALIGNMENT)}
		        .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
		        .create ();

		// decompress into transfer memblock
		auto const decoded =
		    ZSTD_decompress (memBlock.getCpuAddr (), size, buffer.data (), st.st_size);
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

		// initialize deko3d logo texture image descriptor
		auto const offset = align (width * height, DK_IMAGE_LINEAR_STRIDE_ALIGNMENT);
		s_logoTexture.initialize (layout, s_imageMemBlock, offset);
		s_imageDescriptors[1].initialize (s_logoTexture);

		// copy deko3d logo texture to image view
		dk::ImageView imageView{s_logoTexture};
		cmdBuf.copyBufferToImage (
		    {memBlock.getGpuAddr ()}, imageView, {0, 0, 0, LOGO_WIDTH, LOGO_HEIGHT, 1});

		// submit commands to transfer deko3d logo texture
		s_queue.submitCommands (cmdBuf.finishList ());

		// wait for commands to complete before releasing memblocks
		s_queue.waitIdle ();
	}

	// reset command buffer
	cmdBuf.clear ();
}

void imgui::deko3d::render ()
{
	// get ImGui draw data
	auto const drawData = ImGui::GetDrawData ();
	if (drawData->CmdListsCount <= 0)
		return;

	// get framebuffer dimensions
	unsigned width  = drawData->DisplaySize.x * drawData->FramebufferScale.x;
	unsigned height = drawData->DisplaySize.y * drawData->FramebufferScale.y;
	if (width <= 0 || height <= 0)
		return;

	// check if we need to rebuild the swapchain
	if (width != s_width || height != s_height)
	{
		s_width  = width;
		s_height = height;

		s_queue.waitIdle ();
		rebuildSwapchain (width, height);
	}

	// get image from queue
	auto const slot = s_queue.acquireImage (s_swapchain);
	s_cmdBuf[slot].clear ();

	// bind frame/depth buffers and clear them
	dk::ImageView colorTarget{s_frameBuffers[slot]};
	dk::ImageView depthTarget{s_depthBuffer};
	s_cmdBuf[slot].bindRenderTargets (&colorTarget, &depthTarget);
	s_cmdBuf[slot].clearColor (0, DkColorMask_RGBA, 0.125f, 0.294f, 0.478f, 1.0f);
	s_cmdBuf[slot].clearDepthStencil (true, 1.0f, 0xFF, 0);
	s_queue.submitCommands (s_cmdBuf[slot].finishList ());

	// setup desired render state
	auto const setupCmd = setupRenderState (slot, drawData, width, height);
	s_queue.submitCommands (setupCmd);

	// start with bogus descriptor binding so it'll be updated before first draw call
	s_boundDescriptor = ~static_cast<std::uintptr_t> (0);

	// will project scissor/clipping rectangles into framebuffer space
	// (0,0) unless using multi-viewports
	auto const clipOff = drawData->DisplayPos;
	// (1,1) unless using retina display which are often (2,2)
	auto const clipScale = drawData->FramebufferScale;

	// check if we need to grow vertex data memblock
	if (s_vtxMemBlock[slot].getSize () < drawData->TotalVtxCount * sizeof (ImDrawVert))
	{
		s_vtxMemBlock[slot] =
		    dk::MemBlockMaker{s_device,
		        align (drawData->TotalVtxCount * sizeof (ImDrawVert), DK_MEMBLOCK_ALIGNMENT)}
		        .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
		        .create ();
	}

	// check if we need to grow index data memblock
	if (s_idxMemBlock[slot].getSize () < drawData->TotalIdxCount * sizeof (ImDrawIdx))
	{
		s_idxMemBlock[slot] =
		    dk::MemBlockMaker{s_device,
		        align (drawData->TotalIdxCount * sizeof (ImDrawIdx), DK_MEMBLOCK_ALIGNMENT)}
		        .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
		        .create ();
	}

	// get base cpu addresses
	auto const cpuVtx = static_cast<std::uint8_t *> (s_vtxMemBlock[slot].getCpuAddr ());
	auto const cpuIdx = static_cast<std::uint8_t *> (s_idxMemBlock[slot].getCpuAddr ());

	// get base gpu addresses
	auto const gpuVtx = s_vtxMemBlock[slot].getGpuAddr ();
	auto const gpuIdx = s_idxMemBlock[slot].getGpuAddr ();

	// get memblock sizes
	auto const sizeVtx = s_vtxMemBlock[slot].getSize ();
	auto const sizeIdx = s_idxMemBlock[slot].getSize ();

	// bind vertex/index data memblocks
	static_assert (sizeof (ImDrawIdx) == 2);
	s_cmdBuf[slot].bindVtxBuffer (0, gpuVtx, sizeVtx);
	s_cmdBuf[slot].bindIdxBuffer (DkIdxFormat_Uint16, gpuIdx);

	// render command lists
	std::size_t offsetVtx = 0;
	std::size_t offsetIdx = 0;
	for (int i = 0; i < drawData->CmdListsCount; ++i)
	{
		auto const &cmdList = *drawData->CmdLists[i];

		auto const vtxSize = cmdList.VtxBuffer.Size * sizeof (ImDrawVert);
		auto const idxSize = cmdList.IdxBuffer.Size * sizeof (ImDrawIdx);

		// double check that we don't overrun vertex data memblock
		if (sizeVtx - offsetVtx < vtxSize)
		{
			std::fprintf (stderr, "Not enough vertex buffer\n");
			std::fprintf (stderr, "\t%zu/%u used, need %zu\n", offsetVtx, sizeVtx, vtxSize);
			continue;
		}

		// double check that we don't overrun index data memblock
		if (sizeIdx - offsetIdx < idxSize)
		{
			std::fprintf (stderr, "Not enough index buffer\n");
			std::fprintf (stderr, "\t%zu/%u used, need %zu\n", offsetIdx, sizeIdx, idxSize);
			continue;
		}

		// copy vertex/index data into memblocks
		std::memcpy (cpuVtx + offsetVtx, cmdList.VtxBuffer.Data, vtxSize);
		std::memcpy (cpuIdx + offsetIdx, cmdList.IdxBuffer.Data, idxSize);

		for (auto const &cmd : cmdList.CmdBuffer)
		{
			if (cmd.UserCallback)
			{
				// submit commands to preserve ordering
				s_queue.submitCommands (s_cmdBuf[slot].finishList ());

				// user callback, registered via ImDrawList::AddCallback()
				// (ImDrawCallback_ResetRenderState is a special callback value used by the user to
				// request the renderer to reset render state.)
				if (cmd.UserCallback == ImDrawCallback_ResetRenderState)
					s_queue.submitCommands (setupCmd);
				else
					cmd.UserCallback (&cmdList, &cmd);
			}
			else
			{
				// project scissor/clipping rectangles into framebuffer space
				ImVec4 clip;
				clip.x = (cmd.ClipRect.x - clipOff.x) * clipScale.x;
				clip.y = (cmd.ClipRect.y - clipOff.y) * clipScale.y;
				clip.z = (cmd.ClipRect.z - clipOff.x) * clipScale.x;
				clip.w = (cmd.ClipRect.w - clipOff.y) * clipScale.y;

				if (clip.x >= width || clip.y >= height || clip.z < 0.0f || clip.w < 0.0f)
					continue;

				// keep scissor coordinates inside viewport
				if (clip.x < 0.0f)
					clip.x = 0.0f;
				if (clip.y < 0.0f)
					clip.y = 0.0f;
				if (clip.z > width)
					clip.z = width;
				if (clip.w > height)
					clip.z = height;

				// apply scissor boundaries
				s_cmdBuf[slot].setScissors (
				    0, DkScissor{clip.x, clip.y, clip.z - clip.x, clip.w - clip.y});

				// get image descriptor
				auto const descriptor = reinterpret_cast<std::uintptr_t> (cmd.TextureId);
				if (descriptor >= MAX_IMAGES)
					continue;

				// check if we need to bind a new texture
				if (descriptor != s_boundDescriptor)
				{
					s_boundDescriptor = descriptor;

					// bind the new texture
					s_cmdBuf[slot].bindTextures (
					    DkStage_Fragment, 0, dkMakeTextureHandle (descriptor, 0));

					// check if this is the font texture atlas image descriptor
					FragUBO fragUBO;
					fragUBO.font = (descriptor == 0);

					// update fragment shader UBO
					s_cmdBuf[slot].pushConstants (
					    s_uboMemBlock.getGpuAddr () +
					        align (sizeof (VertUBO), DK_UNIFORM_BUF_ALIGNMENT),
					    align (sizeof (FragUBO), DK_UNIFORM_BUF_ALIGNMENT),
					    0,
					    sizeof (FragUBO),
					    &fragUBO);
				}

				// draw the draw list
				s_cmdBuf[slot].drawIndexed (DkPrimitive_Triangles,
				    cmd.ElemCount,
				    1,
				    cmd.IdxOffset + offsetIdx / sizeof (ImDrawIdx),
				    cmd.VtxOffset + offsetVtx / sizeof (ImDrawVert),
				    0);
			}
		}

		offsetVtx += vtxSize;
		offsetIdx += idxSize;
	}

	// wait for fragments to be completed before discarding depth/stencil buffer
	s_cmdBuf[slot].barrier (DkBarrier_Fragments, 0);
	s_cmdBuf[slot].discardDepthStencil ();

	// submit final commands
	s_queue.submitCommands (s_cmdBuf[slot].finishList ());

	// present image
	s_queue.presentImage (s_swapchain, slot);
}

void imgui::deko3d::test ()
{
	auto const x1 = (s_width - LOGO_WIDTH) / 2.0f;
	auto const x2 = x1 + LOGO_WIDTH;
	auto const y1 = (s_height - LOGO_HEIGHT) / 2.0f;
	auto const y2 = y1 + LOGO_HEIGHT;

	ImGui::GetBackgroundDrawList ()->AddImage (
	    reinterpret_cast<ImTextureID> (1), ImVec2 (x1, y1), ImVec2 (x2, y2));
}
