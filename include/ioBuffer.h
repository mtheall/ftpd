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

#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <type_traits>

/// \brief I/O buffer
/// [unusable][usedArea][freeArea]
class IOBuffer
{
public:
	~IOBuffer ();

	/// \brief Parameterized constructor
	/// \param size_ Buffer size
	IOBuffer (std::size_t size_);

	/// \brief Get pointer to writable area
	char *freeArea () const;
	/// \brief Get size of writable area
	std::size_t freeSize () const;
	/// \brief Get span of writable area
	template <typename T>
	    requires (sizeof (T) == 1)
	std::span<T> freeSpan () const
	{
		return {reinterpret_cast<T *> (m_buffer.get () + m_end), m_size - m_end};
	}

	/// \brief Get pointer to readable area
	char *usedArea () const;
	/// \brief Get size of readable area
	std::size_t usedSize () const;
	/// \brief Get span of readable area
	template <typename T>
	    requires (sizeof (T) == 1)
	std::span<T const> usedSpan () const
	{
		return {reinterpret_cast<T const *> (m_buffer.get () + m_start), m_end - m_start};
	}

	/// \brief Consume data from the beginning of usedArea
	/// \param size_ Size to consume
	/// [unusable][+++++usedArea][freeArea]
	///   becomes
	/// [unusable+++++][usedArea][freeArea]
	void markFree (std::size_t size_);
	/// \brief Produce data to the end of usedArea from freeArea
	/// [unusable][usedArea][++++++freeArea]
	///   becomes
	/// [unusable][usedArea++++++][freeArea]
	void markUsed (std::size_t size_);

	/// \brief Whether usedArea is empty
	bool empty () const;

	/// \brief Get buffer capacity
	std::size_t capacity () const;

	/// \brief Clear buffer; usedArea becomes empty
	/// [unusable][usedArea][++++++freeArea]
	///  becomes
	/// [freeArea++++++++++++++++++++++++++]
	void clear ();

	/// \brief Move usedArea to the beginning of the buffer
	/// [unusable][usedArea][freeArea]
	///  becomes
	/// [usedArea][freeArea++++++++++]
	void coalesce ();

private:
	/// \brief Buffer
	std::unique_ptr<char[]> m_buffer;

	/// \brief Buffer size
	std::size_t const m_size;

	/// \brief Start of usedArea
	std::size_t m_start = 0;
	/// \brief Start of freeArea
	std::size_t m_end = 0;
};
