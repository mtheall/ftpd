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

#include <dirent.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace fs
{
std::string printSize (std::uint64_t size_);

class File
{
public:
	~File ();

	File ();

	File (File const &that_) = delete;

	File (File &&that_);

	File &operator= (File const &that_) = delete;

	File &operator= (File &&that_);

	operator bool () const;
	operator FILE * () const;

	void setBufferSize (std::size_t size_);

	bool open (char const *path_, char const *mode_ = "rb");
	void close ();

	ssize_t seek (std::size_t pos_, int origin_);

	ssize_t read (void *data_, std::size_t size_);
	bool readAll (void *data_, std::size_t size_);

	ssize_t write (void const *data_, std::size_t size_);
	bool writeAll (void const *data_, std::size_t size_);

	template <typename T>
	T read ()
	{
		T data;
		if (read (&data, sizeof (data)) != sizeof (data))
			std::abort ();

		return data;
	}

	template <typename T>
	void write (T const &data_)
	{
		if (write (&data_, sizeof (data_)) != sizeof (data_))
			std::abort ();
	}

private:
	std::unique_ptr<std::FILE, int (*) (std::FILE *)> m_fp{nullptr, nullptr};
	std::unique_ptr<char[]> m_buffer;
	std::size_t m_bufferSize = 0;
};

class Dir
{
public:
	~Dir ();

	Dir ();

	Dir (Dir const &that_) = delete;

	Dir (Dir &&that_);

	Dir &operator= (Dir const &that_) = delete;

	Dir &operator= (Dir &&that_);

	operator bool () const;
	operator DIR * () const;

	bool open (char const *const path_);
	void close ();
	struct dirent *read ();

private:
	std::unique_ptr<DIR, int (*) (DIR *)> m_dp{nullptr, nullptr};
};
}
