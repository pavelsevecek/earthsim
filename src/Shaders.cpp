#include "Shaders.hpp"

namespace earth_sim {
std::filesystem::path shader_directory(const char* argv0) {
    std::filesystem::path executable = std::filesystem::absolute(argv0);
#ifdef _WIN32
    std::vector<wchar_t> path(32768);
    DWORD count = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (count > 0 && count < path.size())
        executable = std::wstring(path.data(), count);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> path(size);
    if (_NSGetExecutablePath(path.data(), &size) == 0)
        executable = path.data();
#elif defined(__linux__)
    std::array<char, 4096> path{};
    auto count = readlink("/proc/self/exe", path.data(), path.size() - 1);
    if (count > 0)
        executable = std::string(path.data(), size_t(count));
#endif
    auto adjacent = executable.parent_path() / "shaders";
    if (std::filesystem::is_directory(adjacent))
        return adjacent;
    if (std::filesystem::is_directory("shaders"))
        return std::filesystem::absolute("shaders");
    throw std::runtime_error(
        "Cannot find shaders directory beside EarthSim or in the working directory.");
}
GLuint compile_shader(const std::filesystem::path& file, GLenum type) {
    std::ifstream stream(file);
    if (!stream)
        throw std::runtime_error("Cannot read shader: " + file.string());
    std::string source((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    const char* text = source.c_str();
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint size = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &size);
        std::string log(size_t(std::max(size, 1)), '\0');
        glGetShaderInfoLog(shader, size, nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error(file.string() + ":\n" + log);
    }
    return shader;
}
GLuint program(const std::filesystem::path& directory, const std::string& name) {
    GLuint vs = compile_shader(directory / (name + ".vert"), GL_VERTEX_SHADER);
    GLuint fs = 0;
    try {
        fs = compile_shader(directory / (name + ".frag"), GL_FRAGMENT_SHADER);
    } catch (...) {
        glDeleteShader(vs);
        throw;
    }
    GLuint result = glCreateProgram();
    glAttachShader(result, vs);
    glAttachShader(result, fs);
    glLinkProgram(result);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(result, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint size = 0;
        glGetProgramiv(result, GL_INFO_LOG_LENGTH, &size);
        std::string log(size_t(std::max(size, 1)), '\0');
        glGetProgramInfoLog(result, size, nullptr, log.data());
        glDeleteProgram(result);
        throw std::runtime_error(name + " program link failed:\n" + log);
    }
    return result;
}
GLuint compute_program(const std::filesystem::path& directory, const std::string& name) {
    GLuint shader = compile_shader(directory / (name + ".comp"), GL_COMPUTE_SHADER);
    GLuint result = glCreateProgram();
    glAttachShader(result, shader);
    glLinkProgram(result);
    glDeleteShader(shader);
    GLint ok = 0;
    glGetProgramiv(result, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint size = 0;
        glGetProgramiv(result, GL_INFO_LOG_LENGTH, &size);
        std::string log(size_t(std::max(size, 1)), '\0');
        glGetProgramInfoLog(result, size, nullptr, log.data());
        glDeleteProgram(result);
        throw std::runtime_error(name + " compute program link failed:\n" + log);
    }
    return result;
}
void uniform(GLuint p, const char* name, Vec3 v) {
    glUniform3f(glGetUniformLocation(p, name), v.x, v.y, v.z);
}
void uniform(GLuint p, const char* name, float v) {
    glUniform1f(glGetUniformLocation(p, name), v);
}


} // namespace earth_sim
