// ftpd is a server implementation based on the following:
// - RFC  959 (https://tools.ietf.org/html/rfc959)
// - RFC 3659 (https://tools.ietf.org/html/rfc3659)
// - suggested implementation details from https://cr.yp.to/ftp/filesystem.html
// - Deflate transmission mode for FTP
//   (https://tools.ietf.org/html/draft-preston-ftpext-deflate-04)
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

#include "platform.h"

#include "ftpServer.h"

#include <imgui.h>

#include <GLFW/glfw3.h>

#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

namespace
{
/// \brief GLFW main window
std::unique_ptr<GLFWwindow, void (*) (GLFWwindow *)> s_mainWindow (nullptr, glfwDestroyWindow);

/// \brief Window resize callback
/// \param window_ GLFW window
/// \param width_ New window width
/// \param height_ New window height
void windowResize (GLFWwindow *const window_, int const width_, int const height_)
{
	(void)window_;

	if (!width_ || !height_)
		return;

	glViewport (0, 0, width_, height_);
}

#ifndef NDEBUG
/// \brief GL log callback
/// \param source_ Message source
/// \param type_ Message type
/// \param id_ Message id
/// \param severity_ Message severity
/// \param length_ Message length
/// \param userParam_ User parameter
void APIENTRY logCallback (GLenum const source_,
    GLenum const type_,
    GLuint const id_,
    GLenum const severity_,
    GLsizei const length_,
    GLchar const *const message_,
    void const *const userParam_)
{
	if (id_ == 131185)
		return;

	(void)source_;
	(void)type_;
	(void)severity_;
	(void)length_;
	(void)userParam_;
	std::fprintf (stderr, "%s\n", message_);
}
#endif
}

