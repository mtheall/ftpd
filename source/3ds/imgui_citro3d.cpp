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
#include "imgui_citro3d.h"
#include "imgui_font.h"

#include "vshader_shbin.h"

#include <citro3d.h>

#include <imgui.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <tuple>
#include <vector>

namespace
{
/// \brief Clear color
constexpr auto CLEAR_COLOR = 0x204B7AFF;
/// \brief Maximum texture dimension
constexpr int MAX_TEXTURE_SIZE = 1024;

static_assert (sizeof (ImDrawIdx) == sizeof (std::uint16_t));

/// \brief Slider value
float s_slider;
/// \brief Z offset (for stereoscopic effect)
float s_z;
/// \brief Z offset uniform location
int s_zLocation;

/// \brief Vertex shader
DVLB_s *s_vsh = nullptr;
/// \brief Vertex shader program
shaderProgram_s s_program;

/// \brief Top screen projection matrix
C3D_Mtx s_projTop;
/// \brief Bottom screen projection matrix
C3D_Mtx s_projBottom;
/// \brief Projection matrix uniform location
int s_projLocation;

/// \brief UV scale uniform location
int s_uvScaleLocation;

/// \brief Scissor test bounds
std::uint32_t s_boundScissor[4];
/// \brief Currently bound vertex data
ImDrawVert *s_boundVtxData;
/// \brief Currently bound texture
ImTextureID s_boundTexture = ImTextureID_Invalid;
/// \brief Texture filter mode
GPU_TEXTURE_FILTER_PARAM s_filter = GPU_LINEAR;

/// \brief Vertex data buffer
ImDrawVert *s_vtxData = nullptr;
/// \brief Size of vertex data buffer
std::size_t s_vtxSize = 0;
/// \brief Index data buffer
ImDrawIdx *s_idxData = nullptr;
/// \brief Size of index data buffer
std::size_t s_idxSize = 0;

/// \brief Texture metadata
struct Texture
{
	~Texture ()
	{
		C3D_TexDelete (&image);
	}

	C3D_Tex image{};        ///< Texture image
	ImTextureData *data;    ///< Texture data
	ImTextureFormat format; ///< Texture format
	int width;              ///< Texture width
	int height;             ///< Texture height
	bool fontAtlas = false; ///< Whether this texture is a font atlas
};

/// \brief List of all textures managed by the renderer
std::vector<std::unique_ptr<Texture>> s_textures;

/// \brief List of font sheet textures
std::vector<C3D_Tex> s_fontSheets;

/// \brief RAII helper for texture access within a frame
/// FrameBegin waits for the previous GPU queue; FrameSync only waits for vblank.
/// An existing frame is safe here only before the caller records GPU draws.
struct TextureAccess
{
	/// \brief Whether this RAII instance owns the current frame
	bool const ownsFrame = C3D_FrameBegin (0);

