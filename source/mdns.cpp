// ftpd is a server implementation based on the following:
// - RFC  959 (https://datatracker.ietf.org/doc/html/rfc959)
// - RFC 3659 (https://datatracker.ietf.org/doc/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
//
// ftpd implements mdns based on the following:
// - RFC 1035 (https://datatracker.ietf.org/doc/html/rfc1035)
// - RFC 6762 (https://datatracker.ietf.org/doc/html/rfc6762)
//
// Copyright (C) 2024 Michael Theall
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

#include "mdns.h"

#include "log.h"
#include "platform.h"

#include <arpa/inet.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <concepts>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>
using namespace std::chrono_literals;

static_assert (
    std::endian::native == std::endian::big || std::endian::native == std::endian::little);

static_assert (sizeof (in_addr_t) == 4);

namespace
{
constexpr auto MDNS_TTL = 120;

SockAddr const s_multicastAddress{inet_addr ("224.0.0.251"), 5353};

platform::steady_clock::time_point s_lastAnnounce{};
platform::steady_clock::time_point s_lastProbe{};

std::string s_hostname      = platform::hostname ();
std::string s_hostnameLocal = s_hostname + ".local";

enum class State
{
	Probe1,
	Probe2,
	Probe3,
	Announce1,
	Announce2,
	Complete,
	Conflict,
};

auto s_state = State::Probe1;

#if __has_cpp_attribute(__cpp_lib_byteswap)
template <std::integral T>
using byteswap = std::byteswap<T>;
#else
template <std::integral T>
constexpr T byteswap (T const value_) noexcept
{
	static_assert (std::has_unique_object_representations_v<T>, "T may not have padding bits");
	auto buffer = std::bit_cast<std::array<std::byte, sizeof (T)>> (value_);
	std::ranges::reverse (buffer);
	return std::bit_cast<T> (buffer);
}
#endif

template <std::integral T>
constexpr T hton (T const value_) noexcept
{
	if constexpr (std::endian::native == std::endian::big)
		return value_;
	else
		return byteswap (value_);
}

template <std::integral T>
constexpr T ntoh (T const value_) noexcept
{
	if constexpr (std::endian::native == std::endian::big)
		return value_;
	else
		return byteswap (value_);
}

template <std::integral T, std::integral U>
void const *decode (void const *const buffer_, U &size_, T &out_, bool networkToHost_ = true)
{
	if (!buffer_)
		return nullptr;

	if (size_ < 0 || static_cast<std::make_unsigned_t<T>> (size_) < sizeof (T))
		return nullptr;

	std::memcpy (&out_, buffer_, sizeof (T));

	if (networkToHost_)
		out_ = ntoh (out_);

	size_ -= sizeof (T);
	return static_cast<std::uint8_t const *> (buffer_) + sizeof (T);
}

template <std::integral T>
void const *decode (void const *buffer_, T &size_, std::string &out_)
{
	auto p         = static_cast<char const *> (buffer_);
	auto const end = p + size_;

	std::string result;
	result.reserve (size_);

	while (p < end && *p)
	{
		auto const len = *p++;

		// punt on compressed labels
		if (len & 0xC0)
			return nullptr;

		if (p + len >= end)
			return nullptr;

		if (!result.empty ())
			result.push_back ('.');

		result.insert (std::end (result), p, p + len);
		p += len;
	}

	++p;

	out_ = std::move (result);

	size_ = end - p;
	return p;
}

template <std::integral T, std::integral U>
void *encode (void *const buffer_, U &size_, T in_, bool hostToNetwork_ = true)
{
	if (!buffer_)
		return nullptr;

	if (size_ < sizeof (T))
		return nullptr;

	if (hostToNetwork_)
		in_ = hton (in_);

	std::memcpy (buffer_, &in_, sizeof (T));

	size_ -= sizeof (T);
	return static_cast<std::uint8_t *> (buffer_) + sizeof (T);
}

template <std::integral T>
void *encode (void *const buffer_, T &size_, std::string const &in_)
{
	// names are limited to 255 bytes
	if (in_.size () > 0xFF)
		return nullptr;

	auto p         = static_cast<char *> (buffer_);
	auto const end = p + size_;

	std::string::size_type prev = 0;
	std::string::size_type pos  = 0;
	while (p < end && pos != std::string::npos)
	{
		pos = in_.find ('.', prev);

		auto const label = std::string_view (in_).substr (prev, pos);

		// labels are limited to 63 bytes
		if (label.size () >= size_ || label.size () > 0x3F)
			return nullptr;

		p = static_cast<char *> (encode<std::uint8_t> (p, size_, label.size ()));
		if (!p)
			return nullptr;

		std::memcpy (p, label.data (), label.size ());

		p += label.size ();

		if (pos != std::string::npos)
			prev = pos + 1;
	}

	if (p == end)
		return nullptr;

	*p++ = 0;

	size_ = end - p;
	return p;
}

struct DNSHeader
{
	std::uint16_t id{};
	std::uint16_t flags{};
	std::uint16_t qdCount{};
	std::uint16_t anCount{};
	std::uint16_t nsCount{};
	std::uint16_t arCount{};

