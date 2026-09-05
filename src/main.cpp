#include "Application.hpp"

#include <GL/glew.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <iostream>
int main(int argc, char** argv) {
    (void)argc;
    glfwSetErrorCallback([](int code, const char* description) {
        std::cerr << "GLFW " << code << ": " << description << '\n';
    });
    if (!glfwInit())
        return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* video_mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
    if (video_mode) {
        glfwWindowHint(GLFW_RED_BITS, video_mode->redBits);
        glfwWindowHint(GLFW_GREEN_BITS, video_mode->greenBits);
        glfwWindowHint(GLFW_BLUE_BITS, video_mode->blueBits);
        glfwWindowHint(GLFW_REFRESH_RATE, video_mode->refreshRate);
    }
    int initial_width = video_mode ? video_mode->width : 1440;
    int initial_height = video_mode ? video_mode->height : 900;
    GLFWwindow* window = glfwCreateWindow(
        initial_width, initial_height, "EarthSim", video_mode ? monitor : nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glewExperimental = GL_TRUE;
    GLenum glew_result = glewInit();
    // GLEW may generate GL_INVALID_ENUM while probing a core-profile context.
    glGetError();
    if (glew_result != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW: "
                  << reinterpret_cast<const char*>(glewGetErrorString(glew_result)) << '\n';
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    if (!GLEW_VERSION_4_3) {
        std::cerr << "EarthSim requires OpenGL 4.3.\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    int result = 0;
    try {
        earth_sim::Application application(window, argv[0]);
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            application.frame();
        }
    } catch (const std::exception& error) {
        std::cerr << "EarthSim: " << error.what() << '\n';
        result = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
