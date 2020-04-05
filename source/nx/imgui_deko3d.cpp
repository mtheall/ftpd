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
constexpr auto LOGO_WIDTH  = 500;
constexpr auto LOGO_HEIGHT = 493;

constexpr auto FB_NUM = 2u;

constexpr auto CMDBUF_SIZE   = 1024 * 1024;
constexpr auto DATABUF_SIZE  = 1024 * 1024;
constexpr auto INDEXBUF_SIZE = 1024 * 1024;
constexpr auto IMAGEBUF_SIZE = 16 * 1024 * 1024;

struct VertUBO
{
	glm::mat4 projMtx;
};

struct FragUBO
{
	std::uint32_t font;
};

constexpr std::array VertexAttribState = {
    // clang-format off
    DkVtxAttribState{0, 0, offsetof (ImDrawVert, pos), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
    DkVtxAttribState{0, 0, offsetof (ImDrawVert, uv),  DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
    DkVtxAttribState{0, 0, offsetof (ImDrawVert, col), DkVtxAttribSize_4x8,  DkVtxAttribType_Unorm, 0},
    // clang-format on
};

constexpr std::array VertexBufferState = {
    DkVtxBufferState{sizeof (ImDrawVert), 0},
};

dk::UniqueDevice s_device;

dk::UniqueMemBlock s_depthMemBlock;
dk::Image s_depthBuffer;

dk::UniqueMemBlock s_fbMemBlock;
dk::Image s_frameBuffers[FB_NUM];

dk::Image s_fontTexture;
dk::Image s_logoTexture;

dk::UniqueSwapchain s_swapchain;

dk::UniqueMemBlock s_codeMemBlock;
dk::Shader s_shaders[2];

dk::UniqueMemBlock s_uboMemBlock;

dk::UniqueMemBlock s_vtxMemBlock[FB_NUM];
dk::UniqueMemBlock s_idxMemBlock[FB_NUM];
dk::UniqueMemBlock s_cmdMemBlock[FB_NUM];
dk::UniqueCmdBuf s_cmdBuf[FB_NUM];

dk::UniqueMemBlock s_imageMemBlock;
dk::UniqueMemBlock s_descriptorMemBlock;

dk::UniqueQueue s_queue;

constexpr auto MAX_SAMPLERS                 = 1;
constexpr auto MAX_IMAGES                   = 2;
dk::SamplerDescriptor *s_samplerDescriptors = nullptr;
dk::ImageDescriptor *s_imageDescriptors     = nullptr;

std::uintptr_t s_boundDescriptor = 0;

unsigned s_width  = 0;
unsigned s_height = 0;

template <typename T, typename U>
constexpr inline std::uint32_t align (T const &size_, U const &align_)
{
	return static_cast<std::uint32_t> (size_ + align_ - 1) & ~(align_ - 1);
}

void rebuildSwapchain (unsigned const width_, unsigned const height_)
{
	s_swapchain = nullptr;

	dk::ImageLayout depthLayout;
	dk::ImageLayoutMaker{s_device}
	    .setFlags (DkImageFlags_UsageRender | DkImageFlags_HwCompression)
	    .setFormat (DkImageFormat_Z24S8)
	    .setDimensions (width_, height_)
	    .initialize (depthLayout);

	auto const depthAlign = depthLayout.getAlignment ();
	auto const depthSize  = depthLayout.getSize ();

	if (!s_depthMemBlock)
	{
		s_depthMemBlock = dk::MemBlockMaker{s_device,
		    align (depthSize, std::max<unsigned> (depthAlign, DK_MEMBLOCK_ALIGNMENT))}
		                      .setFlags (DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
		                      .create ();
	}

	s_depthBuffer.initialize (depthLayout, s_depthMemBlock, 0);

	dk::ImageLayout fbLayout;
	dk::ImageLayoutMaker{s_device}
	    .setFlags (
	        DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression)
	    .setFormat (DkImageFormat_RGBA8_Unorm)
	    .setDimensions (width_, height_)
	    .initialize (fbLayout);

	auto const fbAlign = fbLayout.getAlignment ();
	auto const fbSize  = fbLayout.getSize ();

	if (!s_fbMemBlock)
	{
		s_fbMemBlock = dk::MemBlockMaker{s_device,
		    align (FB_NUM * fbSize, std::max<unsigned> (fbAlign, DK_MEMBLOCK_ALIGNMENT))}
		                   .setFlags (DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
		                   .create ();
	}

	std::array<DkImage const *, FB_NUM> swapchainImages;
	for (unsigned i = 0; i < FB_NUM; ++i)
	{
		swapchainImages[i] = &s_frameBuffers[i];
		s_frameBuffers[i].initialize (fbLayout, s_fbMemBlock, i * fbSize);
	}

	s_swapchain = dk::SwapchainMaker{s_device, nwindowGetDefault (), swapchainImages}.create ();
}

void loadShaders ()
{
	struct ShaderFile
	{
		ShaderFile (dk::Shader &shader_, char const *const path_)
		    : shader (shader_), path (path_), size (getSize (path_))
		{
		}

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

		dk::Shader &shader;
		char const *const path;
		std::size_t const size;
	};

	auto shaderFiles = {ShaderFile{s_shaders[0], "romfs:/shaders/imgui_vsh.dksh"},
	    ShaderFile{s_shaders[1], "romfs:/shaders/imgui_fsh.dksh"}};

	auto const codeSize = std::accumulate (std::begin (shaderFiles),
	    std::end (shaderFiles),
	    DK_SHADER_CODE_UNUSABLE_SIZE,
	    [] (auto const sum_, auto const &file_) {
		    return sum_ + align (file_.size, DK_SHADER_CODE_ALIGNMENT);
	    });

	s_codeMemBlock = dk::MemBlockMaker{s_device, align (codeSize, DK_MEMBLOCK_ALIGNMENT)}
	                     .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached |
	                                DkMemBlockFlags_Code)
	                     .create ();

	auto const addr    = static_cast<std::uint8_t *> (s_codeMemBlock.getCpuAddr ());
	std::size_t offset = 0;

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

DkCmdList setupRenderState (int const slot_,
    ImDrawData *const drawData_,
    unsigned const width_,
    unsigned const height_)
{
	// Setup viewport, orthographic projection matrix
	// Our visible imgui space lies from drawData_->DisplayPos (top left) to
	// drawData_->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single
	// viewport apps.
	auto const L = drawData_->DisplayPos.x;
	auto const R = drawData_->DisplayPos.x + drawData_->DisplaySize.x;
	auto const T = drawData_->DisplayPos.y;
	auto const B = drawData_->DisplayPos.y + drawData_->DisplaySize.y;

	VertUBO vertUBO;
	vertUBO.projMtx = glm::orthoRH_ZO (L, R, B, T, -1.0f, 1.0f);

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
	s_cmdBuf[slot_].bindVtxAttribState (VertexAttribState);
	s_cmdBuf[slot_].bindVtxBufferState (VertexBufferState);

	return s_cmdBuf[slot_].finishList ();
}
}

void imgui::deko3d::init ()
{
	// Setup back-end capabilities flags
	ImGuiIO &io = ImGui::GetIO ();

	io.BackendRendererName = "deko3d";
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
}

void imgui::deko3d::exit ()
{
	s_queue.waitIdle ();

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

	s_device = dk::DeviceMaker{}.create ();

	rebuildSwapchain (1920, 1080);

	loadShaders ();

	s_uboMemBlock = dk::MemBlockMaker{s_device,
	    align (align (sizeof (VertUBO), DK_UNIFORM_BUF_ALIGNMENT) +
	               align (sizeof (FragUBO), DK_UNIFORM_BUF_ALIGNMENT),
	        DK_MEMBLOCK_ALIGNMENT)}
	                    .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
	                    .create ();

	for (std::size_t i = 0; i < FB_NUM; ++i)
	{
		s_vtxMemBlock[i] = dk::MemBlockMaker{s_device, align (DATABUF_SIZE, DK_MEMBLOCK_ALIGNMENT)}
		                       .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
		                       .create ();

		s_idxMemBlock[i] = dk::MemBlockMaker{s_device, align (INDEXBUF_SIZE, DK_MEMBLOCK_ALIGNMENT)}
		                       .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
		                       .create ();

		s_cmdMemBlock[i] = dk::MemBlockMaker{s_device, align (CMDBUF_SIZE, DK_MEMBLOCK_ALIGNMENT)}
		                       .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
		                       .create ();

		s_cmdBuf[i] = dk::CmdBufMaker{s_device}.create ();

		s_cmdBuf[i].addMemory (s_cmdMemBlock[i], 0, s_cmdMemBlock[i].getSize ());
	}

	s_queue = dk::QueueMaker{s_device}.setFlags (DkQueueFlags_Graphics).create ();

	s_imageMemBlock = dk::MemBlockMaker{s_device, align (IMAGEBUF_SIZE, DK_MEMBLOCK_ALIGNMENT)}
	                      .setFlags (DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
	                      .create ();

	// Build texture atlas
	ImGuiIO &io = ImGui::GetIO ();
	io.Fonts->SetTexID (nullptr);
	unsigned char *pixels;
	int width;
	int height;
	io.Fonts->GetTexDataAsAlpha8 (&pixels, &width, &height);

	dk::UniqueMemBlock memBlock =
	    dk::MemBlockMaker{s_device, align (width * height, DK_MEMBLOCK_ALIGNMENT)}
	        .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
	        .create ();
	std::memcpy (memBlock.getCpuAddr (), pixels, width * height);

	static_assert (sizeof (dk::ImageDescriptor) == DK_IMAGE_DESCRIPTOR_ALIGNMENT);
	static_assert (sizeof (dk::SamplerDescriptor) == DK_SAMPLER_DESCRIPTOR_ALIGNMENT);
	static_assert (DK_IMAGE_DESCRIPTOR_ALIGNMENT == DK_SAMPLER_DESCRIPTOR_ALIGNMENT);
	s_descriptorMemBlock = dk::MemBlockMaker{s_device,
	    align ((MAX_SAMPLERS + MAX_IMAGES) * sizeof (dk::ImageDescriptor), DK_MEMBLOCK_ALIGNMENT)}
	                           .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
	                           .create ();

	s_samplerDescriptors =
	    static_cast<dk::SamplerDescriptor *> (s_descriptorMemBlock.getCpuAddr ());
	s_imageDescriptors =
	    reinterpret_cast<dk::ImageDescriptor *> (&s_samplerDescriptors[MAX_SAMPLERS]);

	s_samplerDescriptors[0].initialize (
	    dk::Sampler{}
	        .setFilter (DkFilter_Linear, DkFilter_Linear)
	        .setWrapMode (DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge));

	auto &cmdBuf = s_cmdBuf[0];
	dk::ImageLayout layout;
	dk::ImageLayoutMaker{s_device}
	    .setFlags (0)
	    .setFormat (DkImageFormat_R8_Unorm)
	    .setDimensions (width, height)
	    .initialize (layout);

	s_fontTexture.initialize (layout, s_imageMemBlock, 0);
	s_imageDescriptors[0].initialize (s_fontTexture);

	dk::ImageView imageView{s_fontTexture};
	cmdBuf.copyBufferToImage ({memBlock.getGpuAddr ()}, imageView, {0, 0, 0, width, height, 1});

	cmdBuf.bindSamplerDescriptorSet (s_descriptorMemBlock.getGpuAddr (), MAX_SAMPLERS);
	cmdBuf.bindImageDescriptorSet (
	    s_descriptorMemBlock.getGpuAddr () + MAX_SAMPLERS * sizeof (dk::SamplerDescriptor),
	    MAX_IMAGES);

	s_queue.submitCommands (cmdBuf.finishList ());
	s_queue.waitIdle ();

	{
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

		memBlock = dk::MemBlockMaker{s_device, align (size, DK_MEMBLOCK_ALIGNMENT)}
		               .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
		               .create ();

		auto const decoded =
		    ZSTD_decompress (memBlock.getCpuAddr (), size, buffer.data (), st.st_size);
		if (ZSTD_isError (decoded))
		{
			std::fprintf (stderr, "ZSTD_decompress: %s\n", ZSTD_getErrorName (decoded));
			std::abort ();
		}

		dk::ImageLayout layout;
		dk::ImageLayoutMaker{s_device}
		    .setFlags (0)
		    .setFormat (DkImageFormat_RGBA8_Unorm)
		    .setDimensions (LOGO_WIDTH, LOGO_HEIGHT)
		    .initialize (layout);

		auto const offset = align (width * height, DK_IMAGE_LINEAR_STRIDE_ALIGNMENT);
		s_logoTexture.initialize (layout, s_imageMemBlock, offset);
		s_imageDescriptors[1].initialize (s_logoTexture);

		dk::ImageView imageView{s_logoTexture};
		cmdBuf.copyBufferToImage (
		    {memBlock.getGpuAddr ()}, imageView, {0, 0, 0, LOGO_WIDTH, LOGO_HEIGHT, 1});

		s_queue.submitCommands (cmdBuf.finishList ());
		s_queue.waitIdle ();
	}

	cmdBuf.clear ();
}

void imgui::deko3d::render ()
{
	auto const drawData = ImGui::GetDrawData ();
	if (drawData->CmdListsCount <= 0)
		return;

	unsigned width  = drawData->DisplaySize.x * drawData->FramebufferScale.x;
	unsigned height = drawData->DisplaySize.y * drawData->FramebufferScale.y;
	if (width <= 0 || height <= 0)
		return;

	if (width != s_width || height != s_height)
	{
		s_width  = width;
		s_height = height;

		s_queue.waitIdle ();
		rebuildSwapchain (width, height);
	}

	auto const slot = s_queue.acquireImage (s_swapchain);
	s_cmdBuf[slot].clear ();

	dk::ImageView colorTarget{s_frameBuffers[slot]};
	dk::ImageView depthTarget{s_depthBuffer};
	s_cmdBuf[slot].bindRenderTargets (&colorTarget, &depthTarget);
	s_cmdBuf[slot].clearColor (0, DkColorMask_RGBA, 0.125f, 0.294f, 0.478f, 1.0f);
	s_cmdBuf[slot].clearDepthStencil (true, 1.0f, 0xFF, 0);
	s_queue.submitCommands (s_cmdBuf[slot].finishList ());

	// Setup desired render state
	auto const setupCmd = setupRenderState (slot, drawData, width, height);
	s_queue.submitCommands (setupCmd);

	s_boundDescriptor = ~static_cast<std::uintptr_t> (0);

	// Will project scissor/clipping rectangles into framebuffer space
	// (0,0) unless using multi-viewports
	auto const clipOff = drawData->DisplayPos;
	// (1,1) unless using retina display which are often (2,2)
	auto const clipScale = drawData->FramebufferScale;

	if (s_vtxMemBlock[slot].getSize () < drawData->TotalVtxCount * sizeof (ImDrawVert))
	{
		s_vtxMemBlock[slot] = nullptr;
		s_vtxMemBlock[slot] =
		    dk::MemBlockMaker{s_device,
		        align (drawData->TotalVtxCount * sizeof (ImDrawVert), DK_MEMBLOCK_ALIGNMENT)}
		        .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
		        .create ();
	}

	if (s_idxMemBlock[slot].getSize () < drawData->TotalIdxCount * sizeof (ImDrawIdx))
	{
		s_idxMemBlock[slot] = nullptr;
		s_idxMemBlock[slot] =
		    dk::MemBlockMaker{s_device,
		        align (drawData->TotalIdxCount * sizeof (ImDrawIdx), DK_MEMBLOCK_ALIGNMENT)}
		        .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
		        .create ();
	}

	auto const cpuVtx = static_cast<std::uint8_t *> (s_vtxMemBlock[slot].getCpuAddr ());
	auto const cpuIdx = static_cast<std::uint8_t *> (s_idxMemBlock[slot].getCpuAddr ());

	auto const gpuVtx = s_vtxMemBlock[slot].getGpuAddr ();
	auto const gpuIdx = s_idxMemBlock[slot].getGpuAddr ();

	auto const sizeVtx = s_vtxMemBlock[slot].getSize ();
	auto const sizeIdx = s_idxMemBlock[slot].getSize ();

	static_assert (sizeof (ImDrawIdx) == 2);
	s_cmdBuf[slot].bindVtxBuffer (0, gpuVtx, sizeVtx);
	s_cmdBuf[slot].bindIdxBuffer (DkIdxFormat_Uint16, gpuIdx);

	// Render command lists
	std::size_t offsetVtx = 0;
	std::size_t offsetIdx = 0;
	for (int i = 0; i < drawData->CmdListsCount; ++i)
	{
		auto const &cmdList = *drawData->CmdLists[i];

		auto const vtxSize = cmdList.VtxBuffer.Size * sizeof (ImDrawVert);
		auto const idxSize = cmdList.IdxBuffer.Size * sizeof (ImDrawIdx);

		if (sizeVtx - offsetVtx < vtxSize)
		{
			std::fprintf (stderr, "Not enough vertex buffer\n");
			std::fprintf (stderr, "\t%zu/%u used, need %zu\n", offsetVtx, sizeVtx, vtxSize);
			continue;
		}

		if (sizeIdx - offsetIdx < idxSize)
		{
			std::fprintf (stderr, "Not enough index buffer\n");
			std::fprintf (stderr, "\t%zu/%u used, need %zu\n", offsetIdx, sizeIdx, idxSize);
			continue;
		}

		std::memcpy (cpuVtx + offsetVtx, cmdList.VtxBuffer.Data, vtxSize);
		std::memcpy (cpuIdx + offsetIdx, cmdList.IdxBuffer.Data, idxSize);

		for (auto const &cmd : cmdList.CmdBuffer)
		{
			if (cmd.UserCallback)
			{
				s_queue.submitCommands (s_cmdBuf[slot].finishList ());

				// User callback, registered via ImDrawList::AddCallback()
				// (ImDrawCallback_ResetRenderState is a special callback value used by the user to
				// request the renderer to reset render state.)
				if (cmd.UserCallback == ImDrawCallback_ResetRenderState)
					s_queue.submitCommands (setupCmd);
				else
					cmd.UserCallback (&cmdList, &cmd);
			}
			else
			{
				// Project scissor/clipping rectangles into framebuffer space
				ImVec4 clip;
				clip.x = (cmd.ClipRect.x - clipOff.x) * clipScale.x;
				clip.y = (cmd.ClipRect.y - clipOff.y) * clipScale.y;
				clip.z = (cmd.ClipRect.z - clipOff.x) * clipScale.x;
				clip.w = (cmd.ClipRect.w - clipOff.y) * clipScale.y;

				if (clip.x < width && clip.y < height && clip.z >= 0.0f && clip.w >= 0.0f)
				{
					if (clip.x < 0.0f)
						clip.x = 0.0f;
					if (clip.y < 0.0f)
						clip.y = 0.0f;

					s_cmdBuf[slot].setScissors (
					    0, DkScissor{clip.x, clip.y, clip.z - clip.x, clip.w - clip.y});

					auto const descriptor = reinterpret_cast<std::uintptr_t> (cmd.TextureId);
					if (descriptor >= MAX_IMAGES)
						continue;

					if (descriptor != s_boundDescriptor)
					{
						s_boundDescriptor = descriptor;

						s_cmdBuf[slot].bindTextures (
						    DkStage_Fragment, 0, dkMakeTextureHandle (descriptor, 0));

						FragUBO fragUBO;
						fragUBO.font = (descriptor == 0);

						s_cmdBuf[slot].pushConstants (
						    s_uboMemBlock.getGpuAddr () +
						        align (sizeof (VertUBO), DK_UNIFORM_BUF_ALIGNMENT),
						    align (sizeof (FragUBO), DK_UNIFORM_BUF_ALIGNMENT),
						    0,
						    sizeof (FragUBO),
						    &fragUBO);
					}

					s_cmdBuf[slot].drawIndexed (DkPrimitive_Triangles,
					    cmd.ElemCount,
					    1,
					    cmd.IdxOffset + offsetIdx / sizeof (ImDrawIdx),
					    cmd.VtxOffset + offsetVtx / sizeof (ImDrawVert),
					    0);
				}
			}
		}

		offsetVtx += vtxSize;
		offsetIdx += idxSize;
	}

	s_cmdBuf[slot].barrier (DkBarrier_Fragments, 0);
	s_cmdBuf[slot].discardDepthStencil ();
	s_queue.submitCommands (s_cmdBuf[slot].finishList ());

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
