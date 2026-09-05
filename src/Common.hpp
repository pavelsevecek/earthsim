#pragma once

#include <GL/glew.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <IMGUI.h>
#include <imgui_impl_glfw.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif


namespace earth_sim {
constexpr float pi = 3.14159265358979323846f;
constexpr float ui_scale = 1.4f;
struct Vec3 {
    float x;
    float y;
    float z;
};
inline Vec3 operator+(Vec3 a, Vec3 b) {
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}
inline Vec3 operator-(Vec3 a, Vec3 b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}
inline Vec3 operator*(Vec3 a, float s) {
    return { a.x * s, a.y * s, a.z * s };
}
inline float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 cross(Vec3 a, Vec3 b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
inline Vec3 normalize(Vec3 a) {
    return a * (1.0f / std::sqrt(dot(a, a)));
}
inline float mix(float a, float b, float t) {
    return a + (b - a) * t;
}
inline float smooth(float a, float b, float x) {
    float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
using Mat4 = std::array<float, 16>;
Mat4 multiply(const Mat4& a, const Mat4& b);
Mat4 perspective(float aspect, float distance);
Mat4 look_at(Vec3 eye, Vec3 forward, Vec3 right, Vec3 up);
} // namespace earth_sim
