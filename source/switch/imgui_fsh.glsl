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

#version 460

layout (location = 0) in vec2 vtxUv;
layout (location = 1) in vec4 vtxColor;

layout (binding = 0) uniform sampler2D tex;

layout (std140, binding = 0) uniform FragUBO {
	uint font;
} ubo;

layout (location = 0) out vec4 outColor;

void main()
{
	// font texture is single-channel (alpha)
	if (ubo.font != 0)
	    outColor = vtxColor * vec4 (vec3 (1.0), texture (tex, vtxUv).r);
	else
		outColor = vtxColor * texture (tex, vtxUv);
}
