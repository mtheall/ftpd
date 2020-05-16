// ftpd is a server implementation based on the following:
// - RFC  959 (https://tools.ietf.org/html/rfc959)
// - RFC 3659 (https://tools.ietf.org/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
//
// The MIT License (MIT)
//
// Copyright (C) 2020 Michael Theall
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

#pragma once

#ifndef CLASSIC
#include <deko3d.hpp>

#include <cstdint>

namespace imgui
{
namespace deko3d
{
/// \brief Initialize deko3d
/// \param device_ deko3d device (used to allocate vertex/index and font texture buffers)
/// \param queue_ deko3d queue (used to run command lists)
/// \param cmdBuf_ Command buffer (used to build command lists)
/// \param[out] samplerDescriptor_ Sampler descriptor for font texture
/// \param[out] imageDescriptor_ Image descriptor for font texture
/// \param fontTextureHandle_ Texture handle that references samplerDescriptor_ and imageDescriptor_
/// \param imageCount_ Images in the swapchain
void init (dk::UniqueDevice &device_,
    dk::UniqueQueue &queue_,
    dk::UniqueCmdBuf &cmdBuf_,
    dk::SamplerDescriptor &samplerDescriptor_,
    dk::ImageDescriptor &imageDescriptor_,
    DkResHandle fontTextureHandle_,
    unsigned imageCount_);

/// \brief Deinitialize deko3d
void exit ();

/// \brief Render ImGui draw list
/// \param device_ deko3d device (used to reallocate vertex/index buffers)
/// \param queue_ deko3d queue (used to run command lists)
/// \param cmdBuf_ Command buffer (used to build command lists)
/// \param slot_ Image slot
void render (dk::UniqueDevice &device_,
    dk::UniqueQueue &queue_,
    dk::UniqueCmdBuf &cmdBuf_,
    unsigned slot_);

/// \brief Make ImGui texture id from deko3d texture handle
/// \param handle_ Texture handle
inline void *makeTextureID (DkResHandle handle_)
{
	return reinterpret_cast<void *> (static_cast<std::uintptr_t> (handle_));
}

/// \brief Align power-of-two value
/// \tparam T Value type
/// \tparam U Alignment type
/// \param size_ Value to align
/// \param align_ Alignment
template <typename T, typename U>
constexpr inline std::uint32_t align (T const &size_, U const &align_)
{
	return static_cast<std::uint32_t> (size_ + align_ - 1) & ~(align_ - 1);
}
}
}
#endif
