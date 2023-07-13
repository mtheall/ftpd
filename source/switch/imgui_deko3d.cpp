// ftpd is a server implementation based on the following:
// - RFC  959 (https://tools.ietf.org/html/rfc959)
// - RFC 3659 (https://tools.ietf.org/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
//
// The MIT License (MIT)
//
// Copyright (C) 2023 Michael Theall
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
#include "imgui_deko3d.h"

#include "fs.h"

#include "imgui.h"

#include <deko3d.hpp>

#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_INTRINSICS
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>

#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <optional>
#include <vector>

namespace
{
/// \brief Vertex buffer size
constexpr auto VTXBUF_SIZE = 1024u * 1024u;
/// \brief Index buffer size
constexpr auto IDXBUF_SIZE = 1024u * 1024u;

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

/// \brief Shader code memblock
dk::UniqueMemBlock s_codeMemBlock;
/// \brief Shaders (vertex, fragment)
dk::Shader s_shaders[2];

/// \brief UBO memblock
dk::UniqueMemBlock s_uboMemBlock;

/// \brief Vertex data memblock
std::vector<dk::UniqueMemBlock> s_vtxMemBlock;
/// \brief Index data memblock
std::vector<dk::UniqueMemBlock> s_idxMemBlock;

/// \brief Font image memblock
dk::UniqueMemBlock s_fontImageMemBlock;
/// \brief Font texture handle
DkResHandle s_fontTextureHandle;

/// \brief Load shader code
void loadShaders (dk::UniqueDevice &device_)
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
			auto const rc = ::stat (path_, &st);
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
		    return sum_ + imgui::deko3d::align (file_.size, DK_SHADER_CODE_ALIGNMENT);
	    });

	// create shader code memblock
	s_codeMemBlock =
	    dk::MemBlockMaker{device_, imgui::deko3d::align (codeSize, DK_MEMBLOCK_ALIGNMENT)}
	        .setFlags (
	            DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code)
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

		offset = imgui::deko3d::align (offset + file.size, DK_SHADER_CODE_ALIGNMENT);
	}
}

/// \brief Setup render state
/// \param cmdBuf_ Command buffer
/// \param drawData_ Data to draw
/// \param width_ Framebuffer width
/// \param height_ Framebuffer height
DkCmdList setupRenderState (dk::UniqueCmdBuf &cmdBuf_,
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
	cmdBuf_.setViewports (0, DkViewport{0.0f, 0.0f, (float)width_, (float)height_, 0.0f, 0.0f});
	cmdBuf_.bindShaders (DkStageFlag_GraphicsMask, {&s_shaders[0], &s_shaders[1]});
	cmdBuf_.bindUniformBuffer (DkStage_Vertex,
	    0,
	    s_uboMemBlock.getGpuAddr (),
	    imgui::deko3d::align (sizeof (VertUBO), DK_UNIFORM_BUF_ALIGNMENT));
	cmdBuf_.pushConstants (s_uboMemBlock.getGpuAddr (),
	    imgui::deko3d::align (sizeof (VertUBO), DK_UNIFORM_BUF_ALIGNMENT),
	    0,
	    sizeof (VertUBO),
	    &vertUBO);
	cmdBuf_.bindUniformBuffer (DkStage_Fragment,
	    0,
	    s_uboMemBlock.getGpuAddr () +
	        imgui::deko3d::align (sizeof (VertUBO), DK_UNIFORM_BUF_ALIGNMENT),
	    imgui::deko3d::align (sizeof (FragUBO), DK_UNIFORM_BUF_ALIGNMENT));
	cmdBuf_.bindRasterizerState (dk::RasterizerState{}.setCullMode (DkFace_None));
	cmdBuf_.bindColorState (dk::ColorState{}.setBlendEnable (0, true));
	cmdBuf_.bindColorWriteState (dk::ColorWriteState{});
	cmdBuf_.bindDepthStencilState (dk::DepthStencilState{}.setDepthTestEnable (false));
	cmdBuf_.bindBlendStates (0,
	    dk::BlendState{}.setFactors (DkBlendFactor_SrcAlpha,
	        DkBlendFactor_InvSrcAlpha,
	        DkBlendFactor_InvSrcAlpha,
	        DkBlendFactor_Zero));
	cmdBuf_.bindVtxAttribState (VERTEX_ATTRIB_STATE);
	cmdBuf_.bindVtxBufferState (VERTEX_BUFFER_STATE);

	return cmdBuf_.finishList ();
}
}

