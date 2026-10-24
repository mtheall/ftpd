// ftpd is a server implementation based on the following:
// - RFC  959 (https://tools.ietf.org/html/rfc959)
// - RFC 3659 (https://tools.ietf.org/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
// - Deflate transmission mode for FTP
//   (https://tools.ietf.org/html/draft-preston-ftpext-deflate-04)
//
// Copyright (C) 2026 Michael Theall
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

#include "imgui_font.h"

#include "log.h"

#include <3ds.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ranges>

namespace
{
#if 0
template <typename F, typename... Args>
std::invoke_result_t<F, Args...> timedFunc (char const *const name_, F func_, Args &&...args_)
{
	auto const start  = std::chrono::system_clock::now ();
	auto const result = func_ (std::forward<Args> (args_)...);
	auto const end    = std::chrono::system_clock::now ();

	info ("%s took %lld ms",
	    name_,
	    std::chrono::duration_cast<std::chrono::milliseconds> (end - start).count ());

	return result;
}
#endif

[[noreturn]] void fontError (char const *message_)
{
	std::fprintf (stderr, "3DS font: %s\n", message_);
	std::abort ();
}

CFNT_s *systemFont (ImFontConfig *src_)
{
	return static_cast<CFNT_s *> (src_->FontData);
}

bool initSource (ImFontAtlas *, ImFontConfig *src_)
{
	auto const font   = systemFont (src_);
	auto const info   = fontGetInfo (font);
	auto const sheets = fontGetGlyphInfo (font);

	if (!info->lineFeed || !sheets->nRows || !sheets->nLines || !sheets->nSheets ||
	    !sheets->cellHeight || !sheets->sheetWidth || !sheets->sheetHeight ||
	    sheets->sheetWidth % 8 || sheets->sheetHeight % 8 ||
	    (sheets->sheetFmt != GPU_A4 && sheets->sheetFmt != GPU_A8))
		fontError ("unsupported shared font sheet layout");

	auto const pixelCount = static_cast<unsigned> (sheets->sheetWidth) * sheets->sheetHeight;
	if (!sheets->sheetData ||
	    sheets->sheetSize < (sheets->sheetFmt == GPU_A4 ? pixelCount / 2 : pixelCount))
		fontError ("invalid shared font sheet storage");

	return true;
}

int glyphIndex (ImFontAtlas *, ImFontConfig *src_, ImWchar code_)
{
	// libctru's lookup substitutes missing characters. Check the cmap instead so
	// ImGui can perform its own fallback and ellipsis selection. Add one: glyph 0 is valid.
	for (auto cmap = fontGetInfo (systemFont (src_))->cmap; cmap; cmap = cmap->next)
	{
		if (code_ < cmap->codeBegin || code_ > cmap->codeEnd)
			continue;

		int index = -1;
		switch (cmap->mappingMethod)
		{
		case CMAP_TYPE_DIRECT:
			index = cmap->indexOffset + code_ - cmap->codeBegin;
			break;

		case CMAP_TYPE_TABLE:
			index = cmap->indexTable[code_ - cmap->codeBegin];
			break;

		case CMAP_TYPE_SCAN:
		{
			// assume scan entries are sorted by code
			auto const end   = cmap->scanEntries + cmap->nScanEntries;
			auto const entry = std::ranges::lower_bound (
			    cmap->scanEntries, end, code_, {}, [] (auto const &entry_) { return entry_.code; });
			if (entry != end && entry->code == code_)
				index = entry->glyphIndex;
			break;
		}

		default:
			fontError ("unknown shared font character map");
		}

		if (index >= 0 && index < 0xFFFF)
			return index + 1;
	}

	return 0;
}

bool containsGlyph (ImFontAtlas *, ImFontConfig *src_, int index_)
{
	auto const sheets = fontGetGlyphInfo (systemFont (src_));

	return index_ > 0 &&
	       static_cast<unsigned> (index_ - 1) < sheets->nRows * sheets->nLines * sheets->nSheets;
}

bool initBaked (ImFontAtlas *, ImFontConfig *src_, ImFontBaked *baked_, void *)
{
	auto const info  = fontGetInfo (systemFont (src_));
	auto const scale = baked_->Size / info->lineFeed;

	baked_->Ascent  = info->ascent * scale;
	baked_->Descent = (static_cast<float> (info->ascent) - info->lineFeed) * scale;

	return true;
}

bool loadGlyph (ImFontAtlas *atlas_,
    ImFontConfig *src_,
    ImFontBaked *baked_,
    void *,
    int index_,
    ImFontGlyph *glyph_,
    float *advance_)
{
	if (!containsGlyph (atlas_, src_, index_))
		fontError ("invalid shared font glyph index");

	auto const font   = systemFont (src_);
	auto const sheets = fontGetGlyphInfo (font);
	auto const info   = fontGetInfo (font);
	auto const widths = fontGetCharWidthInfo (font, index_ - 1);
	auto const scale  = baked_->Size / info->lineFeed;
	if (advance_)
	{
		*advance_ = widths->charWidth * scale;
		return true;
	}

	glyph_->AdvanceX = widths->charWidth * scale;
	if (!widths->glyphWidth)
		return true;

	auto const perSheet   = sheets->nRows * sheets->nLines;
	auto const sheetIndex = (index_ - 1) / perSheet;
	auto const cell       = (index_ - 1) % perSheet;
	auto const left       = (cell % sheets->nRows) * (sheets->cellWidth + 1) + 1;
	auto const top        = (cell / sheets->nRows) * (sheets->cellHeight + 1) + 1;
	if (left + widths->glyphWidth > sheets->sheetWidth ||
	    top + sheets->cellHeight > sheets->sheetHeight)
		fontError ("glyph exceeds shared font sheet");

	glyph_->X0      = widths->left * scale;
	glyph_->Y0      = (static_cast<float> (info->ascent) - sheets->baselinePos) * scale;
	glyph_->X1      = glyph_->X0 + widths->glyphWidth * scale;
	glyph_->Y1      = glyph_->Y0 + sheets->cellHeight * scale;
	glyph_->Visible = true;
	glyph_->PackId  = ImFontAtlasRectId_Invalid;
	glyph_->U0      = static_cast<float> (left) / sheets->sheetWidth;
	glyph_->U1      = static_cast<float> (left + widths->glyphWidth) / sheets->sheetWidth;

	// [0,1] remains available for ImGui's atlas. Tags start at 2 to avoid its V=1 edge.
	auto const tag = sheetIndex + 2;

	glyph_->V0 = tag + 1.0f - static_cast<float> (top) / sheets->sheetHeight;
	glyph_->V1 = tag + 1.0f - static_cast<float> (top + sheets->cellHeight) / sheets->sheetHeight;

	return true;
}

ImFontLoader const s_loader = [] {
	ImFontLoader loader;
	loader.Name                              = "Nintendo shared font";
	loader.FontSrcInit                       = &initSource;
	loader.FontSrcGetGlyphIndexFromCodepoint = &glyphIndex;
	loader.FontSrcContainsGlyph              = &containsGlyph;
	loader.FontBakedInit                     = &initBaked;
	loader.FontBakedLoadGlyph                = &loadGlyph;

	return loader;
}();
}