	template <std::integral T>
	void const *decode (void const *const buffer_, T &size_)
	{
		auto in = ::decode (buffer_, size_, id);
		in      = ::decode (buffer_, size_, flags);
		in      = ::decode (buffer_, size_, qdCount);
		in      = ::decode (buffer_, size_, anCount);
		in      = ::decode (buffer_, size_, nsCount);
		in      = ::decode (buffer_, size_, arCount);

		return buffer_;
	}

	template <std::integral T>
	void *encode (void *buffer_, T &size_)
	{
		buffer_ = ::encode (buffer_, size_, id);
		buffer_ = ::encode (buffer_, size_, flags);
		buffer_ = ::encode (buffer_, size_, qdCount);
		buffer_ = ::encode (buffer_, size_, anCount);
		buffer_ = ::encode (buffer_, size_, nsCount);
		buffer_ = ::encode (buffer_, size_, arCount);

		return buffer_;
	}
};

struct QueryRecord
{
	std::string qname{};
	std::uint16_t qtype{};
	std::uint16_t qclass{};

	template <std::integral T>
	void const *decode (void const *buffer_, T &size_)
	{
		buffer_ = ::decode (buffer_, size_, qname);
		buffer_ = ::decode (buffer_, size_, qtype);
		buffer_ = ::decode (buffer_, size_, qclass);

		return buffer_;
	}

	template <std::integral T>
	void *encode (void *buffer_, T &size_)
	{
		buffer_ = ::encode (buffer_, size_, qname);
		buffer_ = ::encode (buffer_, size_, qtype);
		buffer_ = ::encode (buffer_, size_, qclass);

		return buffer_;
	}
};

struct ResourceRecord
{
	std::string rname{};
	std::uint16_t rtype{};
	std::uint16_t rclass{};
	std::uint32_t rttl{};
	std::uint16_t rlen{};
	std::vector<std::uint8_t> rdata{};

	template <std::integral T>
	void const *decode (void const *buffer_, T &size_)
	{
		buffer_ = ::decode (buffer_, size_, rname);
		buffer_ = ::decode (buffer_, size_, rtype);
		buffer_ = ::decode (buffer_, size_, rclass);
		buffer_ = ::decode (buffer_, size_, rttl);
		buffer_ = ::decode (buffer_, size_, rlen);

		return buffer_;
	}

