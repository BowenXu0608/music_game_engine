// ============================================================================
// GLFW Stub for Android Build
// Provides type declarations and constants so that existing headers
// (VulkanContext.h, Swapchain.h, Renderer.h, InputManager.h, ImGuiLayer.h)
// compile on Android without the real GLFW library.
// None of the GLFW functions are actually called on Android.
// ============================================================================
#pragma once

// Opaque window type — never dereferenced on Android
struct GLFWwindow;

// Action constants used by InputManager.h
#define GLFW_RELEASE 0
#define GLFW_PRESS   1
#define GLFW_REPEAT  2

// Key constants used by InputManager::keyToLane()
#define GLFW_KEY_0  48
#define GLFW_KEY_1  49
#define GLFW_KEY_2  50
#define GLFW_KEY_3  51
#define GLFW_KEY_4  52
#define GLFW_KEY_5  53
#define GLFW_KEY_6  54
#define GLFW_KEY_7  55
#define GLFW_KEY_8  56
#define GLFW_KEY_9  57
#define GLFW_KEY_Q  81
#define GLFW_KEY_W  87

#define GLFW_KEY_ESCAPE 256

// Window hints (referenced in some cpp files — provide stubs so they compile)
#define GLFW_CLIENT_API    0x00022001
#define GLFW_NO_API        0
#define GLFW_RESIZABLE     0x00020003
#define GLFW_TRUE          1
#define GLFW_FALSE         0

// Stub function declarations (never linked — Android .cpp files don't call these)
inline int  glfwInit() { return 0; }
inline void glfwTerminate() {}
inline void glfwWindowHint(int, int) {}
inline GLFWwindow* glfwCreateWindow(int, int, const char*, void*, void*) { return nullptr; }
inline void glfwDestroyWindow(GLFWwindow*) {}
inline int  glfwWindowShouldClose(GLFWwindow*) { return 0; }
inline void glfwSetWindowShouldClose(GLFWwindow*, int) {}
inline void glfwPollEvents() {}
inline void glfwWaitEvents() {}
inline void glfwGetFramebufferSize(GLFWwindow*, int* w, int* h) { if(w) *w=0; if(h) *h=0; }
inline void glfwSetWindowUserPointer(GLFWwindow*, void*) {}
inline void* glfwGetWindowUserPointer(GLFWwindow*) { return nullptr; }
inline double glfwGetTime() { return 0.0; }

// Callback type stubs
typedef void (*GLFWframebuffersizefun)(GLFWwindow*, int, int);
typedef void (*GLFWkeyfun)(GLFWwindow*, int, int, int, int);
typedef void (*GLFWmousebuttonfun)(GLFWwindow*, int, int, int);
typedef void (*GLFWcursorposfun)(GLFWwindow*, double, double);
typedef void (*GLFWdropfun)(GLFWwindow*, int, const char**);
typedef void (*GLFWscrollfun)(GLFWwindow*, double, double);

inline GLFWframebuffersizefun glfwSetFramebufferSizeCallback(GLFWwindow*, GLFWframebuffersizefun) { return nullptr; }
inline GLFWkeyfun glfwSetKeyCallback(GLFWwindow*, GLFWkeyfun) { return nullptr; }
inline GLFWmousebuttonfun glfwSetMouseButtonCallback(GLFWwindow*, GLFWmousebuttonfun) { return nullptr; }
inline GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow*, GLFWcursorposfun) { return nullptr; }
inline GLFWdropfun glfwSetDropCallback(GLFWwindow*, GLFWdropfun) { return nullptr; }
inline GLFWscrollfun glfwSetScrollCallback(GLFWwindow*, GLFWscrollfun) { return nullptr; }

// Mouse button constants
#define GLFW_MOUSE_BUTTON_LEFT 0

// Vulkan surface stubs — never called on Android
#include <vulkan/vulkan.h>
inline const char** glfwGetRequiredInstanceExtensions(unsigned int* count) { if(count) *count=0; return nullptr; }
inline VkResult glfwCreateWindowSurface(VkInstance, GLFWwindow*, const VkAllocationCallbacks*, VkSurfaceKHR*) { return VK_ERROR_INITIALIZATION_FAILED; }