void imgui::deko3d::init (dk::UniqueDevice &device_,
    dk::UniqueQueue &queue_,
    dk::UniqueCmdBuf &cmdBuf_,
    dk::SamplerDescriptor &samplerDescriptor_,
    dk::ImageDescriptor &imageDescriptor_,
    DkResHandle fontTextureHandle_,
    unsigned const imageCount_)
{
	auto &io = ImGui::GetIO ();

	// setup back-end capabilities flags
	io.BackendRendererName = "deko3d";
	io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

	// load shader code
	loadShaders (device_);

	// create UBO memblock
	s_uboMemBlock = dk::MemBlockMaker{device_,
	    align (align (sizeof (VertUBO), DK_UNIFORM_BUF_ALIGNMENT) +
	               align (sizeof (FragUBO), DK_UNIFORM_BUF_ALIGNMENT),
	        DK_MEMBLOCK_ALIGNMENT)}
	                    .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
	                    .create ();

	// create memblocks for each image slot
	for (std::size_t i = 0; i < imageCount_; ++i)
	{
		// create vertex data memblock
		s_vtxMemBlock.emplace_back (
		    dk::MemBlockMaker{device_, align (VTXBUF_SIZE, DK_MEMBLOCK_ALIGNMENT)}
		        .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
		        .create ());

		// create index data memblock
		s_idxMemBlock.emplace_back (
		    dk::MemBlockMaker{device_, align (IDXBUF_SIZE, DK_MEMBLOCK_ALIGNMENT)}
		        .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
		        .create ());
	}

	// get texture atlas
	io.Fonts->SetTexID (makeTextureID (fontTextureHandle_));
	s_fontTextureHandle = fontTextureHandle_;
	unsigned char *pixels;
	int width;
	int height;
	io.Fonts->GetTexDataAsAlpha8 (&pixels, &width, &height);

	// create memblock for transfer
	dk::UniqueMemBlock memBlock =
	    dk::MemBlockMaker{device_, align (width * height, DK_MEMBLOCK_ALIGNMENT)}
	        .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
	        .create ();
	std::memcpy (memBlock.getCpuAddr (), pixels, width * height);

	// initialize sampler descriptor
	samplerDescriptor_.initialize (
	    dk::Sampler{}
	        .setFilter (DkFilter_Linear, DkFilter_Linear)
	        .setWrapMode (DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge, DkWrapMode_ClampToEdge));

	// initialize texture atlas image layout
	dk::ImageLayout layout;
	dk::ImageLayoutMaker{device_}
	    .setFlags (0)
	    .setFormat (DkImageFormat_R8_Unorm)
	    .setDimensions (width, height)
	    .initialize (layout);

	auto const fontAlign = layout.getAlignment ();
	auto const fontSize  = layout.getSize ();

	// create image memblock
	s_fontImageMemBlock = dk::MemBlockMaker{device_,
	    align (fontSize, std::max<unsigned> (fontAlign, DK_MEMBLOCK_ALIGNMENT))}
	                          .setFlags (DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image)
	                          .create ();

	// initialize font texture atlas image descriptor
	dk::Image fontTexture;
	fontTexture.initialize (layout, s_fontImageMemBlock, 0);
	imageDescriptor_.initialize (fontTexture);

	// copy font texture atlas to image view
	dk::ImageView imageView{fontTexture};
	cmdBuf_.copyBufferToImage ({memBlock.getGpuAddr (), 0, 0},
	    imageView,
	    {0, 0, 0, (unsigned int)width, (unsigned int)height, 1});

	// submit commands to transfer font texture
	queue_.submitCommands (cmdBuf_.finishList ());

	// wait for commands to complete before releasing memblock
	queue_.waitIdle ();
}

void imgui::deko3d::exit ()
{
	s_fontImageMemBlock = nullptr;

	s_idxMemBlock.clear ();
	s_vtxMemBlock.clear ();

	s_uboMemBlock  = nullptr;
	s_codeMemBlock = nullptr;
}

