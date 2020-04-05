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

#ifdef _3DS
#include <3ds.h>
#endif

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

namespace platform
{
bool init ();
bool loop ();
void render ();
void exit ();

#ifdef _3DS
struct steady_clock
{
	using rep        = std::uint64_t;
	using period     = std::ratio<1, SYSCLOCK_ARM11>;
	using duration   = std::chrono::duration<rep, period>;
	using time_point = std::chrono::time_point<steady_clock>;

	constexpr static bool is_steady = true;
	static time_point now () noexcept
	{
		return time_point (duration (svcGetSystemTick ()));
	}
};
#else
using steady_clock = std::chrono::steady_clock;
#endif

class Thread
{
public:
	~Thread ();
	Thread ();

	Thread (std::function<void ()> func_);

	Thread (Thread const &that_) = delete;

	Thread (Thread &&that_);

	Thread &operator= (Thread const &that_) = delete;

	Thread &operator= (Thread &&that_);

	void join ();

	static void sleep (std::chrono::milliseconds timeout_);

private:
	class privateData_t;
	std::unique_ptr<privateData_t> m_d;
};

class Mutex
{
public:
	~Mutex ();
	Mutex ();

	void lock ();
	void unlock ();

private:
	class privateData_t;
	std::unique_ptr<privateData_t> m_d;
};
}
