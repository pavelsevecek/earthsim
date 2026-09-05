#pragma once

#include <memory>

struct GLFWwindow;

namespace earth_sim {
class AppState;

class Application {
    std::unique_ptr<AppState> state_;

public:
    Application(GLFWwindow* window, const char* executable_path);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void frame();
};
} // namespace earth_sim
