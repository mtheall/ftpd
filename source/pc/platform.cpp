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

#include "platform.h"

#include "imgui.h"

#include <glad/glad.h>

#include <GLFW/glfw3.h>

#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>

namespace
{
std::unique_ptr<GLFWwindow, void (*) (GLFWwindow *)> s_mainWindow (nullptr, glfwDestroyWindow);

void windowResize (GLFWwindow *const window_, int const width_, int const height_)
{
	(void)window_;

	if (!width_ || !height_)
		return;

	glViewport (0, 0, width_, height_);
}

#ifndef NDEBUG
void logCallback (GLenum const source_,
    GLenum const type_,
    GLuint const id_,
    GLenum const severity_,
    GLsizei const length_,
    GLchar const *const message_,
    void const *const userParam_)
{
	(void)source_;
	(void)type_;
	(void)id_;
	(void)severity_;
	(void)length_;
	(void)userParam_;
	// std::fprintf (stderr, "%s\n", message_);
}
#endif
}

bool platform::init ()
{
	if (!glfwInit ())
	{
		std::fprintf (stderr, "Failed to initialize GLFW\n");
		return false;
	}

	glfwWindowHint (GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint (GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint (GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifndef NDEBUG
	glfwWindowHint (GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
#endif

	// set depth buffer size
	glfwWindowHint (GLFW_DEPTH_BITS, 24);
	glfwWindowHint (GLFW_STENCIL_BITS, 8);

	s_mainWindow.reset (glfwCreateWindow (1280, 720, "Test Game", nullptr, nullptr));
	if (!s_mainWindow)
	{
		std::fprintf (stderr, "Failed to create window\n");
		glfwTerminate ();
		return false;
	}

	glfwSwapInterval (1);

	// create context
	glfwMakeContextCurrent (s_mainWindow.get ());
	glfwSetFramebufferSizeCallback (s_mainWindow.get (), windowResize);

	if (!gladLoadGL ())
	{
		std::fprintf (stderr, "gladLoadGL: failed\n");
		platform::exit ();
		return false;
	}

#ifndef NDEBUG
	GLint flags;
	glGetIntegerv (GL_CONTEXT_FLAGS, &flags);
	if (flags & GL_CONTEXT_FLAG_DEBUG_BIT)
	{
		glEnable (GL_DEBUG_OUTPUT);
		glEnable (GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageCallback (logCallback, nullptr);
		glDebugMessageControl (GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
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

	IMGUI_CHECKVERSION ();
	ImGui::CreateContext ();

	ImGui_ImplGlfw_InitForOpenGL (s_mainWindow.get (), true);
	ImGui_ImplOpenGL3_Init ("#version 150");

	ImGuiIO &io    = ImGui::GetIO ();
	io.IniFilename = nullptr;

	return true;
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
	ImGui::Render ();

	int width;
	int height;
	glfwGetFramebufferSize (s_mainWindow.get (), &width, &height);
	glViewport (0, 0, width, height);
	glClearColor (0.45f, 0.55f, 0.60f, 1.00f);
	glClear (GL_COLOR_BUFFER_BIT);

	ImGui_ImplOpenGL3_RenderDrawData (ImGui::GetDrawData ());

	glfwSwapBuffers (s_mainWindow.get ());
}

void platform::exit ()
{
	ImGui::DestroyContext ();

	s_mainWindow.reset ();
	glfwTerminate ();
}

///////////////////////////////////////////////////////////////////////////
class platform::Thread::privateData_t
{
public:
	privateData_t () = default;

	privateData_t (std::function<void ()> func_) : thread (func_)
	{
	}

	std::thread thread;
};

///////////////////////////////////////////////////////////////////////////
platform::Thread::~Thread () = default;

platform::Thread::Thread () : m_d (new privateData_t ())
{
}

platform::Thread::Thread (std::function<void ()> func_) : m_d (new privateData_t (func_))
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
class platform::Mutex::privateData_t
{
public:
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
