#pragma once

#include "Shaders.hpp"
#include "Terrain.hpp"

namespace earth_sim {
class Volcanoes {
public:
    struct Meteor {
        Vec3 position;
        Vec3 velocity;
        float trail_emission = 0;
    };

private:
    static constexpr size_t max_tornadoes_ = 8;
    static constexpr float source_clearance_ = 2.0f;
    struct Tornado {
        Vec3 position;
        Vec3 velocity;
        float age;
        float lifetime;
        uint32_t id;
    };
    struct alignas(16) GpuParticle {
        float position_age[4];
        float velocity_life[4];
        float data[4];
    };
    struct GpuSimulation {
        static constexpr uint32_t capacity_ = 16000;
        static constexpr uint32_t buckets_ = 32768;
        GLuint compute = 0;
        GLuint terrain_texture = 0;
        std::array<GLuint, 6> buffers{};
        uint32_t cursor = 0;
        explicit GpuSimulation(const std::filesystem::path& directory, const Terrain& terrain);
        ~GpuSimulation();
        void upload_terrain(const Terrain& terrain);
        void spawn(const std::vector<GpuParticle>& records);
        void step(float dt,
            bool interactions,
            float lifetime,
            float water_level,
            float wind_speed,
            const std::array<float, max_tornadoes_ * 4>& tornado_centers,
            const std::array<float, max_tornadoes_ * 4>& tornado_movements,
            size_t tornado_count);
    };
    GpuSimulation gpu_;
    std::vector<Meteor> meteors_;
    std::vector<Tornado> tornadoes_;
    uint32_t next_tornado_id_ = 1;
    struct Sprite {
        Vec3 position;
        float size;
        float opacity;
        Vec3 emission_color;
    };
    std::array<Vec3, 256> blackbody_colors_{};
    // Integrate Planck radiance against the Wyman/Sloan/Shirley CIE 1931 fits:
    // https://jcgt.org/published/0002/02/01/ (wavelengths in nm, temperature in K).
    static Vec3 blackbody(float temperature);
    Vec3 glow(float temperature) const;
    std::mt19937 random_{ std::random_device{}() };
    std::vector<Vec3> vents_;
    std::vector<Vec3> springs_;
    std::vector<Sprite> sprites_;
    GLuint shader_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint blackbody_texture_ = 0;
    double accumulator_ = 0;
    float emission_ = 0;
    float particle_lifetime_ = 33.0f;
    float particle_brightness_ = 1.0f;
    bool particle_interactions_ = true;
    float range(float low, float high);

public:
    explicit Volcanoes(const std::filesystem::path& directory, const Terrain& terrain);
    ~Volcanoes();
    void launch_meteor(Vec3 target);
    void terrain_changed(Terrain& terrain);
    void add_volcano(Vec3 position);
    void add_spring(Vec3 position);
    void add_tornado(Vec3 position, Vec3 direction_point, float water_level);
    size_t volcano_count() const;
    size_t spring_count() const;
    size_t tornado_count() const;
    const std::vector<Meteor>& meteors() const;
    GLuint terrain_texture() const;
    GLuint particle_buffer() const;
    GLuint particle_head_buffer() const;
    GLuint particle_next_buffer() const;
    static uint32_t particle_capacity();
    float particle_lifetime() const;
    void set_particle_lifetime(float lifetime);
    float particle_brightness() const;
    void set_particle_brightness(float brightness);
    bool particle_interactions() const;
    void set_particle_interactions(bool enabled);
    void impact(Terrain& terrain, Vec3 position);
    void water_impact(Vec3 position);
    void lightning_water_impact(Vec3 position, size_t particle_count);
    void update(Terrain& terrain, double elapsed, float water_level, float wind_speed);
    void prepare_draw(const Mat4& vp,
        const Mat4& light_vp,
        GLuint shadow_map,
        GLuint reflection_texture,
        const Mat4& reflection_vp,
        Vec3 right,
        Vec3 up,
        Vec3 eye,
        Vec3 sun,
        float daylight,
        float atmosphere_opacity,
        float time,
        bool reflection_capture,
        bool clip_enabled,
        float clip_height,
        float clip_direction);
    void draw(const Mat4& vp,
        const Mat4& light_vp,
        GLuint shadow_map,
        GLuint reflection_texture,
        const Mat4& reflection_vp,
        Vec3 right,
        Vec3 up,
        Vec3 eye,
        Vec3 sun,
        float daylight,
        float atmosphere_opacity,
        float time,
        bool draw_vapor,
        bool reflection_capture = false,
        bool clip_enabled = false,
        float clip_height = 0.0f,
        float clip_direction = 1.0f);
    void draw_vapor(const Mat4& vp,
        const Mat4& light_vp,
        GLuint shadow_map,
        GLuint reflection_texture,
        const Mat4& reflection_vp,
        Vec3 right,
        Vec3 up,
        Vec3 eye,
        Vec3 sun,
        float daylight,
        float atmosphere_opacity,
        float time);
};

class Lightning {
    struct Segment {
        Vec3 start;
        float strength;
        Vec3 end;
        float width;
    };
    struct Strike {
        std::vector<Segment> segments;
        float age = 0;
    };
    GLuint shader_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    std::vector<Strike> strikes_;
    std::vector<Segment> visible_segments_;
    std::mt19937 random_{ std::random_device{}() };
    double until_next_ = -1;
    float frequency_ = 6.0f;
    float scheduled_frequency_ = 6.0f;

public:
    explicit Lightning(const std::filesystem::path& directory);
    ~Lightning();
    float range(float low, float high);
    std::vector<Vec3> path(Vec3 start, Vec3 end, int count, float jitter);
    void append_path(Strike& strike, const std::vector<Vec3>& points, float strength, float width);
    void spawn(const Terrain& terrain,
        float water_level,
        float cloud_base,
        float cloud_top,
        Volcanoes& particles);
    double interval();
    float frequency() const;
    void set_frequency(float frequency);
    void update(const Terrain& terrain,
        double elapsed,
        float water_level,
        float cloud_base,
        float cloud_top,
        Volcanoes& particles);
    void draw(const Mat4& vp,
        Vec3 eye,
        Vec3 camera_right,
        bool clip_enabled = false,
        float clip_height = 0.0f,
        float clip_direction = 1.0f);
};

class Rain {
    static constexpr uint32_t capacity_ = 32768;
    static constexpr uint32_t wetness_size_ = 256;
    struct alignas(16) GpuDrop {
        float position_age[4];
        float velocity_state[4];
    };
    GLuint compute_ = 0;
    GLuint shader_ = 0;
    GLuint vao_ = 0;
    GLuint drops_ = 0;
    GLuint wetness_ = 0;
    uint32_t frame_seed_ = 1;
    double accumulator_ = 0;
    float intensity_ = 0.f;

public:
    explicit Rain(const std::filesystem::path& directory);
    ~Rain();
    float intensity() const;
    void set_intensity(float intensity);
    void update(double elapsed,
        bool clouds_enabled,
        float cloud_coverage,
        Vec3 eye,
        float water_level,
        float time,
        float wind_speed,
        float cloud_base,
        float cloud_top,
        GLuint terrain_texture,
        GLuint cloud_density,
        GLuint main_particles,
        GLuint heads,
        GLuint next);
    void bind_wetness() const;
    void draw(const Mat4& vp,
        Vec3 eye,
        Vec3 right,
        Vec3 up,
        float daylight,
        bool clip_enabled = false,
        float clip_height = 0.0f,
        float clip_direction = 1.0f);
};

} // namespace earth_sim