	template <std::integral T>
	void *encode (void *buffer_, T &size_)
	{
		if (rttl > std::numeric_limits<std::int32_t>::max ())
			return nullptr;

		buffer_ = ::encode (buffer_, size_, rname);
		buffer_ = ::encode (buffer_, size_, rtype);
		buffer_ = ::encode (buffer_, size_, rclass);
		buffer_ = ::encode (buffer_, size_, rttl);
		buffer_ = ::encode (buffer_, size_, rlen);

		if (rlen > size_)
			return nullptr;

		rdata.resize (rlen);
		std::memcpy (rdata.data (), buffer_, rlen);

		size_ -= rlen;
		return static_cast<std::uint8_t *> (buffer_) + rlen;
	}
};

void probe (Socket *const socket_, std::string const &qname_)
{
	std::vector<std::uint8_t> response (65536);
	auto available = response.size ();

	auto out = DNSHeader{.qdCount = 1}.encode (response.data (), available);
	out      = QueryRecord{.qname = qname_, .qtype = 255, .qclass = 1}.encode (out, available);

	if (!out)
		return;

	info ("Probe mDNS %s\n", qname_.c_str ());

	socket_->writeTo (response.data (), response.size () - available, s_multicastAddress);
	s_lastProbe = platform::steady_clock::now ();
}

void announce (Socket *const socket_,
    SockAddr const *srcAddr_,
    std::uint16_t const id_,
    std::uint16_t const flags_,
    QueryRecord const &record_,
    SockAddr const &addr_)
{
	std::vector<std::uint8_t> response (65536);
	auto available = response.size ();

	// header
	auto out = encode<std::uint16_t> (response.data (), available, id_);
	out =
	    encode<std::uint16_t> (out, available, flags_ | (1 << 15) | (1 << 10)); // mark response/AA
	out = encode<std::uint16_t> (out, available, 0);
	out = encode<std::uint16_t> (out, available, 1);
	out = encode<std::uint16_t> (out, available, 0);
	out = encode<std::uint16_t> (out, available, 0);

	// answer section
	out = encode (out, available, record_.qname);
	out = encode<std::uint16_t> (out, available, record_.qtype);
	out = encode<std::uint16_t> (out, available, record_.qclass | (1 << 15)); // mark unique/flush
	out = encode<std::uint32_t> (out, available, MDNS_TTL);
	out = encode<std::uint16_t> (out, available, sizeof (in_addr_t));
	out = encode<in_addr_t> (
	    out, available, static_cast<sockaddr_in const &> (addr_).sin_addr.s_addr, false);

	if (!out)
		return;

	auto const preferUnicast = srcAddr_ && ((record_.qclass >> 15) & 0x1);

	if (preferUnicast)
	{
		auto const name = std::string (addr_.name ());
		info (
		    "Respond mDNS %s %s to %s\n", record_.qname.c_str (), name.c_str (), srcAddr_->name ());
		socket_->writeTo (response.data (), response.size () - available, *srcAddr_);
	}

	auto const now = platform::steady_clock::now ();
	if (!preferUnicast || now - s_lastAnnounce > std::chrono::seconds (MDNS_TTL / 4))
	{
		info ("Announce mDNS %s %s\n", record_.qname.c_str (), addr_.name ());
		socket_->writeTo (response.data (), response.size () - available, s_multicastAddress);
		s_lastAnnounce = now;
	}
}
}

void mdns::setHostname (std::string hostname_)
{
	if (hostname_.empty ())
		hostname_ = platform::hostname ();

	if (s_hostname == hostname_)
		return;

	s_hostname      = std::move (hostname_);
	s_hostnameLocal = s_hostname + ".local";

	s_state     = State::Probe1;
	s_lastProbe = platform::steady_clock::now ();
}

UniqueSocket mdns::createSocket ()
{
	auto socket = Socket::create (Socket::eDatagram);
	if (!socket)
		return nullptr;

	if (!socket->setReuseAddress ())
		return nullptr;

	auto iface = SockAddr::AnyIPv4;
	iface.setPort (s_multicastAddress.port ());
	if (!socket->bind (iface))
		return nullptr;

	if (!socket->joinMulticastGroup (s_multicastAddress, iface))
		return nullptr;

	s_state     = State::Probe1;
	s_lastProbe = platform::steady_clock::now ();

	return socket;
}