void imgui::deko3d::render (dk::UniqueDevice &device_,
    dk::UniqueQueue &queue_,
    dk::UniqueCmdBuf &cmdBuf_,
    unsigned const slot_)
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

	// setup desired render state
	auto const setupCmd = setupRenderState (cmdBuf_, drawData, width, height);
	queue_.submitCommands (setupCmd);

	// currently bound texture
	std::optional<DkResHandle> boundTextureHandle;

	// will project scissor/clipping rectangles into framebuffer space
	// (0,0) unless using multi-viewports
	auto const clipOff = drawData->DisplayPos;
	// (1,1) unless using retina display which are often (2,2)
	auto const clipScale = drawData->FramebufferScale;

	// check if we need to grow vertex data memblock
	if (s_vtxMemBlock[slot_].getSize () < drawData->TotalVtxCount * sizeof (ImDrawVert))
	{
		// add 10% to avoid growing many frames in a row
		std::size_t const count = drawData->TotalVtxCount * 1.1f;

		s_vtxMemBlock[slot_] =
		    dk::MemBlockMaker{device_, align (count * sizeof (ImDrawVert), DK_MEMBLOCK_ALIGNMENT)}
		        .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
		        .create ();
	}

	// check if we need to grow index data memblock
	if (s_idxMemBlock[slot_].getSize () < drawData->TotalIdxCount * sizeof (ImDrawIdx))
	{
		// add 10% to avoid growing many frames in a row
		std::size_t const count = drawData->TotalIdxCount * 1.1f;

		s_idxMemBlock[slot_] =
		    dk::MemBlockMaker{device_, align (count * sizeof (ImDrawIdx), DK_MEMBLOCK_ALIGNMENT)}
		        .setFlags (DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached)
		        .create ();
	}

	// get base cpu addresses
	auto const cpuVtx = static_cast<std::uint8_t *> (s_vtxMemBlock[slot_].getCpuAddr ());
	auto const cpuIdx = static_cast<std::uint8_t *> (s_idxMemBlock[slot_].getCpuAddr ());

	// get base gpu addresses
	auto const gpuVtx = s_vtxMemBlock[slot_].getGpuAddr ();
	auto const gpuIdx = s_idxMemBlock[slot_].getGpuAddr ();

	// get memblock sizes
	auto const sizeVtx = s_vtxMemBlock[slot_].getSize ();
	auto const sizeIdx = s_idxMemBlock[slot_].getSize ();

	// bind vertex/index data memblocks
	static_assert (sizeof (ImDrawIdx) == sizeof (std::uint16_t));
	cmdBuf_.bindVtxBuffer (0, gpuVtx, sizeVtx);
	cmdBuf_.bindIdxBuffer (DkIdxFormat_Uint16, gpuIdx);

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
				queue_.submitCommands (cmdBuf_.finishList ());

				// user callback, registered via ImDrawList::AddCallback()
				// (ImDrawCallback_ResetRenderState is a special callback value used by the user to
				// request the renderer to reset render state.)
				if (cmd.UserCallback == ImDrawCallback_ResetRenderState)
					queue_.submitCommands (setupCmd);
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

				// check if clip coordinate are outside of the framebuffer
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
					clip.w = height;

				// apply scissor boundaries
				cmdBuf_.setScissors (0,
				    DkScissor{(unsigned int)clip.x,
				        (unsigned int)clip.y,
				        (unsigned int)(clip.z - clip.x),
				        (unsigned int)(clip.w - clip.y)});

				// get texture handle
				auto const textureHandle = reinterpret_cast<std::uintptr_t> (cmd.TextureId);

				// check if we need to bind a new texture
				if (!boundTextureHandle || textureHandle != *boundTextureHandle)
				{
					// check if this is the first draw or changing to or from the font texture
					if (!boundTextureHandle || textureHandle == s_fontTextureHandle ||
					    *boundTextureHandle == s_fontTextureHandle)
					{
						FragUBO fragUBO;
						fragUBO.font = (textureHandle == s_fontTextureHandle);

						// update fragment shader UBO
						cmdBuf_.pushConstants (
						    s_uboMemBlock.getGpuAddr () +
						        align (sizeof (VertUBO), DK_UNIFORM_BUF_ALIGNMENT),
						    align (sizeof (FragUBO), DK_UNIFORM_BUF_ALIGNMENT),
						    0,
						    sizeof (FragUBO),
						    &fragUBO);
					}

					boundTextureHandle = textureHandle;

					// bind the new texture
					cmdBuf_.bindTextures (DkStage_Fragment, 0, textureHandle);
				}

				// draw the draw list
				cmdBuf_.drawIndexed (DkPrimitive_Triangles,
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

	// submit final commands
	queue_.submitCommands (cmdBuf_.finishList ());
}
#endif
