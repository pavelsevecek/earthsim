#pragma once

#include "Physics.hpp"

namespace earth_sim {
class TerrainShadows;

class SkyRenderer {
    GLuint shader_ = 0;
    GLuint vao_ = 0;

public:
    explicit SkyRenderer(const std::filesystem::path& directory);
    ~SkyRenderer();
    void draw(Vec3 forward,
        Vec3 right,
        Vec3 up,
        Vec3 sun,
        Vec3 fog,
        float daylight,
        float atmosphere_opacity,
        float time,
        float aspect);
};

class TerrainRenderer {
    GLuint shader_ = 0;

public:
    explicit TerrainRenderer(const std::filesystem::path& directory);
    ~TerrainRenderer();
    void draw(const Terrain& terrain,
        const TerrainShadows& shadows,
        const Rain& rain,
        GLuint scorched_texture,
        const Mat4& vp,
        Vec3 eye,
        Vec3 sun,
        Vec3 fog,
        float daylight,
        float atmosphere_opacity,
        bool clip_enabled = false,
        float clip_height = 0.0f,
        float clip_direction = 1.0f);
};

class WaterRenderer {
    static constexpr int resolution_ = 256;
    GLuint shader_ = 0;
    GLuint simulation_ = 0;
    GLuint impact_ = 0;
    GLuint vao_ = 0;
    GLuint ebo_ = 0;
    std::array<GLuint, 2> states_{};
    int state_index_ = 0;
    GLsizei index_count_ = 0;
    double simulation_accumulator_ = 0.0;
    double wind_time_ = 0.0;

public:
    explicit WaterRenderer(const std::filesystem::path& directory);
    ~WaterRenderer();
    void update(double elapsed,
        const std::vector<Volcanoes::WaterImpact>& impacts,
        bool simulation_enabled,
        float wind_speed,
        Vec3 wind_direction,
        float water_level,
        GLuint terrain_height_texture);
    void draw(const Mat4& vp,
        const Mat4& reflection_vp,
        GLuint reflection_texture,
        GLuint terrain_height_texture,
        Vec3 eye,
        Vec3 sun,
        Vec3 fog,
        float daylight,
        float atmosphere_opacity,
        float water_level,
        float time,
        bool simulation_enabled);
};

class TerrainShadows {
    static constexpr int resolution_ = 2048;
    GLuint shader_ = 0;
    GLuint fbo_ = 0;
    GLuint depth_ = 0;
    std::array<Mat4, 2> light_matrices_{};

public:
    explicit TerrainShadows(const std::filesystem::path& directory);
    ~TerrainShadows();
    void render(const Terrain& terrain, Vec3 sun);
    void bind(GLuint terrain_program) const;
    const Mat4& light_matrix(size_t index) const;
    GLuint depth_texture() const;
};

class Clouds {
    static constexpr int simulation_x_ = 256;
    static constexpr int simulation_y_ = 32;
    static constexpr int simulation_z_ = 256;
    GLuint scene_fbo_ = 0;
    GLuint cloud_fbo_ = 0;
    GLuint scene_color_ = 0;
    GLuint scene_depth_ = 0;
    GLuint scene_emission_ = 0;
    GLuint cloud_color_ = 0;
    GLuint noise_texture_ = 0;
    GLuint shader_ = 0;
    GLuint composite_ = 0;
    GLuint vao_ = 0;
    GLuint simulation_ = 0;
    GLuint vapor_deposition_ = 0;
    GLuint divergence_volume_ = 0;
    GLuint vapor_moisture_ = 0;
    GLuint terrain_height_texture_ = 0;
    std::array<GLuint, 2> density_volumes_{};
    std::array<GLuint, 2> velocity_volumes_{};
    std::array<GLuint, 2> pressure_volumes_{};
    int density_index_ = 0;
    int velocity_index_ = 0;
    int pressure_index_ = 0;
    double simulation_accumulator_ = 0.0;
    int width_ = 0;
    int height_ = 0;

public:
    explicit Clouds(const std::filesystem::path& directory, GLuint terrain_texture);
    ~Clouds();
    GLuint density_texture() const;
    GLuint scene_framebuffer() const;
    GLuint scene_color_texture() const;
    GLuint scene_depth_texture() const;
    GLuint scene_emission_texture() const;
    void clear_density();
    void reset(float wind_speed, Vec3 wind_direction, float cloud_base, float cloud_top);
    void update(double elapsed,
        float time,
        const std::vector<Volcanoes::Meteor>& meteors,
        GLuint particle_buffer,
        bool absorb_vapor,
        float wind_speed,
        Vec3 wind_direction,
        float cloud_base,
        float cloud_top);
    void begin_scene(int w, int h);
    void begin_emission();
    void end_emission();
    void draw(Vec3 eye,
        Vec3 forward,
        Vec3 right,
        Vec3 up,
        Vec3 sun,
        Vec3 fog,
        float daylight,
        float atmosphere_opacity,
        float cloud_opacity,
        float time,
        float coverage,
        float wind_speed,
        Vec3 wind_direction,
        float cloud_base,
        float cloud_top,
        float distance,
        GLuint destination);
};

class ExplosionClouds {
    static constexpr int simulation_x_ = 64;
    static constexpr int simulation_y_ = 96;
    static constexpr int simulation_z_ = 64;
    GLuint simulation_ = 0;
    GLuint shader_ = 0;
    GLuint vao_ = 0;
    GLuint terrain_height_texture_ = 0;
    std::array<GLuint, 2> smoke_volumes_{};
    std::array<GLuint, 2> temperature_volumes_{};
    std::array<GLuint, 2> velocity_volumes_{};
    std::array<GLuint, 2> pressure_volumes_{};
    GLuint divergence_volume_ = 0;
    int scalar_index_ = 0;
    int velocity_index_ = 0;
    int pressure_index_ = 0;
    Vec3 box_min_{};
    Vec3 box_max_{};
    float age_ = 0.0f;
    float strength_ = 1.0f;
    double accumulator_ = 0.0;
    bool active_ = false;

