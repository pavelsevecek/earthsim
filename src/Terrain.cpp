#include "Terrain.hpp"

namespace earth_sim {
uint32_t terrain_seed = 0;
struct MountainRange {
    float center_x;
    float center_z;
    float angle;
    float length;
    float width;
    float bend;
    float wave;
    float phase;
    float amplitude;
};
std::vector<MountainRange> mountain_ranges;
void generate_mountain_ranges(uint32_t seed) {
    std::mt19937 random(seed ^ 0xa511e9b3u);
    auto value = [&](float low, float high) {
        return std::uniform_real_distribution<float>(low, high)(random);
    };
    int count = std::uniform_int_distribution<int>(3, 5)(random);
    mountain_ranges.clear();
    mountain_ranges.reserve(size_t(count));
    for (int i = 0; i < count; ++i)
        mountain_ranges.push_back({ value(-380, 380),
            value(-380, 380),
            value(0, pi),
            value(430, 780),
            value(105, 205),
            value(45, 125),
            value(0.004f, 0.009f),
            value(0, 2 * pi),
            value(0.72f, 1.0f) });
}
// Seeded, portable integer hash and quintic gradient noise; no external noise library.
uint32_t hash(int x, int z) {
    uint32_t h = uint32_t(x) * 0x8da6b343u ^ uint32_t(z) * 0xd8163841u ^
                 terrain_seed * 0x9e3779b9u ^ 0xcb1ab31fu;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    return h ^ (h >> 16);
}
float gradient(int x, int z, float dx, float dz) {
    constexpr float d = 0.70710678f;
    constexpr float gradients[8][2] = {
        { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { d, d }, { -d, d }, { d, -d }, { -d, -d }
    };
    const auto& g = gradients[hash(x, z) & 7u];
    return g[0] * dx + g[1] * dz;
}
float noise(float x, float z) {
    int ix = int(std::floor(x));
    int iz = int(std::floor(z));
    float dx = x - float(ix);
    float dz = z - float(iz);
    auto fade = [](float t) { return t * t * t * (t * (t * 6 - 15) + 10); };
    return mix(mix(gradient(ix, iz, dx, dz), gradient(ix + 1, iz, dx - 1, dz), fade(dx)),
        mix(gradient(ix, iz + 1, dx, dz - 1), gradient(ix + 1, iz + 1, dx - 1, dz - 1), fade(dx)),
        fade(dz));
}
float height(float x, float z) {
    float wx = x + 65 * noise(x * 0.002f + 13, z * 0.002f + 7);
    float wz = z + 65 * noise(x * 0.002f - 23, z * 0.002f + 41);
    float ridge = 0;
    float amplitude = 1;
    float frequency = 0.004f;
    float weight = 1;
    for (int i = 0; i < 7; ++i) {
        float n = 1 - std::min(1.0f,
                          std::abs(noise(wx * frequency + 17.3f, wz * frequency + 8.1f)) * 1.65f);
        n = n * n * weight;
        ridge += n * amplitude;
        weight = std::clamp(n * 1.8f, 0.0f, 1.0f);
        frequency *= 2.07f;
        amplitude *= 0.48f;
    }
    float envelope = 0;
    for (const auto& range : mountain_ranges) {
        float dx = wx - range.center_x;
        float dz = wz - range.center_z;
        float along = dx * std::cos(range.angle) + dz * std::sin(range.angle);
        float across = -dx * std::sin(range.angle) + dz * std::cos(range.angle);
        float spine = across - range.bend * std::sin(along * range.wave + range.phase);
        float shaped = range.amplitude * std::exp(-spine * spine / (range.width * range.width) -
                                                  along * along / (range.length * range.length));
        envelope = std::max(envelope, shaped);
    }
    float edge = 1 - smooth(760, 1000, std::max(std::abs(x), std::abs(z)));
    return 0.5f * edge * (8 + 175 * ridge * envelope + 12 * noise(x * 0.01f, z * 0.01f));
}


uint32_t random_terrain_seed() {
    std::random_device seed_source;
    return uint32_t(seed_source()) ^ (uint32_t(seed_source()) << 16);
}


Terrain::Terrain(uint32_t seed) {
    terrain_seed = seed;
    generate_mountain_ranges(terrain_seed);
    heights_.reserve((cells_ + 1) * (cells_ + 1));
    std::vector<uint32_t> indices;
    vertices_.reserve((cells_ + 1) * (cells_ + 1));
    indices.reserve(cells_ * cells_ * 6);
    for (int z = 0; z <= cells_; ++z)
        for (int x = 0; x <= cells_; ++x) {
            float px = -1000 + x * step_;
            float pz = -1000 + z * step_;
            Vec3 n = normalize({ height(px - step_, pz) - height(px + step_, pz),
                2 * step_,
                height(px, pz - step_) - height(px, pz + step_) });
            vertices_.push_back({ { px, height(px, pz), pz }, n });
            heights_.push_back(vertices_.back().position.y);
        }
    for (int z = 0; z < cells_; ++z)
        for (int x = 0; x < cells_; ++x) {
            uint32_t a = uint32_t(z * (cells_ + 1) + x);
            uint32_t b = a + 1;
            uint32_t c = a + cells_ + 1;
            uint32_t d = c + 1;
            indices.insert(indices.end(), { a, c, b, b, c, d });
        }
    auto bounds = std::minmax_element(heights_.begin(), heights_.end());
    min_height_ = *bounds.first;
    max_height_ = *bounds.second;
    count_ = GLsizei(indices.size());
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
        GLsizeiptr(vertices_.size() * sizeof(Vertex)),
        vertices_.data(),
        GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        GLsizeiptr(indices.size() * sizeof(uint32_t)),
        indices.data(),
        GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Vertex),
        reinterpret_cast<void*>(offsetof(Vertex, normal)));
}

