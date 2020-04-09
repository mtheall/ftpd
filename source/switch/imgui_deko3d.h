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

#pragma once

#include <deko3d.hpp>

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
