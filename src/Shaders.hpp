#pragma once

#include "Common.hpp"

namespace earth_sim {
std::filesystem::path shader_directory(const char* argv0);
GLuint compile_shader(const std::filesystem::path& file, GLenum type);
GLuint program(const std::filesystem::path& directory, const std::string& name);
GLuint compute_program(const std::filesystem::path& directory, const std::string& name);
void uniform(GLuint program, const char* name, Vec3 value);
void uniform(GLuint program, const char* name, float value);
} // namespace earth_sim
