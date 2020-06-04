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

#include "gettext.h"

#include "fs.h"

#include <cerrno>
#include <cinttypes>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
using StringMap = std::map<std::string, std::string>;
using Table     = std::vector<std::pair<std::uint32_t, std::uint32_t>>;

StringMap s_translations;

/// \brief Read u32 value
/// \param file_ File to read
/// \param[out] data_ Output data
/// \param le_ Whether little-endian
bool read (fs::File &file_, std::uint32_t &data_, bool const le_)
{
	if (!file_.readAll (&data_, sizeof (data_)))
		return false;

	if (!le_)
		data_ = __builtin_bswap32 (data_);

	return true;
}

/// \brief Load string table [offset, length]
/// \param file_ File to read
/// \param offset_ Offset to read from
/// \param count_ Number of entries
/// \param le_ Whether little-endian
Table loadTable (fs::File &file_, off_t const offset_, std::uint32_t const count_, bool const le_)
{
	if (file_.seek (offset_) != 0)
		return {};

	Table table;
	for (std::uint32_t i = 0; i < count_; ++i)
	{
		std::uint32_t length;
		if (!read (file_, length, le_))
			return {};

		std::uint32_t offset;
		if (!read (file_, offset, le_))
			return {};

		table.emplace_back (std::make_pair (offset, length));
	}

	return table;
}

/// \brief Load string
/// \param file_ File to read
/// \param offset_ String offset
/// \param length_ String length
std::string loadString (fs::File &file_, std::uint32_t const offset_, std::uint32_t const length_)
{
	if (length_ == 0)
		return {};

	if (file_.seek (offset_) != 0)
		return {};

	std::string str (length_, 0);

	if (!file_.readAll (&str[0], length_))
		return {};

	return str;
}

/// \brief Load translations
/// \param language_ Language to load
void loadTranslations (char const *const language_)
{
	char path[32];
	std::sprintf (path, "romfs:/locale/%s/ftpd.mo", language_);

	fs::File file;
	if (!file.open (path))
		return;

	std::uint32_t magic;
	if (!read (file, magic, true))
		return;

	bool le;
	if (magic == 0x950412DE)
		le = true;
	else if (magic == 0xDE120495)
		le = false;
	else
		return;

	std::uint32_t version;
	if (!read (file, version, le) || version != 0)
		return;

	std::uint32_t numStrings;
	if (!read (file, numStrings, le))
		return;

	std::uint32_t originalOffset;
	if (!read (file, originalOffset, le))
		return;

	std::uint32_t translationOffset;
	if (!read (file, translationOffset, le))
		return;

	std::uint32_t hashSize;
	if (!read (file, hashSize, le))
		return;

	std::uint32_t hashOffset;
	if (!read (file, hashOffset, le))
		return;

	auto originalTable = loadTable (file, originalOffset, numStrings, le);
	if (originalTable.size () != numStrings)
		return;

	auto translationTable = loadTable (file, translationOffset, numStrings, le);
	if (translationTable.size () != numStrings)
		return;

	StringMap strings;
	for (std::uint32_t i = 0; i < numStrings; ++i)
	{
		auto originalString = loadString (file, originalTable[i].first, originalTable[i].second);
		auto translationString =
		    loadString (file, translationTable[i].first, translationTable[i].second);

		strings.emplace (std::make_pair (originalString, translationString));
	}

	std::swap (strings, s_translations);
}
}

void setLanguage (char const *const language_)
{
	loadTranslations (language_);
}

char const *getText (char const *const text_)
{
	auto it = s_translations.find (text_);
	if (it != std::end (s_translations) && !it->second.empty ())
		return it->second.c_str ();

	return text_;
}