Terrain::~Terrain() {
    glDeleteBuffers(1, &ebo_);
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
}

void Terrain::draw() const {
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, count_, GL_UNSIGNED_INT, nullptr);
}

void Terrain::update_geometry() {
    for (int z = 0; z <= cells_; ++z)
        for (int x = 0; x <= cells_; ++x) {
            int left = std::max(0, x - 1);
            int right = std::min(cells_, x + 1);
            int back = std::max(0, z - 1);
            int front = std::min(cells_, z + 1);
            size_t i = size_t(z * (cells_ + 1) + x);
            float dx = (heights_[size_t(z * (cells_ + 1) + right)] -
                           heights_[size_t(z * (cells_ + 1) + left)]) /
                       ((right - left) * step_);
            float dz = (heights_[size_t(front * (cells_ + 1) + x)] -
                           heights_[size_t(back * (cells_ + 1) + x)]) /
                       ((front - back) * step_);
            vertices_[i].position.y = heights_[i];
            vertices_[i].normal = normalize({ -dx, 1, -dz });
        }
    auto bounds = std::minmax_element(heights_.begin(), heights_.end());
    min_height_ = *bounds.first;
    max_height_ = *bounds.second;
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(
        GL_ARRAY_BUFFER, 0, GLsizeiptr(vertices_.size() * sizeof(Vertex)), vertices_.data());
}

bool Terrain::apply_height_deltas(const std::vector<int32_t>& deltas, float scale) {
    if (deltas.size() != heights_.size())
        throw std::runtime_error("Terrain delta size does not match the height field.");
    if (pending_height_deltas_.empty())
        pending_height_deltas_.resize(heights_.size());
    bool changed = false;
    for (size_t i = 0; i < heights_.size(); ++i) {
        pending_height_deltas_[i] += double(deltas[i]) * double(scale);
        if (pending_height_deltas_[i] == 0)
            continue;
        float updated = float(double(heights_[i]) + pending_height_deltas_[i]);
        double applied = double(updated) - double(heights_[i]);
        pending_height_deltas_[i] -= applied;
        if (updated != heights_[i]) {
            heights_[i] = updated;
            changed = true;
        }
    }
    if (changed)
        update_geometry();
    return changed;
}

void Terrain::deform(Vec3 center, float radius, float elevation) {
    for (size_t i = 0; i < vertices_.size(); ++i) {
        Vec3 p = vertices_[i].position;
        float distance = std::hypot(p.x - center.x, p.z - center.z);
        if (distance >= radius)
            continue;
        float weight = 1.0f - distance / radius;
        weight = weight * weight * (3.0f - 2.0f * weight);
        heights_[i] += elevation * weight;
    }
    update_geometry();
}