    void initialize();

public:
    explicit ExplosionClouds(const std::filesystem::path& directory, GLuint terrain_texture);
    ~ExplosionClouds();
    void explode(Vec3 position, float strength);
    void clear();
    void update(double elapsed, float wind_speed, Vec3 wind_direction);
    void draw(GLuint destination,
        GLuint scene_depth,
        int width,
        int height,
        Vec3 eye,
        Vec3 forward,
        Vec3 right,
        Vec3 up,
        Vec3 sun,
        float daylight,
        float atmosphere_opacity,
        float distance);
    bool active() const;
};

class ScreenSpaceGI {
    GLuint shader_ = 0;
    GLuint composite_ = 0;
    GLuint fbo_ = 0;
    GLuint texture_ = 0;
    GLuint vao_ = 0;
    int width_ = 0;
    int height_ = 0;

public:
    explicit ScreenSpaceGI(const std::filesystem::path& directory);
    ~ScreenSpaceGI();
    void resize(int w, int h);
    void draw(GLuint scene_fbo,
        GLuint depth,
        GLuint emission,
        int full_width,
        int full_height,
        Vec3 eye,
        Vec3 forward,
        Vec3 right,
        Vec3 up,
        float distance);
};

class PlanarReflection {
    GLuint fbo_ = 0;
    GLuint color_ = 0;
    GLuint depth_ = 0;
    int width_ = 0;
    int height_ = 0;

public:
    PlanarReflection();
    ~PlanarReflection();
    void resize(int full_width, int full_height);
    void begin(int full_width, int full_height);
    GLuint color_texture() const;
    int width() const;
    int height() const;
};

class Bloom {
    GLuint downsample_ = 0;
    GLuint upsample_ = 0;
    GLuint composite_ = 0;
    GLuint vao_ = 0;
    GLuint hdr_fbo_ = 0;
    GLuint hdr_color_ = 0;
    static constexpr int max_mips_ = 12;
    std::array<GLuint, max_mips_> fbos_{};
    std::array<GLuint, max_mips_> colors_{};
    std::array<int, max_mips_> mip_widths_{};
    std::array<int, max_mips_> mip_heights_{};
    int mip_count_ = 0;
    int width_ = 0;
    int height_ = 0;

public:
    explicit Bloom(const std::filesystem::path& directory);
    ~Bloom();
    void resize(int w, int h);
    GLuint hdr_framebuffer() const;
    GLuint hdr_color_texture() const;
    void draw(GLuint scene,
        bool enabled,
        float exposure,
        float bloom_intensity,
        int output_width,
        int output_height);
};

} // namespace earth_sim