void mdns::handleSocket (Socket *socket_, SockAddr const &addr_)
{
	if (!socket_)
		return;

	// only support IPv4 for now
	if (addr_.domain () != SockAddr::Domain::IPv4)
		return;

	auto const now = platform::steady_clock::now ();

	switch (s_state)
	{
	case State::Probe1:
	case State::Probe2:
	case State::Probe3:
		if (now - s_lastProbe > 250ms)
		{
			probe (socket_, s_hostname);
			s_state = static_cast<State> (static_cast<int> (s_state) + 1);
		}
		break;

	case State::Announce1:
	case State::Announce2:
		if (now - s_lastAnnounce > 1s)
		{
			announce (socket_,
			    nullptr,
			    0,
			    0,
			    QueryRecord{.qname = s_hostname, .qtype = 1, .qclass = 1},
			    addr_);
			s_state = static_cast<State> (static_cast<int> (s_state) + 1);
		}

	default:
		break;
	}

	Socket::PollInfo pollInfo{*socket_, POLLIN, 0};
	auto const rc = Socket::poll (&pollInfo, 1, 0ms);
	if (rc <= 0 || !(pollInfo.revents & POLLIN))
		return;

	SockAddr srcAddr;
	std::vector<std::uint8_t> buffer (65536);
	auto bytes = socket_->readFrom (buffer.data (), buffer.size (), srcAddr);
	if (bytes <= 0)
		return;

	// only support IPv4 for now
	if (srcAddr.domain () != SockAddr::Domain::IPv4)
		return;

	// ignore loopback
	if (std::memcmp (&reinterpret_cast<sockaddr_in const &> (srcAddr).sin_addr.s_addr,
	        &reinterpret_cast<sockaddr_in const &> (addr_).sin_addr.s_addr,
	        sizeof (in_addr_t)) == 0)
		return;

	std::uint16_t id;
	std::uint16_t flags;
	std::uint16_t qdCount;
	std::uint16_t anCount;
	std::uint16_t nsCount;
	std::uint16_t arCount;

	// parse header
	auto in = decode (buffer.data (), bytes, id);
	in      = decode (in, bytes, flags);
	in      = decode (in, bytes, qdCount);
	in      = decode (in, bytes, anCount);
	in      = decode (in, bytes, nsCount);
	in      = decode (in, bytes, arCount);

	if (!in)
		return;

	auto const qr = (flags >> 15) & 0x1;

	// ill-formed on queries and responses
	auto const opcode = (flags >> 11) & 0xF;
	if (opcode != 0)
		return;

	// ill-formed on queries
	if (!qr && ((flags >> 10) & 0x1))
		return;

	// punt on truncated messages
	if ((flags >> 9) & 0x1)
		return;

	// ill-formed on queries
	if (!qr && ((flags >> 7) & 0x1))
		return;

	// must be zero
	if ((flags >> 4) & 0x7)
		return;

	// ill-formed on queries and responses
	if ((flags >> 0) & 0xF)
		return;

	// std::vector<std::uint8_t> response (65536);
	//	void *out      = response.data ();
	//  auto available = response.size ();

	std::vector<ResourceRecord> answers;

	bool announced = false;
	for (unsigned i = 0; i < qdCount; ++i)
	{
		QueryRecord record;
		in = record.decode (in, bytes);

		if (!in)
			return;

		// only respond to queries
		if (qr)
			continue;

		// only accept A or ANY type
		if (record.qtype != 1 && record.qtype != 255)
			continue;

		// only accept IN or ANY class
		if ((record.qclass & 0x7FFF) != 1 && (record.qclass & 0x7FFF) != 255)
			continue;

		if (record.qname != s_hostname && record.qname != s_hostnameLocal)
			continue;

		if (!announced)
		{
			std::vector<std::uint8_t> data (sizeof (in_addr_t));
			auto n = data.size ();
			encode (data.data (),
			    n,
			    static_cast<sockaddr_in const &> (addr_).sin_addr.s_addr,
			    false);

			answers.emplace_back (ResourceRecord{// answer
			    .rname  = record.qname,
			    .rtype  = 1,
			    .rclass = static_cast<std::uint16_t> (1 | (1 << 15)),
			    .rttl   = MDNS_TTL,
			    .rlen   = sizeof (in_addr_t),
			    .rdata  = std::move (data)});

			announce (socket_, &srcAddr, id, flags, record, addr_);
			announced = true;
		}
	}

	for (unsigned i = 0; i < anCount; ++i)
	{
		ResourceRecord record;
		in = record.decode (in, bytes);

		if (!in)
			return;
	}
}