bool platform::init ()
{
	// initialize GLFW
	if (!glfwInit ())
	{
		std::fprintf (stderr, "Failed to initialize GLFW\n");
		return false;
	}

	// use OpenGL 4.3 Core Profile
	glfwWindowHint (GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint (GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifndef NDEBUG
	glfwWindowHint (GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
#endif

	// set depth buffer size
	glfwWindowHint (GLFW_DEPTH_BITS, 24);
	glfwWindowHint (GLFW_STENCIL_BITS, 8);

	// create GLFW window
	s_mainWindow.reset (glfwCreateWindow (1280, 720, STATUS_STRING, nullptr, nullptr));
	if (!s_mainWindow)
	{
		std::fprintf (stderr, "Failed to create window\n");
		glfwTerminate ();
		return false;
	}

	// enable vsync
	glfwSwapInterval (1);

	// create context
	glfwMakeContextCurrent (s_mainWindow.get ());
	glfwSetFramebufferSizeCallback (s_mainWindow.get (), windowResize);

	if (!ImGui_ImplGlfw_InitForOpenGL (s_mainWindow.get (), true))
	{
		std::fprintf (stderr, "Failed to init ImGui\n");
		glfwTerminate ();
		return false;
	}

	if (!ImGui_ImplOpenGL3_Init ())
	{
		std::fprintf (stderr, "Failed to init ImGui\n");
		ImGui_ImplGlfw_Shutdown ();
		glfwTerminate ();
		return false;
	}

#ifndef NDEBUG
	GLint flags;
	glGetIntegerv (GL_CONTEXT_FLAGS, &flags);
	if (flags & GL_CONTEXT_FLAG_DEBUG_BIT)
	{
		glEnable (GL_DEBUG_OUTPUT);
		glEnable (GL_DEBUG_OUTPUT_SYNCHRONOUS);

		using DEBUGPROC = void (APIENTRY *) (
		    GLenum, GLenum, GLuint, GLenum, GLsizei, GLchar const *, void const *);

		using DEBUGMESSAGECALLBACKPROC = void (APIENTRY *) (DEBUGPROC, void const *);

		using DEBUGMESSAGECONTROLPROC =
		    void (APIENTRY *) (GLenum, GLenum, GLenum, GLsizei, GLuint const *, GLboolean);

		auto const glDebugMessageCallback =
		    (DEBUGMESSAGECALLBACKPROC)glfwGetProcAddress ("glDebugMessageCallback");

		auto const glDebugMessageControl =
		    (DEBUGMESSAGECONTROLPROC)glfwGetProcAddress ("glDebugMessageControl");

		if (glDebugMessageCallback && glDebugMessageControl)
		{
			glDebugMessageCallback (logCallback, nullptr);
			glDebugMessageControl (GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
		}
	}
#endif

	glEnable (GL_CULL_FACE);
	glFrontFace (GL_CCW);
	glCullFace (GL_BACK);

	glEnable (GL_DEPTH_TEST);
	glDepthFunc (GL_LEQUAL);

	glClearColor (104.0f / 255.0f, 176.0f / 255.0f, 216.0f / 255.0f, 1.0f);
	glEnable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	std::printf ("Renderer:       %s\n", glGetString (GL_RENDERER));
	std::printf ("OpenGL Version: %s\n", glGetString (GL_VERSION));

	auto &io = ImGui::GetIO ();

	// disable imgui.ini file
	io.IniFilename = nullptr;

	return true;
}

bool platform::networkVisible ()
{
	return true;
}

bool platform::networkAddress (SockAddr &addr_)
{
	sockaddr_in addr;
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);

	addr_ = addr;
	return true;
}

std::string const &platform::hostname ()
{
	static std::string hostname = "switch-ftpd.local";
	if (hostname.empty ())
	{
		std::string buffer (256, '\0');
		gethostname (buffer.data (), buffer.size ());

		if (buffer.back () == 0) // check for truncation
		{
			hostname = std::move (buffer);
			hostname.resize (std::strlen (hostname.data ()));
		}
	}

	return hostname;
}

bool platform::loop ()
{
	bool inactive;
	do
	{
		inactive = glfwGetWindowAttrib (s_mainWindow.get (), GLFW_ICONIFIED);
		(inactive ? glfwWaitEvents : glfwPollEvents) ();

		if (glfwWindowShouldClose (s_mainWindow.get ()))
			return false;
	} while (inactive);

	ImGui_ImplOpenGL3_NewFrame ();
	ImGui_ImplGlfw_NewFrame ();
	ImGui::NewFrame ();

	return true;
}

void platform::render ()
{
	auto const freeSpace = FtpServer::getFreeSpace ();
	if (!freeSpace.empty ())
	{
		auto const &io    = ImGui::GetIO ();
		auto const &style = ImGui::GetStyle ();

		auto const size = ImGui::CalcTextSize (freeSpace.c_str ());
		auto const x    = io.DisplaySize.x - size.x - style.FramePadding.x;
		ImGui::GetForegroundDrawList ()->AddText (ImVec2 (x, style.FramePadding.y),
		    ImGui::GetColorU32 (ImGuiCol_Text),
		    freeSpace.c_str ());
	}

	ImGui::Render ();

	glClearColor (0.45f, 0.55f, 0.60f, 1.00f);
	glClear (GL_COLOR_BUFFER_BIT);

	ImGui_ImplOpenGL3_RenderDrawData (ImGui::GetDrawData ());

	glfwSwapBuffers (s_mainWindow.get ());
}

void platform::exit ()
{
	ImGui_ImplOpenGL3_Shutdown ();
	ImGui_ImplGlfw_Shutdown ();
	s_mainWindow.reset ();
	glfwTerminate ();
}

///////////////////////////////////////////////////////////////////////////
/// \brief Platform thread pimpl
class platform::Thread::privateData_t
{
public:
	privateData_t () = default;

	/// \brief Parameterized constructor
	/// \param func_ Thread entry point
	privateData_t (std::function<void ()> &&func_) : thread (std::move (func_))
	{
	}

	/// \brief Underlying thread object
	std::thread thread;
};

///////////////////////////////////////////////////////////////////////////
platform::Thread::~Thread () = default;

platform::Thread::Thread () : m_d (new privateData_t ())
{
}

platform::Thread::Thread (std::function<void ()> &&func_)
    : m_d (new privateData_t (std::move (func_)))
{
}

platform::Thread::Thread (Thread &&that_) : m_d (new privateData_t ())
{
	std::swap (m_d, that_.m_d);
}

platform::Thread &platform::Thread::operator= (Thread &&that_)
{
	std::swap (m_d, that_.m_d);
	return *this;
}

void platform::Thread::join ()
{
	m_d->thread.join ();
}

void platform::Thread::sleep (std::chrono::milliseconds const timeout_)
{
	std::this_thread::sleep_for (timeout_);
}

///////////////////////////////////////////////////////////////////////////
/// \brief Platform mutex pimpl
class platform::Mutex::privateData_t
{
public:
	/// \brief Underlying mutex
	std::mutex mutex;
};

///////////////////////////////////////////////////////////////////////////
platform::Mutex::~Mutex () = default;

platform::Mutex::Mutex () : m_d (new privateData_t ())
{
}

void platform::Mutex::lock ()
{
	m_d->mutex.lock ();
}

void platform::Mutex::unlock ()
{
	m_d->mutex.unlock ();
}
