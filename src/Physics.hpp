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
        float size_scale = 1;
    };
    struct WaterImpact {
        Vec3 position;
        float size_scale = 1;
        bool explosion = false;
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
        // Keep in sync with the flag in particles.vert; vapor expands by default.
        static constexpr uint32_t constant_radius = 1024u;
        float position_age[4];
        float velocity_life[4];
        float data[4];
    };
    struct GpuSimulation {
        static constexpr uint32_t capacity_ = 16000;
        static constexpr uint32_t buckets_ = 32768;
        static constexpr uint32_t terrain_size_ = Terrain::cell_count() + 1;
        static constexpr uint32_t scorched_size_ = 1024;
        // Particle contributions are quantized individually before atomic addition.
        // Nanometre-scale fixed point preserves slow-flow contributions until the
        // asynchronous 100 ms batches are accumulated on the CPU.
        static constexpr float terrain_delta_scale_ = 1000000000.0f;
        struct ErosionReadback {
            GLuint buffer = 0;
            GLsync fence = nullptr;
        };
        std::array<GLuint, 12> compute{};
        GLuint terrain_texture = 0;
        std::array<GLuint, 6> buffers{};
        GLuint sediment = 0;
        GLuint terrain_delta = 0;
        GLuint terrain_flow = 0;
        GLuint scorched_texture = 0;
        std::array<ErosionReadback, 3> erosion_readbacks{};
        uint32_t cursor = 0;
        explicit GpuSimulation(const std::filesystem::path& directory, const Terrain& terrain);
        ~GpuSimulation();
        void upload_terrain(const Terrain& terrain);
        void reset_terrain(const Terrain& terrain);
        void spawn(const std::vector<GpuParticle>& records);
        bool schedule_erosion_readback();
        bool consume_erosion_readback(std::vector<int32_t>& deltas);
        void step(float dt,
            bool interactions,
            float erosion_speed,
            float lifetime,
            float water_level,
            float wind_speed,
            Vec3 wind_direction,
            const std::array<float, max_tornadoes_ * 4>& tornado_centers,
            const std::array<float, max_tornadoes_ * 4>& tornado_movements,
            size_t tornado_count);
    };
    GpuSimulation gpu_;
    std::vector<Meteor> meteors_;
    std::vector<WaterImpact> water_impacts_;
    std::vector<float> impact_strengths_;
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
    double erosion_readback_accumulator_ = 0;
    float lava_emission_ = 0;
    float spring_emission_ = 0;
    float lava_spawn_rate_ = 90.0f;
    float spring_spawn_rate_ = 90.0f;
    double until_next_meteor_ = -1;
    float meteor_frequency_ = 1.0f;
    float scheduled_meteor_frequency_ = 1.0f;
    float particle_lifetime_ = 33.0f;
    float particle_brightness_ = 1.0f;
    float erosion_speed_ = 50.0f;
    bool erosion_simulation_enabled_ = true;
    bool particle_interactions_ = true;
    float range(float low, float high);
    double meteor_interval();
    void add_surface_particles(
        const Terrain& terrain, Vec3 center, float radius, float spacing, bool water);

public:
    explicit Volcanoes(const std::filesystem::path& directory, const Terrain& terrain);
    ~Volcanoes();
    void reset_for_new_terrain(const Terrain& terrain);
    void launch_meteor(Vec3 target, float size_scale);
    void terrain_changed(Terrain& terrain);
    void add_volcano(Vec3 position);
    void add_spring(Vec3 position);
    void remove_lava_source(size_t index);
    void remove_spring_source(size_t index);
    void add_tornado(Vec3 position, Vec3 direction_point, float water_level);
    size_t volcano_count() const;
    size_t spring_count() const;
    size_t tornado_count() const;
    const std::vector<Vec3>& lava_sources() const;
    const std::vector<Vec3>& spring_sources() const;
    const std::vector<Meteor>& meteors() const;
    std::vector<WaterImpact> take_water_impacts();
    std::vector<float> take_impact_strengths();
    GLuint terrain_texture() const;
    GLuint scorched_texture() const;
    GLuint particle_buffer() const;
    GLuint particle_head_buffer() const;
    GLuint particle_next_buffer() const;
    static uint32_t particle_capacity();
    float particle_lifetime() const;
    void set_particle_lifetime(float lifetime);
    float particle_brightness() const;
    void set_particle_brightness(float brightness);
    float lava_spawn_rate() const;
    void set_lava_spawn_rate(float rate);
    float spring_spawn_rate() const;
    void set_spring_spawn_rate(float rate);
    float meteor_frequency() const;
    void set_meteor_frequency(float frequency);
    float erosion_speed() const;
    void set_erosion_speed(float speed);
    bool erosion_simulation_enabled() const;
    void set_erosion_simulation_enabled(bool enabled);
    bool particle_interactions() const;
    void set_particle_interactions(bool enabled);
    void add_water(const Terrain& terrain, Vec3 center, float radius, float spacing);
    void add_lava(const Terrain& terrain, Vec3 center, float radius, float spacing);
    static float meteor_radius(float size_scale) { return 4.0f * size_scale; }
    static float impact_radius(float size_scale) { return 65.0f * size_scale; }
    void impact(Terrain& terrain, Vec3 position, float size_scale);
    void water_impact(Vec3 position, float size_scale);
    void explosion_water_impact(Vec3 position, float size_scale);
    void lightning_water_impact(Vec3 position, size_t particle_count);
    void add_aircraft_vapor(
        Vec3 position, Vec3 forward, Vec3 right, Vec3 up, size_t particle_pairs);
    void update(Terrain& terrain,
        double elapsed,
        float water_level,
        float wind_speed,
        Vec3 wind_direction,
        float meteor_size_scale);
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
    void spawn_between(
        const Terrain& terrain, Vec3 origin, Vec3 target, float water_level, Volcanoes& particles);

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
    void spawn_at(const Terrain& terrain,
        Vec3 target,
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
        Vec3 wind_direction,
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