	~TextureAccess ()
	{
		if (ownsFrame)
			C3D_FrameEnd (0);
	}
};

/// \brief Reports a renderer error and aborts the program
/// \param message_ Error message to report
[[noreturn]] void rendererError (char const *const message_)
{
	std::fprintf (stderr, "citro3d renderer: %s\n", message_);
	std::abort ();
}

bool isVramAddr (void const *const addr_)
{
	return addr_ >= std::bit_cast<void const *> (static_cast<std::uintptr_t> (OS_VRAM_VADDR)) &&
	       addr_ < std::bit_cast<void const *> (
	                   static_cast<std::uintptr_t> (OS_VRAM_VADDR + OS_VRAM_SIZE));
}

/// \brief Computes the offset of a pixel within a tiled texture
/// \param x_ X coordinate of the pixel
/// \param y_ Y coordinate of the pixel
/// \param width_ Width of the texture
std::size_t tiledPixelOffset (unsigned const x_, unsigned const y_, unsigned const width_)
{
	// PICA200 textures store pixels in row-major 8x8 tiles, with Morton order within each tile
	auto const tile = ((y_ / 8) * (width_ / 8) + x_ / 8) * 64;
	auto const x    = x_ & 7;
	auto const y    = y_ & 7;

	return tile | (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) |
	       ((y & 4) << 3);
}

/// \brief Retrieves a texture by its ID
/// \param id_ Texture ID to look up
Texture &getTexture (ImTextureID const id_)
{
	auto const it = std::ranges::find_if (s_textures, [id_] (auto const &texture_) {
		return static_cast<ImTextureID> (reinterpret_cast<std::uintptr_t> (texture_.get ())) == id_;
	});

	if (it == std::end (s_textures))
		rendererError ("unknown texture handle");

	return **it;
}

/// \brief Retrieves the font sheet tag for a given draw command and element
/// \param list_ Draw list containing the command
/// \param cmd_ Draw command
/// \param element_ Element index within the command
unsigned sheetTagFast (ImDrawList const &list_, ImDrawCmd const &cmd_, unsigned const element_)
{
	// iterate over the three vertices of the triangle to determine the font sheet tag
	auto const index = cmd_.VtxOffset + list_.IdxBuffer[cmd_.IdxOffset + element_];
	auto const v     = list_.VtxBuffer[index].uv.y;
	auto const tag   = v >= 2.0f ? static_cast<unsigned> (std::floor (v)) : 0;

	// validate the current font sheet tag against the previous one and the available sheets
	if (tag >= s_fontSheets.size () + 2)
		rendererError ("invalid system font sheet coordinates");

	return tag;
}

/// \brief Retrieves the font sheet tag for a given draw command and element
/// \param list_ Draw list containing the command
/// \param cmd_ Draw command
/// \param element_ Element index within the command
unsigned sheetTagSafe (ImDrawList const &list_, ImDrawCmd const &cmd_, unsigned const element_)
{
	unsigned tag = 0;

	// iterate over the three vertices of the triangle to determine the font sheet tag
	for (unsigned i = 0; i < 3; ++i)
	{
		auto const current = sheetTagFast (list_, cmd_, element_ + i);

		// validate the current font sheet tag against the previous one and the available sheets
		if (i && current != tag)
			rendererError ("invalid system font sheet coordinates");

		tag = current;
	}

	return tag;
}

/// \brief Binds a texture for rendering
/// \param image_ Texture to bind
/// \param alphaOnly_ Whether to use alpha-only mode
/// \param scaleX_ Horizontal scale factor
/// \param scaleY_ Vertical scale factor
void bindTexture (C3D_Tex &image_, bool const alphaOnly_, float const scaleX_, float const scaleY_)
{
	auto const id = reinterpret_cast<ImTextureID> (&image_);

	if (id == s_boundTexture)
		return;

	// update the texture filter settings
	C3D_TexSetFilter (&image_, s_filter, s_filter);

	// bind the texture to the first texture unit
	C3D_TexBind (0, &image_);

	auto const env = C3D_GetTexEnv (0);
	C3D_TexEnvInit (env);

	// use texture color for RGBA8, and primary color for Alpha8
	C3D_TexEnvSrc (env,
	    C3D_RGB,
	    alphaOnly_ ? GPU_PRIMARY_COLOR : GPU_TEXTURE0,
	    GPU_PRIMARY_COLOR,
	    GPU_PRIMARY_COLOR);
	C3D_TexEnvFunc (env, C3D_RGB, alphaOnly_ ? GPU_REPLACE : GPU_MODULATE);

	// set up alpha blending for the texture
	C3D_TexEnvSrc (env, C3D_Alpha, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
	C3D_TexEnvFunc (env, C3D_Alpha, GPU_MODULATE);

	// update the UV scale uniform for the vertex shader
	// scale GPU UVs in the vertex shader to match padded power-of-two dimensions
	C3D_FVUnifSet (GPU_VERTEX_SHADER, s_uvScaleLocation, scaleX_, scaleY_, 0.0f, 0.0f);

	s_boundTexture = id;
}

/// \brief Destroys a texture and releases its GPU resources
/// \param data_ Texture data to destroy
void destroyTexture (ImTextureData &data_)
{
	auto const it = std::ranges::find_if (
	    s_textures, [&data_] (auto const &texture_) { return texture_->data == &data_; });

	if (data_.BackendUserData || data_.GetTexID () != ImTextureID_Invalid)
	{
		if (it == std::end (s_textures) || it->get () != data_.BackendUserData ||
		    reinterpret_cast<ImTextureID> (it->get ()) != data_.GetTexID ())
			rendererError ("texture ownership mismatch");

		s_textures.erase (it);
	}
	else if (it != std::end (s_textures))
		rendererError ("texture lost its GPU handle");

	data_.BackendUserData = nullptr;
	data_.SetTexID (ImTextureID_Invalid);
	data_.SetStatus (ImTextureStatus_Destroyed);

	s_boundTexture = ImTextureID_Invalid;
}

/// \brief Uploads texture data to the GPU
/// \param data_ Texture data to upload
/// \param texture_ GPU texture to upload to
/// \param create_ Whether this is a new texture or an update
void uploadTexture (ImTextureData &data_, Texture &texture_, bool const create_)
{
	auto const rect = create_ ? ImTextureRect{0,
	                                0,
	                                static_cast<unsigned short> (data_.Width),
	                                static_cast<unsigned short> (data_.Height)}
	                          : data_.UpdateRect;

	if (!rect.w || !rect.h || rect.x + rect.w > data_.Width || rect.y + rect.h > data_.Height)
		rendererError ("invalid texture upload rectangle");

	auto const output = static_cast<unsigned char *> (texture_.image.data);
	if (isVramAddr (output))
		rendererError ("cannot update vram textures");

	// extend edge pixels through padding so linear filtering clamps to the logical image
	unsigned const endX = rect.x + rect.w == data_.Width ? texture_.image.width : rect.x + rect.w;
	unsigned const endY = rect.y + rect.h == data_.Height ? texture_.image.height : rect.y + rect.h;

	for (unsigned y = rect.y; y < endY; ++y)
	{
		for (unsigned x = rect.x; x < endX; ++x)
		{
			// PICA200 texture is bottom-left origin
			auto const offset =
			    tiledPixelOffset (x, texture_.image.height - 1 - y, texture_.image.width);

			auto const pixel = static_cast<unsigned char const *> (
			    data_.GetPixelsAt (std::min (x, static_cast<unsigned> (data_.Width - 1)),
			        std::min (y, static_cast<unsigned> (data_.Height - 1))));

			if (data_.Format == ImTextureFormat_Alpha8)
				output[offset] = pixel[0];
			else
			{
				// pixels in GPU_RGBA8 are byte-ordered ABGR on the little-endian CPU
				for (unsigned channel = 0; channel < 4; ++channel)
					output[offset * 4 + channel] = pixel[3 - channel];
			}
		}
	}

	C3D_TexFlush (&texture_.image);
}

/// \defgroup RenderState Render State Functions
/// \brief Functions for managing the render state in ImGui Citro3D backend
/// @{
void resetRenderState (ImDrawList const *, ImDrawCmd const *)
{
}

void setSamplerLinear (ImDrawList const *, ImDrawCmd const *)
{
}

void setSamplerNearest (ImDrawList const *, ImDrawCmd const *)
{
}
/// @}

/// \brief Setup render state
/// \param screen_ Whether top or bottom screen
void setupRenderState (gfxScreen_t const screen_)
{
	// disable face culling
	C3D_CullFace (GPU_CULL_NONE);

	// configure attributes for user with vertex shader
	auto const attrInfo = C3D_GetAttrInfo ();
	AttrInfo_Init (attrInfo);
	AttrInfo_AddLoader (attrInfo, 0, GPU_FLOAT, 2);         // v0 = inPos
	AttrInfo_AddLoader (attrInfo, 1, GPU_FLOAT, 2);         // v1 = inUv
	AttrInfo_AddLoader (attrInfo, 2, GPU_UNSIGNED_BYTE, 4); // v2 = inColor

	// clear bindings
	std::memset (s_boundScissor, 0xFF, sizeof (s_boundScissor));
	s_boundVtxData = nullptr;
	s_boundTexture = ImTextureID_Invalid;
	s_filter       = GPU_LINEAR;

	// bind program
	C3D_BindProgram (&s_program);

	// enable depth test
	C3D_DepthTest (true, GPU_GREATER, GPU_WRITE_COLOR);

	// enable alpha blending
	C3D_AlphaBlend (GPU_BLEND_ADD,
	    GPU_BLEND_ADD,
	    GPU_SRC_ALPHA,
	    GPU_ONE_MINUS_SRC_ALPHA,
	    GPU_SRC_ALPHA,
	    GPU_ONE_MINUS_SRC_ALPHA);

	// apply projection matrix
	C3D_FVUnifMtx4x4 (
	    GPU_VERTEX_SHADER, s_projLocation, screen_ == GFX_TOP ? &s_projTop : &s_projBottom);

	C3D_FVUnifSet (GPU_VERTEX_SHADER, s_zLocation, s_slider * s_z, 0.0f, 0.0f, 0.0f);
}

/// \brief Update textures used by ImGui
/// \param drawData_ Draw data containing textures to update
void updateTextures (ImDrawData const &drawData_)
{
	for (auto const texture : *drawData_.Textures)
	{
		imgui::citro3d::updateTexture (*texture);

		if (texture->Status == ImTextureStatus_OK)
		{
			// include retained atlas textures during growth, not application textures
			auto const &atlasTextures = ImGui::GetIO ().Fonts->TexList;
			getTexture (texture->GetTexID ()).fontAtlas =
			    std::ranges::find (atlasTextures, texture) != std::end (atlasTextures);
		}
	}
}

/// \brief Setup projection matrices for ImGui
/// \param drawData_ Draw data containing display size information
void setupProjection (ImDrawData const &drawData_)
{
	// initialize projection matrices
	Mtx_OrthoTilt (&s_projTop,
	    0.0f,
	    drawData_.DisplaySize.x,
	    drawData_.DisplaySize.y * 0.5f,
	    0.0f,
	    -1.0f,
	    1.0f,
	    false);

	Mtx_OrthoTilt (&s_projBottom,
	    drawData_.DisplaySize.x * 0.1f,
	    drawData_.DisplaySize.x * 0.9f,
	    drawData_.DisplaySize.y,
	    drawData_.DisplaySize.y * 0.5f,
	    -1.0f,
	    1.0f,
	    false);
}

/// \brief Prepare vertex and index buffers for ImGui
/// \param drawData_ Draw data containing vertex and index information
void prepareBuffers (ImDrawData const &drawData_)
{
	// check if we need to grow vertex data buffer
	if (s_vtxSize < static_cast<std::size_t> (drawData_.TotalVtxCount))
	{
		linearFree (s_vtxData);

		// add 10% to avoid growing many frames in a row
		s_vtxSize = drawData_.TotalVtxCount * 1.1f;
		s_vtxData = static_cast<ImDrawVert *> (linearAlloc (sizeof (ImDrawVert) * s_vtxSize));
		if (!s_vtxData)
			rendererError ("cannot grow vertex buffer");
	}

	// check if we need to grow index data buffer
	if (s_idxSize < static_cast<std::size_t> (drawData_.TotalIdxCount))
	{
		linearFree (s_idxData);

		// add 10% to avoid growing many frames in a row
		s_idxSize = drawData_.TotalIdxCount * 1.1f;
		s_idxData = static_cast<ImDrawIdx *> (linearAlloc (sizeof (ImDrawIdx) * s_idxSize));
		if (!s_idxData)
			rendererError ("cannot grow index buffer");
	}

	std::size_t offsetVtx = 0;
	std::size_t offsetIdx = 0;

	for (auto const list : drawData_.CmdLists)
	{
		// copy vertex/index data into linear buffers
		std::memcpy (s_vtxData + offsetVtx,
		    list->VtxBuffer.Data,
		    list->VtxBuffer.Size * sizeof (ImDrawVert));
		std::memcpy (
		    s_idxData + offsetIdx, list->IdxBuffer.Data, list->IdxBuffer.Size * sizeof (ImDrawIdx));

		std::vector<bool> decoded (list->VtxBuffer.Size);

		// adjust font UVs for retained atlas textures
		for (auto const &cmd : list->CmdBuffer)
		{
			if (cmd.UserCallback || !cmd.ElemCount || !getTexture (cmd.GetTexID ()).fontAtlas)
				continue;

			if (cmd.ElemCount % 3)
				rendererError ("font draw command is not a triangle list");

			for (unsigned element = 0; element < cmd.ElemCount; element += 3)
			{
				auto const tag = sheetTagSafe (*list, cmd, element);

				if (!tag)
					continue;

				for (unsigned i = 0; i < 3; ++i)
				{
					auto const index = cmd.VtxOffset + list->IdxBuffer[cmd.IdxOffset + element + i];

					if (!decoded[index])
					{
						s_vtxData[offsetVtx + index].uv.y = list->VtxBuffer[index].uv.y - tag;

						decoded[index] = true;
					}
				}
			}
		}

		offsetVtx += list->VtxBuffer.Size;
		offsetIdx += list->IdxBuffer.Size;
	}
}

/// \brief Render ImGui draw data to a specified render target
/// \param drawData_ Draw data to render
/// \param target_ Render target to draw on
/// \param screen Screen to render on
/// \param slider_ Slider value for adjusting projection
/// \param width Width of the render target
/// \param height Height of the render target
void renderTarget (ImDrawData const &drawData_,
    C3D_RenderTarget *const target_,
    gfxScreen_t const screen,
    float const slider_,
    unsigned const width,
    unsigned const height)
{
	C3D_RenderTargetClear (target_, C3D_CLEAR_ALL, CLEAR_COLOR, 0);

	s_slider = slider_;
	s_z      = 0.0f;

	C3D_FrameDrawOn (target_);
	setupRenderState (screen);

	// will project scissor/clipping rectangles into framebuffer space
	auto const clipOff   = drawData_.DisplayPos;       // (0,0) unless using multi-viewports
	auto const clipScale = drawData_.FramebufferScale; // (1,1) unless using a high-DPI display

	std::size_t offsetVtx = 0;
	std::size_t offsetIdx = 0;

	for (auto const list : drawData_.CmdLists)
	{
		for (auto const &cmd : list->CmdBuffer)
		{
			if (cmd.UserCallback)
			{
				if (cmd.UserCallback == resetRenderState)
					setupRenderState (screen);
				else if (cmd.UserCallback == setSamplerLinear ||
				         cmd.UserCallback == setSamplerNearest)
				{
					s_filter = cmd.UserCallback == setSamplerLinear ? GPU_LINEAR : GPU_NEAREST;
					s_boundTexture = ImTextureID_Invalid;
				}
				else
					cmd.UserCallback (list, &cmd);

				s_boundVtxData = nullptr;
				s_boundTexture = ImTextureID_Invalid;
				std::memset (s_boundScissor, 0xFF, sizeof (s_boundScissor));

				continue;
			}

			if (!cmd.ElemCount)
				continue;

			ImVec4 clip{(cmd.ClipRect.x - clipOff.x) * clipScale.x,
			    (cmd.ClipRect.y - clipOff.y) * clipScale.y,
			    (cmd.ClipRect.z - clipOff.x) * clipScale.x,
			    (cmd.ClipRect.w - clipOff.y) * clipScale.y};

			// apply parallax based on slider and depth
			if (s_slider && s_z && screen == GFX_TOP)
			{
				clip.x -= s_slider * s_z;
				clip.z += s_slider * s_z;
			}

			// skip if the clip rectangle is completely outside the render target
			if (clip.x >= width || clip.y >= height || clip.z <= 0 || clip.w <= 0)
				continue;

			// adjust clip rectangle to be within the render target
			clip.x = std::max (0.0f, clip.x);
			clip.y = std::max (0.0f, clip.y);
			clip.z = std::min (static_cast<float> (width), clip.z);
			clip.w = std::min (static_cast<float> (height), clip.w);

			// skip if the clip rectangle is outside the specific screen region
			if (screen == GFX_TOP
			        ? clip.y >= height * 0.5f
			        : clip.w <= height * 0.5f || clip.z <= width * 0.1f || clip.x >= width * 0.9f)
				continue;

			auto const bottom = screen == GFX_BOTTOM;
			auto const right  = bottom ? width * 0.9f : static_cast<float> (width);
			auto const maxY   = bottom ? width * 0.8f : static_cast<float> (width);
			auto const base   = bottom ? static_cast<float> (height) : height * 0.5f;

			// calculate scissor rectangle based on clip rectangle and screen region
			std::uint32_t const scissor[] = {
			    static_cast<std::uint32_t> (std::clamp (base - clip.w, 0.0f, height * 0.5f)),
			    static_cast<std::uint32_t> (std::clamp (right - clip.z, 0.0f, maxY)),
			    static_cast<std::uint32_t> (std::clamp (base - clip.y, 0.0f, height * 0.5f)),
			    static_cast<std::uint32_t> (std::clamp (right - clip.x, 0.0f, maxY))};

			// update scissor rectangle if it has changed
			if (!std::ranges::equal (scissor, s_boundScissor))
			{
				std::ranges::copy (scissor, s_boundScissor);
				C3D_SetScissor (GPU_SCISSOR_NORMAL, scissor[0], scissor[1], scissor[2], scissor[3]);
			}

			auto const vtxData = s_vtxData + cmd.VtxOffset + offsetVtx;

			// bind vertex buffer if it has changed
			if (vtxData != s_boundVtxData)
			{
				s_boundVtxData     = vtxData;
				auto const bufInfo = C3D_GetBufInfo ();
				BufInfo_Init (bufInfo);
				BufInfo_Add (bufInfo, vtxData, sizeof (ImDrawVert), 3, 0x210);
			}

			auto &texture = getTexture (cmd.GetTexID ());

			unsigned index = 0;
			while (index < cmd.ElemCount)
			{
				auto end       = cmd.ElemCount;
				auto const tag = texture.fontAtlas ? sheetTagFast (*list, cmd, index) : 0;

				if (texture.fontAtlas)
				{
					// only draw elements that belong to the same font sheet
					end = index + 3;
					while (end < cmd.ElemCount && sheetTagFast (*list, cmd, end) == tag)
						end += 3;
				}

				if (tag)
					bindTexture (s_fontSheets[tag - 2], true, 1.0f, 1.0f);
				else
					bindTexture (texture.image,
					    texture.format == ImTextureFormat_Alpha8,
					    static_cast<float> (texture.width) / texture.image.width,
					    static_cast<float> (texture.height) / texture.image.height);

				C3D_DrawElements (GPU_TRIANGLES,
				    end - index,
				    C3D_UNSIGNED_SHORT,
				    s_idxData + cmd.IdxOffset + offsetIdx + index);

				index = end;
			}
		}

		offsetVtx += list->VtxBuffer.Size;
		offsetIdx += list->IdxBuffer.Size;
	}
}
}

void imgui::citro3d::updateTexture (ImTextureData &data_)
{
	switch (data_.Status)
	{
	case ImTextureStatus_OK:
	case ImTextureStatus_Destroyed:
		return;

	case ImTextureStatus_WantCreate:
	{
		if (data_.BackendUserData || data_.GetTexID () != ImTextureID_Invalid)
			rendererError ("create requested for an existing texture");

		if (!data_.Pixels || data_.Width <= 0 || data_.Height <= 0 ||
		    data_.Width > MAX_TEXTURE_SIZE || data_.Height > MAX_TEXTURE_SIZE ||
		    (data_.Format != ImTextureFormat_Alpha8 && data_.Format != ImTextureFormat_RGBA32) ||
		    data_.BytesPerPixel != (data_.Format == ImTextureFormat_Alpha8 ? 1 : 4))
			rendererError ("invalid texture pixels, dimensions or format");

		TextureAccess const access;

		auto texture    = std::make_unique<Texture> ();
		texture->data   = &data_;
		texture->format = data_.Format;
		texture->width  = data_.Width;
		texture->height = data_.Height;

		auto const width  = std::bit_ceil (std::max (8u, static_cast<unsigned> (data_.Width)));
		auto const height = std::bit_ceil (std::max (8u, static_cast<unsigned> (data_.Height)));

		if (!C3D_TexInit (&texture->image,
		        width,
		        height,
		        data_.Format == ImTextureFormat_Alpha8 ? GPU_A8 : GPU_RGBA8))
			rendererError ("cannot allocate texture");

		C3D_TexSetFilter (&texture->image, GPU_LINEAR, GPU_LINEAR);
		C3D_TexSetWrap (&texture->image, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
		std::memset (texture->image.data, 0, texture->image.size);
		uploadTexture (data_, *texture, true);

		data_.BackendUserData = texture.get ();
		data_.SetTexID (reinterpret_cast<ImTextureID> (texture.get ()));
		data_.SetStatus (ImTextureStatus_OK);

		s_textures.push_back (std::move (texture));

		break;
	}

	case ImTextureStatus_WantUpdates:
	{
		auto &texture = getTexture (data_.GetTexID ());

		if (&texture != data_.BackendUserData || texture.data != &data_ || !data_.Pixels ||
		    texture.format != data_.Format || texture.width != data_.Width ||
		    texture.height != data_.Height ||
		    data_.BytesPerPixel != (texture.format == ImTextureFormat_Alpha8 ? 1 : 4))
			rendererError ("update changed texture ownership, dimensions or format");

		TextureAccess const access;

		uploadTexture (data_, texture, false);
		data_.SetStatus (ImTextureStatus_OK);

		break;
	}

	case ImTextureStatus_WantDestroy:
	{
		TextureAccess const access;

		destroyTexture (data_);

		break;
	}
	}
}

std::unique_ptr<ImTextureData> imgui::citro3d::importTexture (C3D_Tex *const tex_)
{
	if (C3D_TexGetType (tex_) != GPU_TEX_2D)
	{
		std::fprintf (stderr, "only 2D textures are supported");
		return nullptr;
	}

	if (tex_->fmt != GPU_RGBA8 && tex_->fmt != GPU_A8)
	{
		std::fprintf (stderr, "3DS graphics atlas must use RGBA8 or A8\n");
		return nullptr;
	}

	auto result = std::make_unique<ImTextureData> ();

	result->Create (tex_->fmt == GPU_RGBA8 ? ImTextureFormat_RGBA32 : ImTextureFormat_Alpha8,
	    tex_->width,
	    tex_->height);
	result->UseColors = tex_->fmt == GPU_RGBA8;

	auto texture    = std::make_unique<Texture> ();
	texture->data   = result.get ();
	texture->format = result->Format;
	texture->width  = result->Width;
	texture->height = result->Height;
	std::memcpy (&texture->image, tex_, sizeof (C3D_Tex));
	std::memset (tex_, 0, sizeof (C3D_Tex));

	C3D_TexSetFilter (&texture->image, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap (&texture->image, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

	result->BackendUserData = texture.get ();

	result->SetTexID (reinterpret_cast<ImTextureID> (texture.get ()));
	result->SetStatus (ImTextureStatus_OK);

	s_textures.push_back (std::move (texture));

	return result;
}

void imgui::citro3d::init ()
{
	// setup back-end capabilities flags
	auto &io         = ImGui::GetIO ();
	auto &platformIO = ImGui::GetPlatformIO ();

	io.BackendRendererName = "citro3d";
	io.BackendFlags |=
	    ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;

	platformIO.Renderer_TextureMaxWidth       = MAX_TEXTURE_SIZE;
	platformIO.Renderer_TextureMaxHeight      = MAX_TEXTURE_SIZE;
	platformIO.DrawCallback_ResetRenderState  = resetRenderState;
	platformIO.DrawCallback_SetSamplerLinear  = setSamplerLinear;
	platformIO.DrawCallback_SetSamplerNearest = setSamplerNearest;

	// load vertex shader
	s_vsh = DVLB_ParseFile (
	    const_cast<std::uint32_t *> (reinterpret_cast<std::uint32_t const *> (vshader_shbin)),
	    vshader_shbin_size);

	if (!s_vsh)
		rendererError ("cannot load vertex shader");

	// initialize vertex shader program
	shaderProgramInit (&s_program);
	shaderProgramSetVsh (&s_program, &s_vsh->DVLE[0]);

	// get uniform locations
	s_zLocation       = shaderInstanceGetUniformLocation (s_program.vertexShader, "z");
	s_projLocation    = shaderInstanceGetUniformLocation (s_program.vertexShader, "proj");
	s_uvScaleLocation = shaderInstanceGetUniformLocation (s_program.vertexShader, "uvScale");

	s_vtxSize = s_idxSize = 65536;

	// allocate vertex data buffer
	s_vtxData = static_cast<ImDrawVert *> (linearAlloc (sizeof (ImDrawVert) * s_vtxSize));

	// allocate index data buffer
	s_idxData = static_cast<ImDrawIdx *> (linearAlloc (sizeof (ImDrawIdx) * s_idxSize));

	if (!s_vtxData || !s_idxData)
		rendererError ("cannot allocate vertex/index buffers");

	// map the shared system font and initialize its glyph loader
	imgui::ctru::initSystemFont ();

	// use system font sheets as citro3d textures
	auto const font   = fontGetSystemFont ();
	auto const sheets = fontGetGlyphInfo (font);

	s_fontSheets.resize (sheets->nSheets);

	for (unsigned i = 0; i < s_fontSheets.size (); ++i)
	{
		auto &image = s_fontSheets[i];
		image.data  = fontGetGlyphSheetTex (font, i);

		if (!image.data)
			rendererError ("missing shared system font sheet");

		image.fmt    = static_cast<GPU_TEXCOLOR> (sheets->sheetFmt);
		image.size   = sheets->sheetSize;
		image.width  = sheets->sheetWidth;
		image.height = sheets->sheetHeight;

		C3D_TexSetFilter (&image, GPU_LINEAR, GPU_LINEAR);
		C3D_TexSetWrap (&image, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	}
}

void imgui::citro3d::exit ()
{
	// free textures
	{
		TextureAccess const access;
		while (!s_textures.empty ())
			destroyTexture (*s_textures.back ()->data);
		s_fontSheets.clear ();
	}

	// free vertex/index data buffers
	linearFree (s_idxData);
	linearFree (s_vtxData);

	s_idxData = nullptr;
	s_vtxData = nullptr;
	s_idxSize = 0;
	s_vtxSize = 0;

	// free shader program
	shaderProgramFree (&s_program);
	DVLB_Free (s_vsh);
	s_vsh = nullptr;

	// uninitialize ImGui renderer backend
	auto &io         = ImGui::GetIO ();
	auto &platformIO = ImGui::GetPlatformIO ();

	io.BackendRendererName = nullptr;
	io.BackendFlags &=
	    ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);

	platformIO.Renderer_TextureMaxWidth       = 0;
	platformIO.Renderer_TextureMaxHeight      = 0;
	platformIO.DrawCallback_ResetRenderState  = nullptr;
	platformIO.DrawCallback_SetSamplerLinear  = nullptr;
	platformIO.DrawCallback_SetSamplerNearest = nullptr;
}

void imgui::citro3d::render (C3D_RenderTarget *const topLeft_,
    C3D_RenderTarget *const topRight_,
    C3D_RenderTarget *const bottom_)
{
	// get draw data
	auto const drawData = ImGui::GetDrawData ();

	updateTextures (*drawData);

	if (drawData->CmdLists.empty ())
		return;

	unsigned const width  = drawData->DisplaySize.x * drawData->FramebufferScale.x;
	unsigned const height = drawData->DisplaySize.y * drawData->FramebufferScale.y;

	if (!width || !height)
		return;

	setupProjection (*drawData);
	prepareBuffers (*drawData);

	auto const slider = osGet3DSliderState ();

	for (auto const &[target, screen] : {std::make_tuple (topLeft_, GFX_TOP),
	         std::make_tuple (slider ? topRight_ : nullptr, GFX_TOP),
	         std::make_tuple (bottom_, GFX_BOTTOM)})
	{
		if (!target)
			continue;

		auto const targetSlider = target == topLeft_    ? slider
		                          : target == topRight_ ? -slider
		                                                : 0.0f;

		renderTarget (*drawData, target, screen, targetSlider, width, height);
	}
}

void imgui::citro3d::setZ (ImDrawList const *const, ImDrawCmd const *const cmd_)
{
	s_z = std::bit_cast<float> (
	    static_cast<std::uint32_t> (reinterpret_cast<std::uintptr_t> (cmd_->UserCallbackData)));
	C3D_FVUnifSet (GPU_VERTEX_SHADER, s_zLocation, s_slider * s_z, 0.0f, 0.0f, 0.0f);
}
#endif
