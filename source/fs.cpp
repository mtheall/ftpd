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

#include "fs.h"

#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <string>

#if defined(NDS) || defined(_3DS) || defined(__SWITCH__)
#define getline __getline
#endif

std::string fs::printSize (std::uint64_t const size_)
{
	constexpr std::uint64_t const KiB = 1024;
	constexpr std::uint64_t const MiB = 1024 * KiB;
	constexpr std::uint64_t const GiB = 1024 * MiB;
	constexpr std::uint64_t const TiB = 1024 * GiB;
	constexpr std::uint64_t const PiB = 1024 * TiB;
	constexpr std::uint64_t const EiB = 1024 * PiB;

	char buffer[64] = {};

	for (auto const &[name, bin] : {
	         // clang-format off
	         std::make_pair ("EiB", EiB),
	         std::make_pair ("PiB", PiB),
	         std::make_pair ("TiB", TiB),
	         std::make_pair ("GiB", GiB),
	         std::make_pair ("MiB", MiB),
	         std::make_pair ("KiB", KiB)
	         // clang-format on
	     })
	{
		// get the integral portion of the number
		auto const whole = size_ / bin;
		if (size_ >= 100 * bin)
		{
			// >= 100, print xxxXiB
			std::sprintf (buffer, "%" PRIu64 "%s", whole, name);
			return buffer;
		}

		// get the fractional portion of the number
		auto const frac = size_ - whole * bin;
		if (size_ >= 10 * bin)
		{
			// >= 10, print xx.xXiB
			std::sprintf (buffer, "%" PRIu64 ".%" PRIu64 "%s", whole, frac * 10 / bin, name);
			return buffer;
		}

		if (size_ >= 1000 * (bin / KiB))
		{
			// >= 1000 of lesser bin, print x.xxXiB
			std::sprintf (buffer, "%" PRIu64 ".%02" PRIu64 "%s", whole, frac * 100 / bin, name);
			return buffer;
		}
	}

	// < 1KiB, just print the number
	std::sprintf (buffer, "%" PRIu64 "B", size_);
	return buffer;
}

///////////////////////////////////////////////////////////////////////////
fs::File::~File ()
{
	std::free (m_lineBuffer);
}

fs::File::File () = default;

fs::File::File (File &&that_) = default;

fs::File &fs::File::operator= (File &&that_) = default;

fs::File::operator bool () const
{
	return static_cast<bool> (m_fp);
}

fs::File::operator FILE * () const
{
	return m_fp.get ();
}

void fs::File::setBufferSize (std::size_t const size_)
{
	if (m_bufferSize != size_)
	{
		m_buffer     = std::make_unique<char[]> (size_);
		m_bufferSize = size_;
	}

	if (m_fp)
		std::setvbuf (m_fp.get (), m_buffer.get (), _IOFBF, m_bufferSize);
}

bool fs::File::open (char const *const path_, char const *const mode_)
{
	auto const fp = std::fopen (path_, mode_);
	if (!fp)
		return false;

	m_fp = std::unique_ptr<std::FILE, int (*) (std::FILE *)> (fp, &std::fclose);

	if (m_buffer)
		std::setvbuf (m_fp.get (), m_buffer.get (), _IOFBF, m_bufferSize);

	return true;
}

void fs::File::close ()
{
	m_fp.reset ();
}

std::make_signed_t<std::size_t> fs::File::seek (std::size_t const pos_, int const origin_)
{
	return std::fseek (m_fp.get (), pos_, origin_);
}

std::make_signed_t<std::size_t> fs::File::read (void *const buffer_, std::size_t const size_)
{
	assert (buffer_);
	assert (size_ > 0);

	return std::fread (buffer_, 1, size_, m_fp.get ());
}

std::make_signed_t<std::size_t> fs::File::read (IOBuffer &buffer_)
{
	assert (buffer_.freeSize () > 0);

	auto const rc = read (buffer_.freeArea (), buffer_.freeSize ());
	if (rc > 0)
		buffer_.markUsed (rc);

	return rc;
}

std::string_view fs::File::readLine ()
{
	while (true)
	{
		auto rc = ::getline (&m_lineBuffer, &m_lineBufferSize, m_fp.get ());
		if (rc < 0)
			return {};

		while (rc > 0)
		{
			if (m_lineBuffer[rc - 1] != '\r' && m_lineBuffer[rc - 1] != '\n')
				break;

			m_lineBuffer[--rc] = 0;
		}

		if (rc > 0)
			return std::string_view (m_lineBuffer, rc);
	}
}

bool fs::File::readAll (void *const buffer_, std::size_t const size_)
{
	assert (buffer_);
	assert (size_ > 0);

	auto p = static_cast<char *> (buffer_);

	std::size_t bytes = 0;
	while (bytes < size_)
	{
		auto const rc = read (p, size_ - bytes);
		if (rc <= 0)
			return false;

		p += rc;
		bytes += rc;
	}

	return true;
}

std::make_signed_t<std::size_t> fs::File::write (void const *const buffer_, std::size_t const size_)
{
	assert (buffer_);
	assert (size_ > 0);

	return std::fwrite (buffer_, 1, size_, m_fp.get ());
}

std::make_signed_t<std::size_t> fs::File::write (IOBuffer &buffer_)
{
	assert (buffer_.usedSize () > 0);

	auto const rc = write (buffer_.usedArea (), buffer_.usedSize ());
	if (rc > 0)
		buffer_.markFree (rc);

	return rc;
}

bool fs::File::writeAll (void const *const buffer_, std::size_t const size_)
{
	assert (buffer_);
	assert (size_ > 0);

	auto p = static_cast<char const *> (buffer_);

	std::size_t bytes = 0;
	while (bytes < size_)
	{
		auto const rc = write (p, size_ - bytes);
		if (rc <= 0)
			return false;

		p += rc;
		bytes += rc;
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////
fs::Dir::~Dir () = default;

fs::Dir::Dir () = default;

fs::Dir::Dir (Dir &&that_) = default;

fs::Dir &fs::Dir::operator= (Dir &&that_) = default;

fs::Dir::operator bool () const
{
	return static_cast<bool> (m_dp);
}

fs::Dir::operator DIR * () const
{
	return m_dp.get ();
}

bool fs::Dir::open (char const *const path_)
{
	auto const dp = ::opendir (path_);
	if (!dp)
		return false;

	m_dp = std::unique_ptr<DIR, int (*) (DIR *)> (dp, &::closedir);
	return true;
}

void fs::Dir::close ()
{
	m_dp.reset ();
}

struct dirent *fs::Dir::read ()
{
	errno = 0;
	return ::readdir (m_dp.get ());
}