void imgui::ctru::initSystemFont ()
{
	if (R_FAILED (fontEnsureMapped ()))
		fontError ("cannot map shared system font");

	auto const font = fontGetSystemFont ();
	if (!font)
		fontError ("shared system font is unavailable");

	auto const sheets = fontGetGlyphInfo (font);
	if (!sheets->cellHeight)
		fontError ("invalid shared system font cell height");

	auto const fontInfo = fontGetInfo (font);

	auto &io    = ImGui::GetIO ();
	auto &atlas = *io.Fonts;

	atlas.TexDesiredFormat = ImTextureFormat_Alpha8;
	atlas.TexMinWidth      = 512;
	atlas.TexMinHeight     = 128;
	atlas.TexMaxWidth      = 1024;
	atlas.TexMaxHeight     = 1024;

	ImFontConfig config;
	config.FontData             = font;
	config.FontDataSize         = sizeof (CFNT_s);
	config.FontDataOwnedByAtlas = false;
	config.FontLoader           = &s_loader;
	config.SizePixels           = fontInfo->lineFeed * 15.0f / sheets->cellHeight;

	std::snprintf (config.Name, sizeof (config.Name), "Nintendo shared font");
	auto const imFont = atlas.AddFont (&config);
	if (!imFont)
		fontError ("cannot initialize shared font loader");

	auto const fallbackIndex = static_cast<unsigned> (fontInfo->alterCharIndex) + 1;
	unsigned fallbackCode    = 0x10000;
	for (auto cmap = fontInfo->cmap; cmap && fallbackCode == 0x10000; cmap = cmap->next)
	{
		auto const consider = [&] (unsigned code_, unsigned index_) {
			if (code_ && index_ + 1 == fallbackIndex && code_ < fallbackCode &&
			    glyphIndex (&atlas, &config, static_cast<ImWchar> (code_)) ==
			        static_cast<int> (fallbackIndex))
				fallbackCode = code_;
		};

		switch (cmap->mappingMethod)
		{
		case CMAP_TYPE_DIRECT:
			if (fallbackIndex - 1 >= cmap->indexOffset)
			{
				auto const code = cmap->codeBegin + fallbackIndex - 1 - cmap->indexOffset;
				if (code <= cmap->codeEnd)
					consider (code, fallbackIndex - 1);
			}
			break;

		case CMAP_TYPE_TABLE:
			for (unsigned code = cmap->codeBegin; code <= cmap->codeEnd && fallbackCode == 0x10000;
			     ++code)
				consider (code, cmap->indexTable[code - cmap->codeBegin]);
			break;

		case CMAP_TYPE_SCAN:
		{
			// assume scan entries are sorted by glyphIndex
			auto const end   = cmap->scanEntries + cmap->nScanEntries;
			auto const entry = std::ranges::lower_bound (
			    cmap->scanEntries, end, fallbackIndex - 1, {}, [] (auto const &entry_) {
				    return entry_.code;
			    });
			if (entry != end && entry->glyphIndex == fallbackIndex - 1)
				consider (entry->code, entry->glyphIndex);
			break;
		}
		}
	}

	if (fallbackCode < 0x10000)
		imFont->FallbackChar = static_cast<ImWchar> (fallbackCode);

	// ImGui's synthesized ellipsis copies packed dot pixels, which borrowed glyphs don't have.
	if (imFont->EllipsisAutoBake)
	{
		imFont->EllipsisAutoBake = false;
		imFont->EllipsisChar     = glyphIndex (&atlas, &config, '.') ? '.' : imFont->FallbackChar;
	}
}
