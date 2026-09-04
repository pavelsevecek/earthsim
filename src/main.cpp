#define GLAD_GL_IMPLEMENTATION
#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
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

namespace {
#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER 0x91B9
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#define GL_SHADER_STORAGE_BARRIER_BIT 0x2000
#endif
	using DispatchComputeProc = void (APIENTRY*)(GLuint, GLuint, GLuint);
	using MemoryBarrierProc = void (APIENTRY*)(GLbitfield);
	DispatchComputeProc dispatchCompute = nullptr;
	MemoryBarrierProc memoryBarrier = nullptr;
	constexpr float pi = 3.14159265358979323846f;
	constexpr float uiScale = 1.6f;
	uint32_t terrainSeed = 0;
	struct MountainRange {
		float centerX, centerZ, angle, length, width, bend, wave, phase, amplitude;
	};
	std::vector<MountainRange> mountainRanges;
	void generateMountainRanges(uint32_t seed) {
		std::mt19937 random(seed ^ 0xa511e9b3u);
		auto value = [&](float low, float high) {return std::uniform_real_distribution<float>(low, high)(random); };
		int count = std::uniform_int_distribution<int>(3, 5)(random);
		mountainRanges.clear(); mountainRanges.reserve(size_t(count));
		for (int i = 0; i < count; ++i) mountainRanges.push_back({ value(-380,380),value(-380,380),value(0,pi),
			value(430,780),value(105,205),value(45,125),value(0.004f,0.009f),value(0,2 * pi),value(0.72f,1.0f) });
	}
	struct Vec3 { float x, y, z; };
	Vec3 operator+(Vec3 a, Vec3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	Vec3 operator-(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	Vec3 operator*(Vec3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
	float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	Vec3 cross(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
	Vec3 normalize(Vec3 a) { return a * (1.0f / std::sqrt(dot(a, a))); }
	float mix(float a, float b, float t) { return a + (b - a) * t; }
	float smooth(float a, float b, float x) { float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3 - 2 * t); }
	using Mat4 = std::array<float, 16>;
	Mat4 multiply(const Mat4& a, const Mat4& b) {
		Mat4 result{};
		for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
			for (int k = 0; k < 4; ++k) result[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k];
		return result;
	}
	Mat4 perspective(float aspect, float distance) {
		float f = 1.0f / std::tan(pi / 8.0f);
		float nearPlane = std::max(0.001f, distance * 0.0001f), farPlane = std::max(6000.0f, distance + 4000.0f);
		return { f / aspect,0,0,0, 0,f,0,0, 0,0,(farPlane + nearPlane) / (nearPlane - farPlane),-1,
			0,0,2 * farPlane * nearPlane / (nearPlane - farPlane),0 };
	}
	Mat4 lookAt(Vec3 eye, Vec3 forward, Vec3 right, Vec3 up) {
		return { right.x,up.x,-forward.x,0, right.y,up.y,-forward.y,0,
			right.z,up.z,-forward.z,0, -dot(right,eye),-dot(up,eye),dot(forward,eye),1 };
	}

	// Seeded, portable integer hash and quintic gradient noise; no external noise library.
	uint32_t hash(int x, int z) {
		uint32_t h = uint32_t(x) * 0x8da6b343u ^ uint32_t(z) * 0xd8163841u
			^ terrainSeed * 0x9e3779b9u ^ 0xcb1ab31fu;
		h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; return h ^ (h >> 16);
	}
	float gradient(int x, int z, float dx, float dz) {
		constexpr float d = 0.70710678f;
		constexpr float gradients[8][2] = { {1,0},{-1,0},{0,1},{0,-1},{d,d},{-d,d},{d,-d},{-d,-d} };
		const auto& g = gradients[hash(x, z) & 7u]; return g[0] * dx + g[1] * dz;
	}
	float noise(float x, float z) {
		int ix = int(std::floor(x)), iz = int(std::floor(z));
		float dx = x - float(ix), dz = z - float(iz);
		auto fade = [](float t) {return t * t * t * (t * (t * 6 - 15) + 10); };
		return mix(mix(gradient(ix, iz, dx, dz), gradient(ix + 1, iz, dx - 1, dz), fade(dx)),
			mix(gradient(ix, iz + 1, dx, dz - 1), gradient(ix + 1, iz + 1, dx - 1, dz - 1), fade(dx)), fade(dz));
	}
	float height(float x, float z) {
		float wx = x + 65 * noise(x * 0.002f + 13, z * 0.002f + 7);
		float wz = z + 65 * noise(x * 0.002f - 23, z * 0.002f + 41);
		float ridge = 0, amplitude = 1, frequency = 0.004f, weight = 1;
		for (int i = 0; i < 7; ++i) {
			float n = 1 - std::min(1.0f, std::abs(noise(wx * frequency + 17.3f, wz * frequency + 8.1f)) * 1.65f);
			n = n * n * weight;
			ridge += n * amplitude; weight = std::clamp(n * 1.8f, 0.0f, 1.0f);
			frequency *= 2.07f; amplitude *= 0.48f;
		}
		float envelope = 0;
		for (const auto& range : mountainRanges) {
			float dx = wx - range.centerX, dz = wz - range.centerZ;
			float along = dx * std::cos(range.angle) + dz * std::sin(range.angle);
			float across = -dx * std::sin(range.angle) + dz * std::cos(range.angle);
			float spine = across - range.bend * std::sin(along * range.wave + range.phase);
			float shaped = range.amplitude * std::exp(-spine * spine / (range.width * range.width)
				- along * along / (range.length * range.length));
			envelope = std::max(envelope, shaped);
		}
		float edge = 1 - smooth(760, 1000, std::max(std::abs(x), std::abs(z)));
		return 0.5f * edge * (8 + 175 * ridge * envelope + 12 * noise(x * 0.01f, z * 0.01f));
	}

	std::filesystem::path shaderDirectory(const char* argv0) {
		std::filesystem::path executable = std::filesystem::absolute(argv0);
#ifdef _WIN32
		std::vector<wchar_t> path(32768);
		DWORD count = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
		if (count > 0 && count < path.size()) executable = std::wstring(path.data(), count);
#elif defined(__APPLE__)
		uint32_t size = 0; _NSGetExecutablePath(nullptr, &size);
		std::vector<char> path(size);
		if (_NSGetExecutablePath(path.data(), &size) == 0) executable = path.data();
#elif defined(__linux__)
		std::array<char, 4096> path{};
		auto count = readlink("/proc/self/exe", path.data(), path.size() - 1);
		if (count > 0) executable = std::string(path.data(), size_t(count));
#endif
		auto adjacent = executable.parent_path() / "shaders";
		if (std::filesystem::is_directory(adjacent)) return adjacent;
		if (std::filesystem::is_directory("shaders")) return std::filesystem::absolute("shaders");
		throw std::runtime_error("Cannot find shaders directory beside EarthSim or in the working directory.");
	}
	GLuint compileShader(const std::filesystem::path& file, GLenum type) {
		std::ifstream stream(file);
		if (!stream) throw std::runtime_error("Cannot read shader: " + file.string());
		std::string source((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
		const char* text = source.c_str(); GLuint shader = glCreateShader(type);
		glShaderSource(shader, 1, &text, nullptr); glCompileShader(shader);
		GLint ok = 0; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
		if (!ok) {
			GLint size = 0; glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &size);
			std::string log(size_t(std::max(size, 1)), '\0'); glGetShaderInfoLog(shader, size, nullptr, log.data());
			glDeleteShader(shader); throw std::runtime_error(file.string() + ":\n" + log);
		}
		return shader;
	}
	GLuint program(const std::filesystem::path& directory, const std::string& name) {
		GLuint vs = compileShader(directory / (name + ".vert"), GL_VERTEX_SHADER), fs = 0;
		try { fs = compileShader(directory / (name + ".frag"), GL_FRAGMENT_SHADER); }
		catch (...) { glDeleteShader(vs); throw; }
		GLuint result = glCreateProgram(); glAttachShader(result, vs); glAttachShader(result, fs); glLinkProgram(result);
		glDeleteShader(vs); glDeleteShader(fs);
		GLint ok = 0; glGetProgramiv(result, GL_LINK_STATUS, &ok);
		if (!ok) {
			GLint size = 0; glGetProgramiv(result, GL_INFO_LOG_LENGTH, &size);
			std::string log(size_t(std::max(size, 1)), '\0'); glGetProgramInfoLog(result, size, nullptr, log.data());
			glDeleteProgram(result); throw std::runtime_error(name + " program link failed:\n" + log);
		}
		return result;
	}
	GLuint computeProgram(const std::filesystem::path& directory, const std::string& name) {
		GLuint shader = compileShader(directory / (name + ".comp"), GL_COMPUTE_SHADER);
		GLuint result = glCreateProgram(); glAttachShader(result, shader); glLinkProgram(result); glDeleteShader(shader);
		GLint ok = 0; glGetProgramiv(result, GL_LINK_STATUS, &ok);
		if (!ok) {
			GLint size = 0; glGetProgramiv(result, GL_INFO_LOG_LENGTH, &size);
			std::string log(size_t(std::max(size, 1)), '\0'); glGetProgramInfoLog(result, size, nullptr, log.data());
			glDeleteProgram(result); throw std::runtime_error(name + " compute program link failed:\n" + log);
		}
		return result;
	}
	void uniform(GLuint p, const char* name, Vec3 v) { glUniform3f(glGetUniformLocation(p, name), v.x, v.y, v.z); }
	void uniform(GLuint p, const char* name, float v) { glUniform1f(glGetUniformLocation(p, name), v); }

	struct Terrain {
		struct Vertex { Vec3 position, normal; };
		static constexpr int cells = 512;
		static constexpr float step = 2000.0f / cells;
		std::vector<float> heights;
		std::vector<Vertex> vertices;
		float minHeight = 0, maxHeight = 0;
		GLuint vao = 0, vbo = 0, ebo = 0; GLsizei count = 0;
		Terrain() {
			heights.reserve((cells + 1) * (cells + 1));
			std::vector<uint32_t> indices;
			vertices.reserve((cells + 1) * (cells + 1)); indices.reserve(cells * cells * 6);
			for (int z = 0; z <= cells; ++z) for (int x = 0; x <= cells; ++x) {
				float px = -1000 + x * step, pz = -1000 + z * step;
				Vec3 n = normalize({ height(px - step,pz) - height(px + step,pz),2 * step,height(px,pz - step) - height(px,pz + step) });
				vertices.push_back({ {px,height(px,pz),pz},n });
				heights.push_back(vertices.back().position.y);
			}
			for (int z = 0; z < cells; ++z) for (int x = 0; x < cells; ++x) {
				uint32_t a = uint32_t(z * (cells + 1) + x), b = a + 1, c = a + cells + 1, d = c + 1;
				indices.insert(indices.end(), { a,c,b,b,c,d });
			}
			auto bounds = std::minmax_element(heights.begin(), heights.end()); minHeight = *bounds.first; maxHeight = *bounds.second;
			count = GLsizei(indices.size());
			glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo); glGenBuffers(1, &ebo);
			glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(vertices.size() * sizeof(Vertex)), vertices.data(), GL_DYNAMIC_DRAW);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(indices.size() * sizeof(uint32_t)), indices.data(), GL_STATIC_DRAW);
			glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
			glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));
		}
		~Terrain() { glDeleteBuffers(1, &ebo); glDeleteBuffers(1, &vbo); glDeleteVertexArrays(1, &vao); }
		void carveCrater(Vec3 center, float radius) {
			for (size_t i = 0; i < vertices.size(); ++i) {
				Vec3 p = vertices[i].position;
				float r = std::hypot(p.x - center.x, p.z - center.z);
				if (r >= radius * 1.25f) continue;
				// Excavate a spherical cap whose depth is 30% of its rim radius.
				float capDepth = radius * 0.3f;
				float sphereRadius = (radius * radius + capDepth * capDepth) / (2 * capDepth);
				float sphereCenterY = center.y + sphereRadius - capDepth;
				float bowl = r < radius ? sphereCenterY - std::sqrt(std::max(0.0f, sphereRadius * sphereRadius - r * r)) :
					mix(center.y, heights[i], smooth(radius, radius * 1.25f, r));
				heights[i] = std::min(heights[i], bowl);
			}
			for (int z = 0; z <= cells; ++z) for (int x = 0; x <= cells; ++x) {
				int left = std::max(0, x - 1), right = std::min(cells, x + 1), back = std::max(0, z - 1), front = std::min(cells, z + 1);
				size_t i = size_t(z * (cells + 1) + x);
				float dx = (heights[size_t(z * (cells + 1) + right)] - heights[size_t(z * (cells + 1) + left)]) / ((right - left) * step);
				float dz = (heights[size_t(front * (cells + 1) + x)] - heights[size_t(back * (cells + 1) + x)]) / ((front - back) * step);
				vertices[i].position.y = heights[i]; vertices[i].normal = normalize({ -dx,1,-dz });
			}
			auto bounds = std::minmax_element(heights.begin(), heights.end()); minHeight = *bounds.first; maxHeight = *bounds.second;
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(vertices.size() * sizeof(Vertex)), vertices.data());
		}
		bool segmentHit(Vec3 start, Vec3 end, Vec3& hit) const {
			// Test the swept footprint against the mesh, so fast diagonal strikes
			// cannot tunnel through ridges or collide with clamped terrain outside the map.
			float minX = std::min(start.x, end.x), maxX = std::max(start.x, end.x);
			float minZ = std::min(start.z, end.z), maxZ = std::max(start.z, end.z);
			if (maxX < -1000 || minX>1000 || maxZ < -1000 || minZ>1000) return false;
			auto grid = [](float v) {return std::clamp(int(std::floor((v + 1000) / step)), 0, cells - 1); };
			int x0 = grid(minX), x1 = grid(maxX), z0 = grid(minZ), z1 = grid(maxZ);
			Vec3 direction = end - start;
			float closest = 2;
			auto triangle = [&](Vec3 a, Vec3 b, Vec3 c) {
				Vec3 e1 = b - a, e2 = c - a, p = cross(direction, e2);
				float determinant = dot(e1, p);
				if (std::abs(determinant) < 1e-7f) return;
				float inverse = 1 / determinant;
				Vec3 relative = start - a;
				float u = dot(relative, p) * inverse;
				if (u < -0.00001f || u>1.00001f) return;
				Vec3 q = cross(relative, e1);
				float v = dot(direction, q) * inverse;
				if (v < -0.00001f || u + v>1.00001f) return;
				float t = dot(e2, q) * inverse;
				if (t >= 0 && t <= 1) closest = std::min(closest, t);
				};
			for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) {
				size_t a = size_t(z * (cells + 1) + x), b = a + 1, c = a + cells + 1, d = c + 1;
				triangle(vertices[a].position, vertices[c].position, vertices[b].position);
				triangle(vertices[b].position, vertices[c].position, vertices[d].position);
			}
			if (closest > 1) return false;
			hit = start + direction * closest; return true;
		}
		// Sample the actual mesh triangles, not the higher-frequency noise surface.
		float surface(float x, float z, Vec3& normal) const {
			float gx = std::clamp((x + 1000) / step, 0.0f, float(cells));
			float gz = std::clamp((z + 1000) / step, 0.0f, float(cells));
			int ix = std::min(int(gx), cells - 1), iz = std::min(int(gz), cells - 1);
			float u = gx - ix, v = gz - iz;
			size_t a = size_t(iz * (cells + 1) + ix);
			float ha = heights[a], hb = heights[a + 1], hc = heights[a + cells + 1], hd = heights[a + cells + 2];
			float dx, dz, y;
			if (u + v <= 1) { dx = (hb - ha) / step; dz = (hc - ha) / step; y = ha + (hb - ha) * u + (hc - ha) * v; }
			else { dx = (hd - hc) / step; dz = (hd - hb) / step; y = hd + (hc - hd) * (1 - u) + (hb - hd) * (1 - v); }
			normal = normalize({ -dx,1,-dz }); return y;
		}
	};

	struct Volcanoes {
		static constexpr float sourceClearance = 2.0f;
		struct alignas(16) GpuParticle {
			float positionAge[4];
			float velocityLife[4];
			float data[4];
		};
		struct GpuSimulation {
			static constexpr uint32_t capacity = 16000, buckets = 32768;
			GLuint compute = 0, terrainTexture = 0;
			std::array<GLuint, 6> buffers{};
			uint32_t cursor = 0;
			explicit GpuSimulation(const std::filesystem::path& directory, const Terrain& terrain) {
				compute = computeProgram(directory, "particle_compute");
				glGenBuffers(GLsizei(buffers.size()), buffers.data());
				const GLsizeiptr sizes[] = { GLsizeiptr(capacity * sizeof(GpuParticle)),GLsizeiptr(capacity * 4 * sizeof(float)),
					GLsizeiptr(capacity * 4 * sizeof(float)),GLsizeiptr(capacity * sizeof(float)),
					GLsizeiptr(buckets * sizeof(int)),GLsizeiptr(capacity * sizeof(int)) };
				for (size_t i = 0; i < buffers.size(); ++i) {
					glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[i]);
					glBufferData(GL_SHADER_STORAGE_BUFFER, sizes[i], nullptr, GL_DYNAMIC_DRAW);
					glBindBufferBase(GL_SHADER_STORAGE_BUFFER, GLuint(i), buffers[i]);
				}
				std::vector<GpuParticle> inactive(capacity);
				glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[0]);
				glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, GLsizeiptr(inactive.size() * sizeof(GpuParticle)), inactive.data());
				glGenTextures(1, &terrainTexture); uploadTerrain(terrain);
			}
			~GpuSimulation() { glDeleteProgram(compute); glDeleteBuffers(GLsizei(buffers.size()), buffers.data()); glDeleteTextures(1, &terrainTexture); }
			void uploadTerrain(const Terrain& terrain) {
				glBindTexture(GL_TEXTURE_2D, terrainTexture);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, Terrain::cells + 1, Terrain::cells + 1, 0, GL_RED, GL_FLOAT, terrain.heights.data());
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			}
			void spawn(const std::vector<GpuParticle>& records) {
				if (records.empty()) return;
				size_t source = 0;
				while (source < records.size()) {
					size_t count = std::min(records.size() - source, size_t(capacity - cursor));
					glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[0]);
					glBufferSubData(GL_SHADER_STORAGE_BUFFER, GLintptr(cursor * sizeof(GpuParticle)), GLsizeiptr(count * sizeof(GpuParticle)), records.data() + source);
					cursor = (cursor + uint32_t(count)) % capacity; source += count;
				}
				memoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
			}
			void step(float dt, bool interactions, float lifetime) {
				glUseProgram(compute);
				for (size_t i = 0; i < buffers.size(); ++i) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, GLuint(i), buffers[i]);
				glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, terrainTexture);
				glUniform1i(glGetUniformLocation(compute, "terrainHeight"), 0);
				glUniform1ui(glGetUniformLocation(compute, "capacity"), capacity);
				glUniform1f(glGetUniformLocation(compute, "dt"), dt);
				glUniform1f(glGetUniformLocation(compute, "particleLifetime"), lifetime);
				glUniform1i(glGetUniformLocation(compute, "interactions"), interactions ? 1 : 0);
				auto run = [&](int pass, uint32_t count) {
					glUniform1i(glGetUniformLocation(compute, "pass"), pass);
					dispatchCompute((count + 127) / 128, 1, 1); memoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
					};
				run(0, std::max(capacity, buckets)); run(1, capacity);
				// Phase changes need the hash even when fluid interactions are disabled.
				run(0, buckets); run(2, capacity); run(9, capacity);
				for (int iteration = 0; interactions && iteration < 3; ++iteration) {
					run(0, buckets); run(2, capacity); run(3, capacity); run(4, capacity); run(5, capacity);
				}
				if (interactions) { run(0, buckets); run(2, capacity); }
				run(6, capacity); run(7, capacity);
				memoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
			}
		};
		GpuSimulation gpu;
		struct Meteor { Vec3 position, velocity; float trailEmission = 0; };
		std::vector<Meteor> meteors;
		struct Sprite { Vec3 position; float size, opacity; Vec3 emissionColor; };
		std::array<Vec3, 256> blackbodyColors{};
		// Integrate Planck radiance against the Wyman/Sloan/Shirley CIE 1931 fits:
		// https://jcgt.org/published/0002/02/01/ (wavelengths in nm, temperature in K).
		static Vec3 blackbody(float temperature) {
			Vec3 xyz{};
			for (int wavelength = 380; wavelength <= 780; wavelength += 5) {
				double nm = wavelength;
				auto gaussian = [nm](double center, double left, double right) {
					double t = (nm - center) * (nm < center ? left : right); return std::exp(-0.5 * t * t);
					};
				double x = 1.056 * gaussian(599.8, .0264, .0323) + .362 * gaussian(442, .0624, .0374) - .065 * gaussian(501.1, .049, .0382);
				double y = .821 * gaussian(568.8, .0213, .0247) + .286 * gaussian(530.9, .0613, .0322);
				double z = 1.217 * gaussian(437, .0845, .0278) + .681 * gaussian(459, .0385, .0725);
				// A fixed radiance reference preserves cooling-related dimming.
				double radiance = std::pow(560 / nm, 5) * std::expm1(1.438776877e7 / (560 * 1600.0))
					/ std::expm1(1.438776877e7 / (nm * temperature));
				xyz = xyz + Vec3{ float(x),float(y),float(z) }*float(radiance * 5);
			}
			return { std::max(0.0f,3.2406f * xyz.x - 1.5372f * xyz.y - .4986f * xyz.z),
				std::max(0.0f,-.9689f * xyz.x + 1.8758f * xyz.y + .0415f * xyz.z),
				std::max(0.0f,.0557f * xyz.x - .2040f * xyz.y + 1.0570f * xyz.z) };
		}
		Vec3 glow(float temperature) const {
			float index = std::clamp((temperature - 300) / 1500, 0.0f, 1.0f) * 255;
			size_t low = std::min(size_t(index), size_t(254));
			return blackbodyColors[low] * (1 - (index - low)) + blackbodyColors[low + 1] * (index - low);
		}
		std::mt19937 random{ std::random_device{}() };
		std::vector<Vec3> vents;
		std::vector<Vec3> springs;
		std::vector<Sprite> sprites;
		GLuint shader = 0, vao = 0, vbo = 0, blackbodyTexture = 0;
		double accumulator = 0;
		float emission = 0;
		float particleLifetime = 33.0f;
		float particleBrightness = 1.0f;
		bool particleInteractions = true;
		float range(float low, float high) { return std::uniform_real_distribution<float>(low, high)(random); }
		explicit Volcanoes(const std::filesystem::path& directory, const Terrain& terrain) :gpu(directory, terrain) {
			Vec3 reference = blackbody(1600);
			float scale = 1 / std::max({ reference.x,reference.y,reference.z });
			for (size_t i = 0; i < blackbodyColors.size(); ++i) blackbodyColors[i] = blackbody(300 + 1500 * float(i) / 255) * scale;
			sprites.reserve(32);
			glGenTextures(1, &blackbodyTexture); glBindTexture(GL_TEXTURE_1D, blackbodyTexture);
			glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB32F, GLsizei(blackbodyColors.size()), 0, GL_RGB, GL_FLOAT, blackbodyColors.data());
			glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			shader = program(directory, "particles");
			glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
			glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Sprite), nullptr);
			glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Sprite), reinterpret_cast<void*>(offsetof(Sprite, size)));
			glEnableVertexAttribArray(2); glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Sprite), reinterpret_cast<void*>(offsetof(Sprite, emissionColor)));
			glVertexAttribDivisor(0, 1); glVertexAttribDivisor(1, 1); glVertexAttribDivisor(2, 1);
		}
		~Volcanoes() { glDeleteTextures(1, &blackbodyTexture); glDeleteBuffers(1, &vbo); glDeleteVertexArrays(1, &vao); glDeleteProgram(shader); }
		void launchMeteor(Vec3 target) {
			float azimuth = range(0, 2 * pi), tilt = range(15, 55) * pi / 180;
			Vec3 approach{ std::sin(tilt) * std::cos(azimuth),std::cos(tilt),std::sin(tilt) * std::sin(azimuth) };
			meteors.push_back({ target + approach * (700 / approach.y),approach * (-range(650,900)),0 });
		}
		void impact(Terrain& terrain, Vec3 position) {
			terrain.carveCrater(position, 65.0f);
			gpu.uploadTerrain(terrain);
			// Keep existing vents attached to the displaced terrain.
			for (auto& vent : vents) { Vec3 n; vent.y = terrain.surface(vent.x, vent.z, n) + sourceClearance; }
			for (auto& spring : springs) { Vec3 n; spring.y = terrain.surface(spring.x, spring.z, n) + sourceClearance; }
			constexpr size_t ejectaCount = 1200;
			std::vector<GpuParticle> records; records.reserve(ejectaCount);
			for (size_t i = 0; i < ejectaCount; ++i) {
				float angle = range(0, 2 * pi), speed = range(10, 50);
				// Cone surface: exactly 45 degrees from global +Y, independent of slope.
				float horizontalSpeed = speed * std::sin(pi / 4);
				Vec3 velocity{ std::cos(angle) * horizontalSpeed,speed * std::cos(pi / 4),std::sin(angle) * horizontalSpeed };
				float radius = range(0, 18);
				Vec3 p = position + Vec3{ std::cos(angle) * radius,2,std::sin(angle) * radius };
				Vec3 n; p.y = std::max(p.y, terrain.surface(p.x, p.z, n) + 2);
				p = p + velocity;
				float size = range(1.2f, 2.8f), temperature = range(1550, 1800);
				records.push_back({ {p.x,p.y,p.z,0},{velocity.x,velocity.y,velocity.z,0},{size,temperature,7,1} });
			}
			gpu.spawn(records);
		}
		void update(Terrain& terrain, double elapsed) {
			constexpr float dt = 1.0f / 120.0f;
			// The shared clock bounds real elapsed time before applying the speed multiplier.
			accumulator += elapsed;
			while (accumulator >= dt) {
				accumulator -= dt;
				for (size_t i = 0; i < meteors.size();) {
					auto& meteor = meteors[i];
					Vec3 previousPosition = meteor.position;
					Vec3 next = meteor.position + meteor.velocity * dt;
					Vec3 contact;
					bool hit = terrain.segmentHit(previousPosition, next, contact);
					if (hit) next = contact;
					meteor.trailEmission += 600 * dt;
					while (meteor.trailEmission >= 1) {
						meteor.trailEmission -= 1;
						Vec3 p = previousPosition + (next - previousPosition) * range(0, 1);
						Vec3 v{ range(-1.5f,1.5f),range(-0.5f,0.5f),range(-1.5f,1.5f) };
						float life = range(1.5f, 3.0f), size = range(1.5f, 3.5f);
						gpu.spawn({ GpuParticle{{p.x,p.y,p.z,0},{v.x,v.y,v.z,life},{size,1800,8,1}} });
					}
					meteor.position = next;
					if (hit) { impact(terrain, next); meteors.erase(meteors.begin() + i); }
					else if (next.y < terrain.minHeight - 1000) meteors.erase(meteors.begin() + i);
					else ++i;
				}
				emission += 90 * dt;
				std::vector<GpuParticle> spawned;
				while (emission >= 1) {
					emission -= 1;
					for (Vec3 vent : vents) {
						Vec3 velocity{ range(-1.2f,1.2f),range(0.0f,1.5f),range(-1.2f,1.2f) };
						float size = range(1.2f, 2.2f), temperature = range(1450, 1650);
						Vec3 spawn = vent + Vec3{ range(-1.0f,1.0f),0.25f,range(-1.0f,1.0f) };
						Vec3 normal; float ground = terrain.surface(spawn.x, spawn.z, normal);
						float radius = std::max(0.65f, size * 0.52f);
						spawn.y = std::max(spawn.y, ground + radius / std::max(normal.y, 0.25f) + 0.25f);
						spawned.push_back({ {spawn.x,spawn.y,spawn.z,0},{velocity.x,velocity.y,velocity.z,0},{size,temperature,7,1} });
					}
					for (Vec3 spring : springs) {
						Vec3 velocity{ range(-1.2f,1.2f),range(0.0f,1.5f),range(-1.2f,1.2f) };
						float size = range(1.2f, 2.2f);
						Vec3 spawn = spring + Vec3{ range(-1.0f,1.0f),0.25f,range(-1.0f,1.0f) };
						Vec3 normal; float ground = terrain.surface(spawn.x, spawn.z, normal);
						float radius = std::max(0.65f, size * 0.52f);
						spawn.y = std::max(spawn.y, ground + radius / std::max(normal.y, 0.25f) + 0.25f);
						// Gravity, terrain collision, and fluid interaction plus the water tag.
						spawned.push_back({ {spawn.x,spawn.y,spawn.z,0},{velocity.x,velocity.y,velocity.z,0},{size,300,23,1} });
					}
				}
				gpu.spawn(spawned);
				gpu.step(dt, particleInteractions, particleLifetime);
			}
		}
		void prepareDraw(const Mat4& vp, const Mat4& lightVp, GLuint shadowMap, Vec3 right, Vec3 up, Vec3 eye, Vec3 sun, float daylight, float atmosphereOpacity) {
			glUseProgram(shader);
			glUniformMatrix4fv(glGetUniformLocation(shader, "viewProjection"), 1, GL_FALSE, vp.data());
			uniform(shader, "cameraRight", right); uniform(shader, "cameraUp", up); uniform(shader, "eye", eye);
			uniform(shader, "sunDirection", sun); uniform(shader, "daylight", daylight);
			uniform(shader, "atmosphereOpacity", atmosphereOpacity);
			uniform(shader, "particleBrightness", pow(2.f, particleBrightness));
			glUniform1f(glGetUniformLocation(shader, "particleLifetime"), particleLifetime);
			glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_1D, blackbodyTexture);
			glUniform1i(glGetUniformLocation(shader, "blackbodyColors"), 4);
			glUniformMatrix4fv(glGetUniformLocation(shader, "lightViewProjection"), 1, GL_FALSE, lightVp.data());
			glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D_ARRAY, shadowMap);
			glUniform1i(glGetUniformLocation(shader, "terrainShadowMap"), 5);
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, gpu.buffers[0]);
			glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
		}
		void draw(const Mat4& vp, const Mat4& lightVp, GLuint shadowMap, Vec3 right, Vec3 up, Vec3 eye, Vec3 sun, float daylight, float atmosphereOpacity, bool drawVapor) {
			prepareDraw(vp, lightVp, shadowMap, right, up, eye, sun, daylight, atmosphereOpacity);
			glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
			glUniform1i(glGetUniformLocation(shader, "gpuParticles"), 1);
			glUniform1i(glGetUniformLocation(shader, "renderMode"), 0);
			glUniform1i(glGetUniformLocation(shader, "opaquePass"), 1);
			glDrawArraysInstanced(GL_TRIANGLES, 0, 6, GLsizei(GpuSimulation::capacity));

			sprites.clear();
			for (Vec3 vent : vents) sprites.push_back({ vent + Vec3{0,3.0f,0},5,1,glow(1600) });
			for (const auto& meteor : meteors) sprites.push_back({ meteor.position,8,1,glow(1800) * 3 });
			if (!sprites.empty()) {
				glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(sprites.size() * sizeof(Sprite)), sprites.data(), GL_STREAM_DRAW);
				glUniform1i(glGetUniformLocation(shader, "gpuParticles"), 0);
				glDrawArraysInstanced(GL_TRIANGLES, 0, 6, GLsizei(sprites.size()));
			}

			glUniform1i(glGetUniformLocation(shader, "gpuParticles"), 1);
			glUniform1i(glGetUniformLocation(shader, "renderMode"), 2);
			glUniform1i(glGetUniformLocation(shader, "opaquePass"), 0);
			glDepthMask(GL_FALSE);
			glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDrawArraysInstanced(GL_TRIANGLES, 0, 6, GLsizei(GpuSimulation::capacity));

			if (drawVapor) {
				glUniform1i(glGetUniformLocation(shader, "renderMode"), 3);
				glDrawArraysInstanced(GL_TRIANGLES, 0, 6, GLsizei(GpuSimulation::capacity));
			}

			glUniform1i(glGetUniformLocation(shader, "gpuParticles"), 1);
			glUniform1i(glGetUniformLocation(shader, "renderMode"), 1);
			glUniform1i(glGetUniformLocation(shader, "opaquePass"), 0);
			glDepthMask(GL_FALSE);
			glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
			glDrawArraysInstanced(GL_TRIANGLES, 0, 6, GLsizei(GpuSimulation::capacity));
			glDepthMask(GL_TRUE); glDisable(GL_BLEND);
			glActiveTexture(GL_TEXTURE0);
		}
		void drawVapor(const Mat4& vp, const Mat4& lightVp, GLuint shadowMap, Vec3 right, Vec3 up, Vec3 eye, Vec3 sun, float daylight, float atmosphereOpacity) {
			prepareDraw(vp, lightVp, shadowMap, right, up, eye, sun, daylight, atmosphereOpacity);
			glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
			glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glUniform1i(glGetUniformLocation(shader, "gpuParticles"), 1);
			glUniform1i(glGetUniformLocation(shader, "renderMode"), 3);
			glUniform1i(glGetUniformLocation(shader, "opaquePass"), 0);
			glDrawArraysInstanced(GL_TRIANGLES, 0, 6, GLsizei(GpuSimulation::capacity));
			glDepthMask(GL_TRUE); glDisable(GL_BLEND); glActiveTexture(GL_TEXTURE0);
		}
	};

	struct Lightning {
		struct Segment { Vec3 start; float strength; Vec3 end; float width; };
		struct Strike { std::vector<Segment> segments; float age = 0; };
		GLuint shader = 0, vao = 0, vbo = 0;
		std::vector<Strike> strikes;
		std::vector<Segment> visibleSegments;
		std::mt19937 random{ std::random_device{}() };
		double untilNext = -1;
		float frequency = 6.0f;
		float scheduledFrequency = 6.0f;
		explicit Lightning(const std::filesystem::path& directory) {
			shader = program(directory, "lightning");
			glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
			glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Segment), nullptr);
			glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Segment), reinterpret_cast<void*>(offsetof(Segment, end)));
			glVertexAttribDivisor(0, 1); glVertexAttribDivisor(1, 1);
		}
		~Lightning() { glDeleteBuffers(1, &vbo); glDeleteVertexArrays(1, &vao); glDeleteProgram(shader); }
		float range(float low, float high) { return std::uniform_real_distribution<float>(low, high)(random); }
		std::vector<Vec3> path(Vec3 start, Vec3 end, int count, float jitter) {
			std::vector<Vec3> points; points.reserve(size_t(count + 1)); points.push_back(start);
			for (int i = 1; i < count; ++i) {
				float t = float(i) / count, envelope = std::sin(pi * t);
				Vec3 point = start + (end - start) * t;
				point = point + Vec3{ range(-jitter,jitter) * envelope,range(-jitter * 0.3f,jitter * 0.3f) * envelope,
					range(-jitter,jitter) * envelope };
				points.push_back(point);
			}
			points.push_back(end); return points;
		}
		void appendPath(Strike& strike, const std::vector<Vec3>& points, float strength, float width) {
			for (size_t i = 1; i < points.size(); ++i)
				strike.segments.push_back({ points[i - 1],strength * range(0.82f,1.0f),points[i],width });
		}
		void spawn(const Terrain& terrain) {
			Vec3 origin{ range(-820,820),range(370,500),range(-820,820) };
			float targetX = std::clamp(origin.x + range(-180, 180), -980.0f, 980.0f);
			float targetZ = std::clamp(origin.z + range(-180, 180), -980.0f, 980.0f);
			Vec3 normal; Vec3 target{ targetX,terrain.surface(targetX,targetZ,normal) + 1.0f,targetZ };
			Strike strike;
			auto mainPath = path(origin, target, 22, 24.0f);
			appendPath(strike, mainPath, 1.0f, 0.85f);
			int branchCount = int(range(1.0f, 5.0f));
			for (int branch = 0; branch < branchCount; ++branch) {
				int index = int(range(3.0f, float(mainPath.size() - 4)));
				Vec3 start = mainPath[size_t(index)];
				Vec3 end = start + Vec3{ range(-120,120),-range(55,150),range(-120,120) };
				float ground = terrain.surface(std::clamp(end.x, -999.0f, 999.0f), std::clamp(end.z, -999.0f, 999.0f), normal);
				end.y = std::max(end.y, ground + 8.0f);
				appendPath(strike, path(start, end, int(range(5.0f, 9.0f)), 13.0f), 0.55f, 0.48f);
			}
			strikes.push_back(std::move(strike));
		}
		double interval() {
			double rate = std::max(double(frequency) / 60.0, 1e-6);
			return std::exponential_distribution<double>(rate)(random);
		}
		void update(const Terrain& terrain, double elapsed) {
			for (auto& strike : strikes) strike.age += float(elapsed);
			strikes.erase(std::remove_if(strikes.begin(), strikes.end(), [](const Strike& strike) {return strike.age >= 0.4f; }), strikes.end());
			if (frequency != scheduledFrequency) { scheduledFrequency = frequency; untilNext = -1; }
			if (frequency <= 0) { untilNext = -1; return; }
			if (untilNext < 0) untilNext = interval();
			untilNext -= elapsed;
			while (untilNext <= 0) { spawn(terrain); untilNext += interval(); }
		}
		void draw(const Mat4& vp, Vec3 eye, Vec3 cameraRight) {
			visibleSegments.clear();
			for (const auto& strike : strikes) {
				float fade = 1.0f - smooth(0.1f, 0.4f, strike.age);
				for (auto segment : strike.segments) { segment.strength *= fade; visibleSegments.push_back(segment); }
			}
			if (visibleSegments.empty()) return;
			glUseProgram(shader);
			glUniformMatrix4fv(glGetUniformLocation(shader, "viewProjection"), 1, GL_FALSE, vp.data());
			uniform(shader, "eye", eye); uniform(shader, "cameraRight", cameraRight);
			glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(visibleSegments.size() * sizeof(Segment)), visibleSegments.data(), GL_STREAM_DRAW);
			glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
			glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_ONE, GL_ONE);
			glDrawArraysInstanced(GL_TRIANGLES, 0, 6, GLsizei(visibleSegments.size()));
			glDepthMask(GL_TRUE); glDisable(GL_BLEND);
		}
	};

	// Small ImGui renderer keeps even the UI GLSL in external text files.
	struct UiRenderer {
		GLuint shader = 0, vao = 0, vbo = 0, ebo = 0, font = 0;
		explicit UiRenderer(const std::filesystem::path& dir) {
			shader = program(dir, "ui");
			glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo); glGenBuffers(1, &ebo);
			glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
			glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, pos)));
			glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, uv)));
			glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert), reinterpret_cast<void*>(offsetof(ImDrawVert, col)));
			unsigned char* pixels = nullptr; int w = 0, h = 0;
			auto& io = ImGui::GetIO(); io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
			glGenTextures(1, &font); glBindTexture(GL_TEXTURE_2D, font);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
			io.Fonts->SetTexID(ImTextureID(font));
			io.BackendRendererName = "EarthSim_OpenGL33";
			io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
		}
		~UiRenderer() {
			ImGui::GetIO().Fonts->SetTexID(0);
			glDeleteTextures(1, &font); glDeleteBuffers(1, &ebo); glDeleteBuffers(1, &vbo);
			glDeleteVertexArrays(1, &vao); glDeleteProgram(shader);
		}
		void draw(ImDrawData* data, int w, int h) {
			auto setup = [&]() {
				glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD); glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
				glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glEnable(GL_SCISSOR_TEST);
				glViewport(0, 0, w, h); glActiveTexture(GL_TEXTURE0); glUseProgram(shader);
				glUniform1i(glGetUniformLocation(shader, "fontTexture"), 0);
				glUniform2f(glGetUniformLocation(shader, "displayPosition"), data->DisplayPos.x, data->DisplayPos.y);
				glUniform2f(glGetUniformLocation(shader, "displaySize"), data->DisplaySize.x, data->DisplaySize.y);
				glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
				};
			setup();
			for (int i = 0; i < data->CmdListsCount; ++i) {
				const auto* list = data->CmdLists[i];
				glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(list->VtxBuffer.Size * sizeof(ImDrawVert)), list->VtxBuffer.Data, GL_STREAM_DRAW);
				glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(list->IdxBuffer.Size * sizeof(ImDrawIdx)), list->IdxBuffer.Data, GL_STREAM_DRAW);
				for (const auto& cmd : list->CmdBuffer) {
					if (cmd.UserCallback) {
						if (cmd.UserCallback == ImDrawCallback_ResetRenderState) setup(); else cmd.UserCallback(list, &cmd);
						continue;
					}
					float x1 = std::clamp((cmd.ClipRect.x - data->DisplayPos.x) * data->FramebufferScale.x, 0.0f, float(w));
					float y1 = std::clamp((cmd.ClipRect.y - data->DisplayPos.y) * data->FramebufferScale.y, 0.0f, float(h));
					float x2 = std::clamp((cmd.ClipRect.z - data->DisplayPos.x) * data->FramebufferScale.x, 0.0f, float(w));
					float y2 = std::clamp((cmd.ClipRect.w - data->DisplayPos.y) * data->FramebufferScale.y, 0.0f, float(h));
					if (x2 <= x1 || y2 <= y1) continue;
					glScissor(int(x1), int(float(h) - y2), int(x2 - x1), int(y2 - y1));
					glBindTexture(GL_TEXTURE_2D, GLuint(cmd.GetTexID()));
					glDrawElementsBaseVertex(GL_TRIANGLES, GLsizei(cmd.ElemCount), sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
						reinterpret_cast<void*>(size_t(cmd.IdxOffset) * sizeof(ImDrawIdx)), GLint(cmd.VtxOffset));
				}
			}
			glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND);
		}
	};

	struct TerrainShadows {
		static constexpr int resolution = 2048;
		GLuint shader = 0, fbo = 0, depth = 0;
		std::array<Mat4, 2> lightMatrices{};
		explicit TerrainShadows(const std::filesystem::path& directory) {
			shader = program(directory, "terrain_shadow");
			glGenFramebuffers(1, &fbo); glGenTextures(1, &depth);
			glBindTexture(GL_TEXTURE_2D_ARRAY, depth);
			glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24, resolution, resolution, 2, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
			glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
			const float border[] = { 1,1,1,1 }; glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, border);
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depth, 0, 0);
			glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				glDeleteTextures(1, &depth); glDeleteFramebuffers(1, &fbo); glDeleteProgram(shader);
				throw std::runtime_error("Terrain shadow framebuffer is incomplete.");
			}
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}
		~TerrainShadows() { glDeleteTextures(1, &depth); glDeleteFramebuffers(1, &fbo); glDeleteProgram(shader); }
		void render(const Terrain& terrain, Vec3 sun) {
			// Fixed world-space coverage encloses the whole mesh at every sun angle,
			// independent of camera zoom/pan. Two layers handle sunlight and moonlight.
			float centerY = (terrain.minHeight + terrain.maxHeight) * 0.5f;
			float halfHeight = (terrain.maxHeight - terrain.minHeight) * 0.5f;
			float extent = std::max(1500.0f, std::sqrt(2000000.0f + halfHeight * halfHeight) + 50);
			float lightDistance = extent + 700, nearPlane = 100, farPlane = lightDistance + extent + 100;
			const Mat4 projection{ 1 / extent,0,0,0, 0,1 / extent,0,0,
				0,0,-2 / (farPlane - nearPlane),0, 0,0,-(farPlane + nearPlane) / (farPlane - nearPlane),1 };
			glBindFramebuffer(GL_FRAMEBUFFER, fbo); glViewport(0, 0, resolution, resolution);
			glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
			glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDepthFunc(GL_LESS);
			glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1.5f, 2.0f);
			glUseProgram(shader); glBindVertexArray(terrain.vao);
			for (int layer = 0; layer < 2; ++layer) {
				Vec3 direction = sun * (layer == 0 ? 1.0f : -1.0f);
				Vec3 forward = direction * (-1);
				Vec3 reference = std::abs(direction.y) > 0.95f ? Vec3{ 0,0,1 } : Vec3{ 0,1,0 };
				Vec3 right = normalize(cross(forward, reference)), up = cross(right, forward);
				lightMatrices[layer] = multiply(projection, lookAt(Vec3{ 0,centerY,0 } + direction * lightDistance, forward, right, up));
				glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depth, 0, layer);
				glClear(GL_DEPTH_BUFFER_BIT);
				glUniformMatrix4fv(glGetUniformLocation(shader, "lightViewProjection"), 1, GL_FALSE, lightMatrices[layer].data());
				glDrawElements(GL_TRIANGLES, terrain.count, GL_UNSIGNED_INT, nullptr);
			}
			glDisable(GL_POLYGON_OFFSET_FILL); glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}
		void bind(GLuint terrainProgram) const {
			glUniformMatrix4fv(glGetUniformLocation(terrainProgram, "lightViewProjection[0]"), 2, GL_FALSE, lightMatrices[0].data());
			glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D_ARRAY, depth);
			glUniform1i(glGetUniformLocation(terrainProgram, "terrainShadowMap"), 3);
			glActiveTexture(GL_TEXTURE0);
		}
	};

	struct Clouds {
		GLuint sceneFbo = 0, cloudFbo = 0, sceneColor = 0, sceneDepth = 0, sceneEmission = 0, cloudColor = 0, noiseTexture = 0;
		GLuint shader = 0, composite = 0, vao = 0;
		int width = 0, height = 0;
		explicit Clouds(const std::filesystem::path& directory) {
			shader = program(directory, "clouds");
			try { composite = program(directory, "cloud_composite"); }
			catch (...) { glDeleteProgram(shader); throw; }
			glGenVertexArrays(1, &vao);
			glGenFramebuffers(1, &sceneFbo); glGenFramebuffers(1, &cloudFbo);
			glGenTextures(1, &sceneColor); glGenTextures(1, &sceneDepth); glGenTextures(1, &sceneEmission); glGenTextures(1, &cloudColor);
			glGenTextures(1, &noiseTexture); glBindTexture(GL_TEXTURE_3D, noiseTexture);
			constexpr int size = 64;
			std::vector<unsigned char> values(size * size * size);
			std::mt19937 random(79231);
			for (auto& value : values) value = static_cast<unsigned char>(random() & 255);
			glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, size, size, size, 0, GL_RED, GL_UNSIGNED_BYTE, values.data());
			glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
		}
		~Clouds() {
			glDeleteFramebuffers(1, &sceneFbo); glDeleteFramebuffers(1, &cloudFbo);
			GLuint textures[] = { sceneColor,sceneDepth,sceneEmission,cloudColor,noiseTexture }; glDeleteTextures(5, textures);
			glDeleteProgram(shader); glDeleteProgram(composite); glDeleteVertexArrays(1, &vao);
		}
		void beginScene(int w, int h) {
			if (w != width || h != height) {
				width = w; height = h;
				auto allocate = [](GLuint texture, int w, int h, bool depth) {
					glBindTexture(GL_TEXTURE_2D, texture);
					glTexImage2D(GL_TEXTURE_2D, 0, depth ? GL_DEPTH_COMPONENT24 : GL_RGBA16F, w, h, 0,
						depth ? GL_DEPTH_COMPONENT : GL_RGBA, GL_FLOAT, nullptr);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, depth ? GL_NEAREST : GL_LINEAR);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, depth ? GL_NEAREST : GL_LINEAR);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
					glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
					};
				allocate(sceneColor, w, h, false); allocate(sceneDepth, w, h, true); allocate(sceneEmission, w, h, false);
				glBindTexture(GL_TEXTURE_2D, sceneEmission);
				int mipWidth = w, mipHeight = h, level = 0;
				while (mipWidth > 1 || mipHeight > 1) {
					mipWidth = std::max(1, mipWidth / 2); mipHeight = std::max(1, mipHeight / 2); ++level;
					glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA16F, mipWidth, mipHeight, 0, GL_RGBA, GL_FLOAT, nullptr);
				}
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, level);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
				allocate(cloudColor, (w + 1) / 2, (h + 1) / 2, false);
				glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneColor, 0);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, sceneEmission, 0);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, sceneDepth, 0);
				if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) throw std::runtime_error("Cloud scene framebuffer is incomplete.");
				glBindFramebuffer(GL_FRAMEBUFFER, cloudFbo);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, cloudColor, 0);
				if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) throw std::runtime_error("Cloud volume framebuffer is incomplete.");
			}
			glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
			glDrawBuffer(GL_COLOR_ATTACHMENT0);
		}
		void beginEmission() {
			const GLenum buffers[] = { GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1 };
			glDrawBuffers(2, buffers);
			const float black[] = { 0,0,0,0 }; glClearBufferfv(GL_COLOR, 1, black);
		}
		void endEmission() {
			glDrawBuffer(GL_COLOR_ATTACHMENT0);
		}
		void draw(Vec3 eye, Vec3 forward, Vec3 right, Vec3 up, Vec3 sun, Vec3 fog, float daylight, float opacity, float time, float distance, GLuint destination) {
			glBindFramebuffer(GL_FRAMEBUFFER, cloudFbo); glViewport(0, 0, (width + 1) / 2, (height + 1) / 2);
			glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
			glUseProgram(shader); glBindVertexArray(vao);
			glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, sceneDepth);
			glUniform1i(glGetUniformLocation(shader, "sceneDepth"), 0);
			glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_3D, noiseTexture);
			glUniform1i(glGetUniformLocation(shader, "noiseTexture"), 1);
			uniform(shader, "eye", eye); uniform(shader, "cameraForward", forward); uniform(shader, "cameraRight", right); uniform(shader, "cameraUp", up);
			uniform(shader, "sunDirection", sun); uniform(shader, "fogColor", fog); uniform(shader, "daylight", daylight);
			uniform(shader, "atmosphereOpacity", opacity); uniform(shader, "time", time);
			uniform(shader, "aspect", float(width) / height); uniform(shader, "tanHalfFov", std::tan(pi / 8));
			Mat4 projection = perspective(float(width) / height, distance);
			glUniform2f(glGetUniformLocation(shader, "depthProjection"), projection[10], projection[14]);
			glDrawArrays(GL_TRIANGLES, 0, 3);
			glBindFramebuffer(GL_FRAMEBUFFER, destination); glViewport(0, 0, width, height);
			glUseProgram(composite);
			glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, sceneColor);
			glUniform1i(glGetUniformLocation(composite, "sceneColor"), 0);
			glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, sceneDepth);
			glUniform1i(glGetUniformLocation(composite, "sceneDepth"), 1);
			glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, cloudColor);
			glUniform1i(glGetUniformLocation(composite, "cloudColor"), 2);
			glUniform2f(glGetUniformLocation(composite, "depthProjection"), projection[10], projection[14]);
			glDrawArrays(GL_TRIANGLES, 0, 3);
			glDepthMask(GL_TRUE); glActiveTexture(GL_TEXTURE0);
		}
	};

	struct ScreenSpaceGI {
		GLuint shader = 0, composite = 0, fbo = 0, texture = 0, vao = 0;
		int width = 0, height = 0;
		explicit ScreenSpaceGI(const std::filesystem::path& directory) {
			shader = program(directory, "ssgi");
			try { composite = program(directory, "ssgi_composite"); }
			catch (...) { glDeleteProgram(shader); throw; }
			glGenFramebuffers(1, &fbo); glGenTextures(1, &texture); glGenVertexArrays(1, &vao);
		}
		~ScreenSpaceGI() {
			glDeleteProgram(shader); glDeleteProgram(composite);
			glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &texture); glDeleteVertexArrays(1, &vao);
		}
		void resize(int w, int h) {
			int nextWidth = std::max(1, (w + 1) / 2), nextHeight = std::max(1, (h + 1) / 2);
			if (nextWidth == width && nextHeight == height) return;
			width = nextWidth; height = nextHeight;
			glBindTexture(GL_TEXTURE_2D, texture);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) throw std::runtime_error("SSGI framebuffer is incomplete.");
		}
		void draw(GLuint sceneFbo, GLuint depth, GLuint emission, int fullWidth, int fullHeight,
			Vec3 eye, Vec3 forward, Vec3 right, Vec3 up, float distance) {
			resize(fullWidth, fullHeight);
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			glBindTexture(GL_TEXTURE_2D, emission); glGenerateMipmap(GL_TEXTURE_2D);
			glViewport(0, 0, width, height);
			glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
			glBindVertexArray(vao); glUseProgram(shader);
			glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, depth);
			glUniform1i(glGetUniformLocation(shader, "sceneDepth"), 0);
			glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, emission);
			glUniform1i(glGetUniformLocation(shader, "sceneEmission"), 1);
			uniform(shader, "eye", eye); uniform(shader, "cameraForward", forward);
			uniform(shader, "cameraRight", right); uniform(shader, "cameraUp", up);
			uniform(shader, "aspect", float(fullWidth) / fullHeight); uniform(shader, "tanHalfFov", std::tan(pi / 8));
			Mat4 projection = perspective(float(fullWidth) / fullHeight, distance);
			glUniform2f(glGetUniformLocation(shader, "depthProjection"), projection[10], projection[14]);
			glDrawArrays(GL_TRIANGLES, 0, 3);
			glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo); glViewport(0, 0, fullWidth, fullHeight);
			glUseProgram(composite);
			glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texture);
			glUniform1i(glGetUniformLocation(composite, "indirectLight"), 0);
			glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, depth);
			glUniform1i(glGetUniformLocation(composite, "sceneDepth"), 1);
			glUniform2f(glGetUniformLocation(composite, "depthProjection"), projection[10], projection[14]);
			glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_ONE, GL_ONE);
			glDrawArrays(GL_TRIANGLES, 0, 3);
			glDisable(GL_BLEND); glDepthMask(GL_TRUE); glActiveTexture(GL_TEXTURE0);
		}
	};

	struct Bloom {
		GLuint downsample = 0, upsample = 0, composite = 0, vao = 0;
		GLuint hdrFbo = 0, hdrColor = 0;
		static constexpr int maxMips = 12;
		std::array<GLuint, maxMips> fbos{}, colors{};
		std::array<int, maxMips> mipWidths{}, mipHeights{};
		int mipCount = 0;
		int width = 0, height = 0;
		explicit Bloom(const std::filesystem::path& directory) {
			try {
				downsample = program(directory, "bloom_downsample");
				upsample = program(directory, "bloom_upsample");
				composite = program(directory, "bloom_composite");
			}
			catch (...) { glDeleteProgram(downsample); glDeleteProgram(upsample); glDeleteProgram(composite); throw; }
			glGenVertexArrays(1, &vao); glGenFramebuffers(1, &hdrFbo); glGenTextures(1, &hdrColor);
			glGenFramebuffers(maxMips, fbos.data()); glGenTextures(maxMips, colors.data());
		}
		~Bloom() {
			glDeleteProgram(downsample); glDeleteProgram(upsample); glDeleteProgram(composite);
			glDeleteVertexArrays(1, &vao); glDeleteFramebuffers(1, &hdrFbo); glDeleteTextures(1, &hdrColor);
			glDeleteFramebuffers(maxMips, fbos.data()); glDeleteTextures(maxMips, colors.data());
		}
		void resize(int w, int h) {
			if (w == width && h == height) return;
			auto allocate = [](GLuint fbo, GLuint texture, int w, int h) {
				glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texture);
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glBindFramebuffer(GL_FRAMEBUFFER, fbo);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
				if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) throw std::runtime_error("Bloom framebuffer is incomplete.");
				};
			allocate(hdrFbo, hdrColor, w, h);
			int mw = w, mh = h; mipCount = 0;
			for (int i = 0; i < maxMips; ++i) {
				mw = std::max(1, mw / 2); mh = std::max(1, mh / 2);
				mipWidths[i] = mw; mipHeights[i] = mh;
				allocate(fbos[i], colors[i], mw, mh); ++mipCount;
				if (mw < 4 || mh < 4) break;
			}
			width = w; height = h; glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}
		void draw(GLuint scene, bool enabled, float exposure, float bloomIntensity) {
			glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
			glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST); glDisable(GL_FRAMEBUFFER_SRGB);
			glBindVertexArray(vao); glActiveTexture(GL_TEXTURE0);
			glUseProgram(downsample); glUniform1i(glGetUniformLocation(downsample, "source"), 0);
			// Threshold-free HDR pyramid, progressively downsampled with a 13-tap filter.
			for (int i = 0; enabled && i < mipCount; ++i) {
				glBindFramebuffer(GL_FRAMEBUFFER, fbos[i]); glViewport(0, 0, mipWidths[i], mipHeights[i]);
				glBindTexture(GL_TEXTURE_2D, i == 0 ? scene : colors[i - 1]);
				glDrawArrays(GL_TRIANGLES, 0, 3);
			}
			// Add each smaller mip back into its parent through a 3x3 tent filter.
			glUseProgram(upsample); glUniform1i(glGetUniformLocation(upsample, "source"), 0);
			glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_ONE, GL_ONE);
			for (int i = enabled ? mipCount - 1 : 0; i > 0; --i) {
				glBindFramebuffer(GL_FRAMEBUFFER, fbos[i - 1]); glViewport(0, 0, mipWidths[i - 1], mipHeights[i - 1]);
				glBindTexture(GL_TEXTURE_2D, colors[i]);
				glDrawArrays(GL_TRIANGLES, 0, 3);
			}
			glDisable(GL_BLEND);
			glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, width, height); glUseProgram(composite);
			glBindTexture(GL_TEXTURE_2D, scene); glUniform1i(glGetUniformLocation(composite, "scene"), 0);
			glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, colors[0]);
			glUniform1i(glGetUniformLocation(composite, "bloom"), 1);
			uniform(composite, "bloomStrength", enabled ? 0.04f * bloomIntensity : 0.0f);
			uniform(composite, "exposure", exposure);
			glDrawArrays(GL_TRIANGLES, 0, 3);
			glActiveTexture(GL_TEXTURE0); glDepthMask(GL_TRUE);
		}
	};

	void run(GLFWwindow* window, const std::filesystem::path& directory) {
		struct Programs {
			GLuint terrain = 0, sky = 0, water = 0, skyVao = 0, waterVao = 0;
			~Programs() {
				glDeleteProgram(terrain); glDeleteProgram(sky); glDeleteProgram(water);
				glDeleteVertexArrays(1, &skyVao); glDeleteVertexArrays(1, &waterVao);
			}
		} programs;
		programs.terrain = program(directory, "terrain"); programs.sky = program(directory, "sky"); programs.water = program(directory, "water");
		glGenVertexArrays(1, &programs.skyVao); glGenVertexArrays(1, &programs.waterVao);
		std::random_device seedSource;
		terrainSeed = uint32_t(seedSource()) ^ (uint32_t(seedSource()) << 16);
		generateMountainRanges(terrainSeed);
		Terrain terrain;
		Volcanoes volcanoes(directory, terrain);
		Lightning lightning(directory);
		UiRenderer ui(directory);
		Clouds clouds(directory);
		TerrainShadows shadows(directory);
		ScreenSpaceGI screenSpaceGI(directory);
		Bloom bloom(directory);
		float yaw = 0.65f, pitch = 0.48f, distance = 1050;
		float atmosphereOpacity = 0.4f;
		float waterLevel = 0.0f;
		float cameraExposure = 0.0f;
		float bloomIntensity = 1.0f;
		bool cloudsEnabled = true;
		bool bloomEnabled = true;
		bool ssgiEnabled = false;
		float timeSpeed = 1.0f;
		Vec3 target{ 0,50,0 };
		bool panning = false, rotating = false;
		enum class PlacementTool { None, Volcano, Spring, Meteor };
		PlacementTool placement = PlacementTool::None;
		bool placementMiss = false;
		double previous = glfwGetTime();
		double simulationTime = 0.0;
		while (!glfwWindowShouldClose(window)) {
			glfwPollEvents();
			double now = glfwGetTime();
			// Bound stall recovery consistently for the sky, clouds, and particle physics.
			double elapsed = std::clamp(now - previous, 0.0, 0.1) * double(timeSpeed);
			previous = now;
			simulationTime += elapsed;
			volcanoes.update(terrain, elapsed);
			lightning.update(terrain, elapsed);
			int w = 0, h = 0; glfwGetFramebufferSize(window, &w, &h);
			if (w <= 0 || h <= 0) { glfwWaitEventsTimeout(0.05); continue; }
			ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
			auto& io = ImGui::GetIO();
			if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
				if (placement != PlacementTool::None) { placement = PlacementTool::None; placementMiss = false; }
				else glfwSetWindowShouldClose(window, GLFW_TRUE);
			}
			bool placeClick = placement != PlacementTool::None && !io.WantCaptureMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
			bool targetClick = placement == PlacementTool::None && !io.WantCaptureMouse && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
			if (placement != PlacementTool::None && !io.WantCaptureMouse) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
			if (!io.WantCaptureMouse) {
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && placement == PlacementTool::None && !targetClick) panning = true;
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) rotating = true;
			}
			if (targetClick) panning = false;
			bool focused = glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0;
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || !focused) panning = false;
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) || !focused) rotating = false;
			if (rotating) {
				yaw = std::remainder(yaw - io.MouseDelta.x * 0.005f, 2 * pi);
				pitch = std::remainder(pitch + io.MouseDelta.y * 0.005f, 2 * pi);
			}
			if (!io.WantCaptureMouse) {
				float zoomed = distance * std::exp(-io.MouseWheel * 0.12f);
				// Reject only floating-point overflow/underflow, with no distance limits.
				if (std::isfinite(zoomed) && zoomed > 0) distance = zoomed;
			}
			Vec3 orbit{ std::cos(pitch) * std::sin(yaw),std::sin(pitch),std::cos(pitch) * std::cos(yaw) };
			// An analytic basis stays stable when orbiting through either pole.
			Vec3 forward = orbit * (-1), right{ std::cos(yaw),0,-std::sin(yaw) }, up = cross(right, forward);
			if (panning) {
				// Match screen-space dragging at the orbit target, including on HiDPI displays.
				float unitsPerPixel = 2 * distance * std::tan(pi / 8) / std::max(io.DisplaySize.y, 1.0f);
				target = target + right * (-io.MouseDelta.x * unitsPerPixel) + up * (io.MouseDelta.y * unitsPerPixel);
			}
			Vec3 eye = target + orbit * distance;
			float day = float(std::fmod(simulationTime / 60.0 + 0.34, 1.0));
			float angle = 2 * pi * (day - 0.25f);
			Vec3 sun = normalize({ std::cos(angle),std::sin(angle),0.30f * std::cos(angle) });
			float daylight = smooth(-0.15f, 0.22f, sun.y);
			Vec3 fog = Vec3{ 0.012f,0.019f,0.040f }*(1 - daylight) + Vec3{ 0.42f,0.59f,0.72f }*daylight;
			float sunset = std::exp(-std::abs(sun.y) * 10) * 0.32f;
			fog = fog * (1 - sunset) + Vec3{ 0.70f,0.23f,0.09f }*sunset;

			shadows.render(terrain, sun);
			bloom.resize(w, h);
			// HDR scene/depth are required even when the optional cloud pass is disabled.
			clouds.beginScene(w, h);
			glViewport(0, 0, w, h); glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND);
			glDepthMask(GL_TRUE); glClear(GL_DEPTH_BUFFER_BIT);
			glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
			glUseProgram(programs.sky);
			uniform(programs.sky, "cameraForward", forward); uniform(programs.sky, "cameraRight", right); uniform(programs.sky, "cameraUp", up);
			uniform(programs.sky, "aspect", float(w) / float(h)); uniform(programs.sky, "tanHalfFov", std::tan(pi / 8));
			uniform(programs.sky, "sunDirection", sun); uniform(programs.sky, "daylight", daylight); uniform(programs.sky, "fogColor", fog);
			uniform(programs.sky, "atmosphereOpacity", atmosphereOpacity);
			glBindVertexArray(programs.skyVao); glDrawArrays(GL_TRIANGLES, 0, 3);
			glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE); glCullFace(GL_BACK);
			glUseProgram(programs.terrain);
			Mat4 projection = perspective(float(w) / float(h), distance);
			Mat4 vp = multiply(projection, lookAt(eye, forward, right, up));
			glUniformMatrix4fv(glGetUniformLocation(programs.terrain, "viewProjection"), 1, GL_FALSE, vp.data());
			uniform(programs.terrain, "eye", eye); uniform(programs.terrain, "sunDirection", sun);
			uniform(programs.terrain, "daylight", daylight); uniform(programs.terrain, "fogColor", fog);
			uniform(programs.terrain, "atmosphereOpacity", atmosphereOpacity);
			shadows.bind(programs.terrain);
			glBindVertexArray(terrain.vao); glDrawElements(GL_TRIANGLES, terrain.count, GL_UNSIGNED_INT, nullptr);
			if (placeClick || targetClick) {
				// Read only for a surface action, before particles/clouds/UI are drawn.
				// The scene depth selects the visible triangle, including mountain occlusion.
				int px = int(std::floor(io.MousePos.x * float(w) / io.DisplaySize.x));
				int py = h - 1 - int(std::floor(io.MousePos.y * float(h) / io.DisplaySize.y));
				if (placeClick) placementMiss = true;
				if (px >= 0 && px < w && py >= 0 && py < h) {
					float depth = 1;
					glReadPixels(px, py, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
					if (depth < 1) {
						float sx = 2 * (float(px) + 0.5f) / w - 1, sy = 2 * (float(py) + 0.5f) / h - 1;
						Vec3 ray = forward + right * (sx * float(w) / h * std::tan(pi / 8)) + up * (sy * std::tan(pi / 8));
						float viewDistance = projection[14] / (2 * depth - 1 + projection[10]);
						Vec3 position = eye + ray * viewDistance;
						position.x = std::clamp(position.x, -1000.0f, 1000.0f);
						position.z = std::clamp(position.z, -1000.0f, 1000.0f);
						Vec3 normal;
						position.y = terrain.surface(position.x, position.z, normal);
						if (targetClick) target = position;
						else {
							if (placement == PlacementTool::Volcano) {
								position.y += Volcanoes::sourceClearance; volcanoes.vents.push_back(position);
							}
							else if (placement == PlacementTool::Spring) {
								position.y += Volcanoes::sourceClearance; volcanoes.springs.push_back(position);
							}
							else volcanoes.launchMeteor(position);
							placement = PlacementTool::None; placementMiss = false;
						}
					}
				}
			}
			glUseProgram(programs.water);
			glUniformMatrix4fv(glGetUniformLocation(programs.water, "viewProjection"), 1, GL_FALSE, vp.data());
			uniform(programs.water, "eye", eye); uniform(programs.water, "sunDirection", sun);
			uniform(programs.water, "daylight", daylight); uniform(programs.water, "atmosphereOpacity", atmosphereOpacity);
			uniform(programs.water, "waterLevel", waterLevel);
			glBindVertexArray(programs.waterVao);
			glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
			glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDrawArrays(GL_TRIANGLES, 0, 6);
			glDepthMask(GL_TRUE); glDisable(GL_BLEND);
			constexpr float cloudBase = 290.0f;
			bool vaporAfterClouds = cloudsEnabled && eye.y < cloudBase;
			if (ssgiEnabled) clouds.beginEmission();
			volcanoes.draw(vp, shadows.lightMatrices[0], shadows.depth, right, up, eye, sun, daylight, atmosphereOpacity, !vaporAfterClouds);
			if (ssgiEnabled) {
				clouds.endEmission();
				screenSpaceGI.draw(clouds.sceneFbo, clouds.sceneDepth, clouds.sceneEmission, w, h, eye, forward, right, up, distance);
			}
			if (cloudsEnabled) {
				clouds.draw(eye, forward, right, up, sun, fog, daylight, atmosphereOpacity, float(simulationTime), distance, bloom.hdrFbo);
				glBindFramebuffer(GL_FRAMEBUFFER, bloom.hdrFbo);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, clouds.sceneDepth, 0);
				glDrawBuffer(GL_COLOR_ATTACHMENT0); glViewport(0, 0, w, h);
				if (vaporAfterClouds)
					volcanoes.drawVapor(vp, shadows.lightMatrices[0], shadows.depth, right, up, eye, sun, daylight, atmosphereOpacity);
				lightning.draw(vp, eye, right);
				glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
			}
			else lightning.draw(vp, eye, right);
			bloom.draw(cloudsEnabled ? bloom.hdrColor : clouds.sceneColor, bloomEnabled,
				std::pow(2.0f, cameraExposure), bloomIntensity);

			ImGui::SetNextWindowPos(ImVec2(20 * uiScale, 20 * uiScale), ImGuiCond_Always);
			ImGui::SetNextWindowBgAlpha(0.78f);
			ImGui::Begin("EarthSim", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
			ImGui::TextColored(ImVec4(0.65f, 0.86f, 0.76f, 1), "E A R T H S I M");
			ImGui::TextUnformatted("Procedural mountain range"); ImGui::Spacing();
			int minutes = int(day * 1440);
			ImGui::Text("%02d:%02d  /  %s", minutes / 60, minutes % 60, sun.y > 0.15f ? "Daylight" : sun.y < -0.15f ? "Night" : "Twilight");
			ImGui::SetNextItemWidth(180 * uiScale);
			ImGui::SliderFloat("Time speed", &timeSpeed, 0.0f, 4.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
			if (timeSpeed > 0) ImGui::TextDisabled("1 day = %.1f real seconds", 60.0f / timeSpeed);
			else ImGui::TextDisabled("Simulation paused");
			ImGui::SetNextItemWidth(180 * uiScale);
			ImGui::SliderFloat("Camera exposure", &cameraExposure, -5.0f, 5.0f, "%+.1f EV", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SetNextItemWidth(180 * uiScale);
			ImGui::SliderFloat("Bloom intensity", &bloomIntensity, 0.0f, 2.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
			ImGui::Text("Active volcanoes: %d", int(volcanoes.vents.size()));
			ImGui::Text("Active springs: %d", int(volcanoes.springs.size()));
			if (ImGui::Button("Create volcano")) {
				placement = PlacementTool::Volcano;
				placementMiss = false; panning = false;
			}
			ImGui::SameLine();
			if (ImGui::Button("Create spring")) {
				placement = PlacementTool::Spring;
				placementMiss = false; panning = false;
			}
			ImGui::SameLine();
			if (ImGui::Button("Meteor strike")) {
				placement = PlacementTool::Meteor;
				placementMiss = false; panning = false;
			}
			if (placement != PlacementTool::None) {
				const char* placementPrompt = placement == PlacementTool::Volcano ? "Click terrain to place a volcano." :
					placement == PlacementTool::Spring ? "Click terrain to place a spring." : "Click terrain to target a meteor.";
				ImGui::TextUnformatted(placementPrompt);
				ImGui::TextDisabled("Esc cancels placement.");
				if (ImGui::Button("Cancel placement")) { placement = PlacementTool::None; placementMiss = false; }
				if (placementMiss) ImGui::TextColored(ImVec4(1, 0.65f, 0.3f, 1), "No terrain here. Click the landscape.");
			}
			ImGui::SetNextItemWidth(180 * uiScale);
			ImGui::SliderFloat("Particle life", &volcanoes.particleLifetime, 1.0f, 120.0f, "%.1f s", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SetNextItemWidth(180 * uiScale);
			ImGui::SliderFloat("Particle brightness", &volcanoes.particleBrightness, 0.0f, 100.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
			ImGui::Checkbox("Particle interactions", &volcanoes.particleInteractions);
			ImGui::SetNextItemWidth(180 * uiScale);
			ImGui::SliderFloat("Atmosphere opacity", &atmosphereOpacity, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SetNextItemWidth(180 * uiScale);
			ImGui::SliderFloat("Water level", &waterLevel, -100.0f, 250.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SetNextItemWidth(180 * uiScale);
			ImGui::DragFloat("Lightning frequency", &lightning.frequency, 1.f, 0.f, 1.e6f, "%.1f / min");
			ImGui::Checkbox("Clouds", &cloudsEnabled);
			ImGui::Checkbox("Bloom", &bloomEnabled);
			ImGui::Checkbox("SSGI", &ssgiEnabled);
			ImGui::Separator();
			ImGui::TextUnformatted("Drag left mouse to pan\nDouble-click terrain to focus\nDrag right mouse to rotate\nScroll to zoom\nEsc to exit");
			ImGui::End(); ImGui::Render(); ui.draw(ImGui::GetDrawData(), w, h);
			glfwSwapBuffers(window);
		}
	}
} // namespace

int main(int argc, char** argv) {
	(void)argc;
	glfwSetErrorCallback([](int code, const char* description) {std::cerr << "GLFW " << code << ": " << description << '\n'; });
	if (!glfwInit()) return 1;
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
	glfwWindowHint(GLFW_SAMPLES, 4);
	GLFWwindow* window = glfwCreateWindow(1440, 900, "EarthSim", nullptr, nullptr);
	if (!window) { glfwTerminate(); return 1; }
	glfwMakeContextCurrent(window); glfwSwapInterval(1);
	if (!gladLoadGL(glfwGetProcAddress) || !GLAD_GL_VERSION_3_3) {
		std::cerr << "EarthSim requires OpenGL 4.3.\n"; glfwDestroyWindow(window); glfwTerminate(); return 1;
	}
	dispatchCompute = reinterpret_cast<DispatchComputeProc>(glfwGetProcAddress("glDispatchCompute"));
	memoryBarrier = reinterpret_cast<MemoryBarrierProc>(glfwGetProcAddress("glMemoryBarrier"));
	if (!dispatchCompute || !memoryBarrier) {
		std::cerr << "EarthSim requires OpenGL 4.3 compute shaders.\n"; glfwDestroyWindow(window); glfwTerminate(); return 1;
	}
	IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGui::GetIO().IniFilename = nullptr;
	ImGui::StyleColorsDark();
	auto& style = ImGui::GetStyle(); style.WindowRounding = 10; style.WindowPadding = ImVec2(18, 16); style.ItemSpacing = ImVec2(8, 8);
	style.ScaleAllSizes(uiScale);
	ImFontConfig fontConfig;
	fontConfig.SizePixels = 13.0f * uiScale;
	ImGui::GetIO().Fonts->AddFontDefault(&fontConfig);
	if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
		ImGui::DestroyContext(); glfwDestroyWindow(window); glfwTerminate(); return 1;
	}
	int result = 0;
	try { run(window, shaderDirectory(argv[0])); }
	catch (const std::exception& error) { std::cerr << "EarthSim: " << error.what() << '\n'; result = 1; }
	ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
	glfwDestroyWindow(window); glfwTerminate(); return result;
}
