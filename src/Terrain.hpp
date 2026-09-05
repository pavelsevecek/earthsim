#pragma once

#include "Common.hpp"

namespace earth_sim {
extern uint32_t terrain_seed;
void generate_mountain_ranges(uint32_t seed);
float height(float x, float z);
uint32_t random_terrain_seed();

class Terrain {
    struct Vertex {
        Vec3 position;
        Vec3 normal;
    };
    static constexpr int cells_ = 512;
    static constexpr float step_ = 2000.0f / cells_;
    std::vector<float> heights_;
    std::vector<Vertex> vertices_;
    float min_height_ = 0;
    float max_height_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    GLsizei count_ = 0;

public:
    explicit Terrain(uint32_t seed);
    ~Terrain();
    void update_geometry();
    void deform(Vec3 center, float radius, float elevation);
    void carve_crater(Vec3 center, float radius);
    bool segment_hit(Vec3 start, Vec3 end, Vec3& hit) const;
    void draw() const;
    // Sample the actual mesh triangles, not the higher-frequency noise surface.
    float surface(float x, float z, Vec3& normal) const;

    static constexpr int cell_count() {
        return cells_;
    }
    const std::vector<float>& height_data() const {
        return heights_;
    }
    float min_height() const {
        return min_height_;
    }
    float max_height() const {
        return max_height_;
    }
};

} // namespace earth_sim