void Terrain::carve_crater(Vec3 center, float radius) {
    for (size_t i = 0; i < vertices_.size(); ++i) {
        Vec3 p = vertices_[i].position;
        float r = std::hypot(p.x - center.x, p.z - center.z);
        if (r >= radius * 1.25f)
            continue;
        // Excavate a spherical cap whose depth is 30% of its rim radius.
        float cap_depth = radius * 0.3f;
        float sphere_radius = (radius * radius + cap_depth * cap_depth) / (2 * cap_depth);
        float sphere_center_y = center.y + sphere_radius - cap_depth;
        float bowl =
            r < radius
                ? sphere_center_y - std::sqrt(std::max(0.0f, sphere_radius * sphere_radius - r * r))
                : mix(center.y, heights_[i], smooth(radius, radius * 1.25f, r));
        heights_[i] = std::min(heights_[i], bowl);
    }
    update_geometry();
}

bool Terrain::segment_hit(Vec3 start, Vec3 end, Vec3& hit) const {
    // Test the swept footprint against the mesh, so fast diagonal strikes
    // cannot tunnel through ridges or collide with clamped terrain outside the map.
    float min_x = std::min(start.x, end.x);
    float max_x = std::max(start.x, end.x);
    float min_z = std::min(start.z, end.z);
    float max_z = std::max(start.z, end.z);
    if (max_x < -1000 || min_x > 1000 || max_z < -1000 || min_z > 1000)
        return false;
    auto grid = [](float v) {
        return std::clamp(int(std::floor((v + 1000) / step_)), 0, cells_ - 1);
    };
    int x0 = grid(min_x);
    int x1 = grid(max_x);
    int z0 = grid(min_z);
    int z1 = grid(max_z);
    Vec3 direction = end - start;
    float closest = 2;
    auto triangle = [&](Vec3 a, Vec3 b, Vec3 c) {
        Vec3 e1 = b - a;
        Vec3 e2 = c - a;
        Vec3 p = cross(direction, e2);
        float determinant = dot(e1, p);
        if (std::abs(determinant) < 1e-7f)
            return;
        float inverse = 1 / determinant;
        Vec3 relative = start - a;
        float u = dot(relative, p) * inverse;
        if (u < -0.00001f || u > 1.00001f)
            return;
        Vec3 q = cross(relative, e1);
        float v = dot(direction, q) * inverse;
        if (v < -0.00001f || u + v > 1.00001f)
            return;
        float t = dot(e2, q) * inverse;
        if (t >= 0 && t <= 1)
            closest = std::min(closest, t);
    };
    for (int z = z0; z <= z1; ++z)
        for (int x = x0; x <= x1; ++x) {
            size_t a = size_t(z * (cells_ + 1) + x);
            size_t b = a + 1;
            size_t c = a + cells_ + 1;
            size_t d = c + 1;
            triangle(vertices_[a].position, vertices_[c].position, vertices_[b].position);
            triangle(vertices_[b].position, vertices_[c].position, vertices_[d].position);
        }
    if (closest > 1)
        return false;
    hit = start + direction * closest;
    return true;
}

// Sample the actual mesh triangles, not the higher-frequency noise surface.
float Terrain::surface(float x, float z, Vec3& normal) const {
    float gx = std::clamp((x + 1000) / step_, 0.0f, float(cells_));
    float gz = std::clamp((z + 1000) / step_, 0.0f, float(cells_));
    int ix = std::min(int(gx), cells_ - 1);
    int iz = std::min(int(gz), cells_ - 1);
    float u = gx - ix;
    float v = gz - iz;
    size_t a = size_t(iz * (cells_ + 1) + ix);
    float ha = heights_[a];
    float hb = heights_[a + 1];
    float hc = heights_[a + cells_ + 1];
    float hd = heights_[a + cells_ + 2];
    float dx;
    float dz;
    float y;
    if (u + v <= 1) {
        dx = (hb - ha) / step_;
        dz = (hc - ha) / step_;
        y = ha + (hb - ha) * u + (hc - ha) * v;
    } else {
        dx = (hd - hc) / step_;
        dz = (hd - hb) / step_;
        y = hd + (hc - hd) * (1 - u) + (hb - hd) * (1 - v);
    }
    normal = normalize({ -dx, 1, -dz });
    return y;
}

} // namespace earth_sim
