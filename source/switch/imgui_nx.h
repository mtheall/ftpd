// ftpd is a server implementation based on the following:
// - RFC  959 (https://tools.ietf.org/html/rfc959)
// - RFC 3659 (https://tools.ietf.org/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
// - Deflate transmission mode for FTP
//   (https://tools.ietf.org/html/draft-preston-ftpext-deflate-04)
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
#include <switch.h>

namespace imgui
{
namespace nx
{
/// \brief Initialize switch platform
bool init ();
/// \brief Deinitialize switch platform
void exit ();

/// \brief Prepare switch for a new frame
/// \param padState_ Gamepad state
/// \param touchState_ Touch screen state
/// \param mouseState_ Mouse state
/// \param kbState_ Keyboard state
void newFrame (PadState const *padState_,
    HidTouchScreenState const *touchState_ = nullptr,
    HidMouseState const *mouseState_       = nullptr,
    HidKeyboardState const *kbState_       = nullptr);
}
}
#endif
