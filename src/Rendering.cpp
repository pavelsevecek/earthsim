#include "Rendering.hpp"

namespace earth_sim {
SkyRenderer::SkyRenderer(const std::filesystem::path& directory) {
    shader_ = program(directory, "sky");
    glGenVertexArrays(1, &vao_);
}

SkyRenderer::~SkyRenderer() {
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(shader_);
}

void SkyRenderer::draw(Vec3 forward,
    Vec3 right,
    Vec3 up,
    Vec3 sun,
    Vec3 fog,
    float daylight,
    float atmosphere_opacity,
    float time,
    float aspect) {
    glUseProgram(shader_);
    uniform(shader_, "cameraForward", forward);
    uniform(shader_, "cameraRight", right);
    uniform(shader_, "cameraUp", up);
    uniform(shader_, "aspect", aspect);
    uniform(shader_, "tanHalfFov", std::tan(pi / 8));
    uniform(shader_, "sunDirection", sun);
    uniform(shader_, "daylight", daylight);
    uniform(shader_, "fogColor", fog);
    uniform(shader_, "atmosphereOpacity", atmosphere_opacity);
    uniform(shader_, "time", time);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

TerrainRenderer::TerrainRenderer(const std::filesystem::path& directory) {
    shader_ = program(directory, "terrain");
}

TerrainRenderer::~TerrainRenderer() {
    glDeleteProgram(shader_);
}

void TerrainRenderer::draw(const Terrain& terrain,
    const TerrainShadows& shadows,
    const Rain& rain,
    const Mat4& vp,
    Vec3 eye,
    Vec3 sun,
    Vec3 fog,
    float daylight,
    float atmosphere_opacity,
    bool clip_enabled,
    float clip_height,
    float clip_direction) {
    glUseProgram(shader_);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "viewProjection"), 1, GL_FALSE, vp.data());
    uniform(shader_, "eye", eye);
    uniform(shader_, "sunDirection", sun);
    uniform(shader_, "daylight", daylight);
    uniform(shader_, "fogColor", fog);
    uniform(shader_, "atmosphereOpacity", atmosphere_opacity);
    if (clip_enabled) {
        uniform(shader_, "clipHeight", clip_height);
        uniform(shader_, "clipDirection", clip_direction);
    }
    rain.bind_wetness();
    shadows.bind(shader_);
    terrain.draw();
}

WaterRenderer::WaterRenderer(const std::filesystem::path& directory) {
    shader_ = program(directory, "water");
    glGenVertexArrays(1, &vao_);
}

WaterRenderer::~WaterRenderer() {
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(shader_);
}

void WaterRenderer::draw(const Mat4& vp,
    const Mat4& reflection_vp,
    GLuint reflection_texture,
    GLuint terrain_height_texture,
    Vec3 eye,
    Vec3 sun,
    Vec3 fog,
    float daylight,
    float atmosphere_opacity,
    float water_level,
    float time) {
    glUseProgram(shader_);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "viewProjection"), 1, GL_FALSE, vp.data());
    uniform(shader_, "eye", eye);
    uniform(shader_, "sunDirection", sun);
    uniform(shader_, "fogColor", fog);
    uniform(shader_, "daylight", daylight);
    uniform(shader_, "atmosphereOpacity", atmosphere_opacity);
    uniform(shader_, "waterLevel", water_level);
    uniform(shader_, "time", time);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, reflection_texture);
    glUniform1i(glGetUniformLocation(shader_, "reflectionTexture"), 6);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, terrain_height_texture);
    glUniform1i(glGetUniformLocation(shader_, "terrainHeight"), 7);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "reflectionViewProjection"),
        1,
        GL_FALSE,
        reflection_vp.data());
    glBindVertexArray(vao_);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

TerrainShadows::TerrainShadows(const std::filesystem::path& directory) {
    shader_ = program(directory, "terrain_shadow");
    glGenFramebuffers(1, &fbo_);
    glGenTextures(1, &depth_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, depth_);
    glTexImage3D(GL_TEXTURE_2D_ARRAY,
        0,
        GL_DEPTH_COMPONENT24,
        resolution_,
        resolution_,
        2,
        0,
        GL_DEPTH_COMPONENT,
        GL_FLOAT,
        nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    const float border[] = { 1, 1, 1, 1 };
    glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, border);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depth_, 0, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteTextures(1, &depth_);
        glDeleteFramebuffers(1, &fbo_);
        glDeleteProgram(shader_);
        throw std::runtime_error("Terrain shadow framebuffer is incomplete.");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

TerrainShadows::~TerrainShadows() {
    glDeleteTextures(1, &depth_);
    glDeleteFramebuffers(1, &fbo_);
    glDeleteProgram(shader_);
}

void TerrainShadows::render(const Terrain& terrain, Vec3 sun) {
    // Fixed world-space coverage encloses the whole mesh at every sun angle,
    // independent of camera zoom/pan. Two layers handle sunlight and moonlight.
    float center_y = (terrain.min_height() + terrain.max_height()) * 0.5f;
    float half_height = (terrain.max_height() - terrain.min_height()) * 0.5f;
    float extent = std::max(1500.0f, std::sqrt(2000000.0f + half_height * half_height) + 50);
    float light_distance = extent + 700;
    float near_plane = 100;
    float far_plane = light_distance + extent + 100;
    const Mat4 projection{ 1 / extent,
        0,
        0,
        0,
        0,
        1 / extent,
        0,
        0,
        0,
        0,
        -2 / (far_plane - near_plane),
        0,
        0,
        0,
        -(far_plane + near_plane) / (far_plane - near_plane),
        1 };
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, resolution_, resolution_);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 2.0f);
    glUseProgram(shader_);
    for (int layer = 0; layer < 2; ++layer) {
        Vec3 direction = sun * (layer == 0 ? 1.0f : -1.0f);
        Vec3 forward = direction * (-1);
        Vec3 reference = std::abs(direction.y) > 0.95f ? Vec3{ 0, 0, 1 } : Vec3{ 0, 1, 0 };
        Vec3 right = normalize(cross(forward, reference));
        Vec3 up = cross(right, forward);
        light_matrices_[layer] = multiply(projection,
            look_at(Vec3{ 0, center_y, 0 } + direction * light_distance, forward, right, up));
        glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depth_, 0, layer);
        glClear(GL_DEPTH_BUFFER_BIT);
        glUniformMatrix4fv(glGetUniformLocation(shader_, "lightViewProjection"),
            1,
            GL_FALSE,
            light_matrices_[layer].data());
        terrain.draw();
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void TerrainShadows::bind(GLuint terrain_program) const {
    glUniformMatrix4fv(glGetUniformLocation(terrain_program, "lightViewProjection[0]"),
        2,
        GL_FALSE,
        light_matrices_[0].data());
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D_ARRAY, depth_);
    glUniform1i(glGetUniformLocation(terrain_program, "terrainShadowMap"), 3);
    glActiveTexture(GL_TEXTURE0);
}

const Mat4& TerrainShadows::light_matrix(size_t index) const {
    return light_matrices_[index];
}

GLuint TerrainShadows::depth_texture() const {
    return depth_;
}

Clouds::Clouds(const std::filesystem::path& directory, GLuint terrain_texture)
    : terrain_height_texture_(terrain_texture) {
    shader_ = program(directory, "clouds");
    try {
        composite_ = program(directory, "cloud_composite");
        simulation_ = compute_program(directory, "cloud_sim");
        vapor_deposition_ = compute_program(directory, "cloud_vapor");
    } catch (...) {
        glDeleteProgram(shader_);
        throw;
    }
    glGenVertexArrays(1, &vao_);
    glGenFramebuffers(1, &scene_fbo_);
    glGenFramebuffers(1, &cloud_fbo_);
    glGenTextures(1, &scene_color_);
    glGenTextures(1, &scene_depth_);
    glGenTextures(1, &scene_emission_);
    glGenTextures(1, &cloud_color_);
    glGenTextures(1, &noise_texture_);
    glBindTexture(GL_TEXTURE_3D, noise_texture_);
    constexpr int size = 64;
    std::vector<unsigned char> values(size * size * size);
    std::mt19937 random(79231);
    for (auto& value : values)
        value = static_cast<unsigned char>(random() & 255);
    glTexImage3D(
        GL_TEXTURE_3D, 0, GL_R8, size, size, size, 0, GL_RED, GL_UNSIGNED_BYTE, values.data());
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
    glGenTextures(2, density_volumes_.data());
    glGenTextures(2, velocity_volumes_.data());
    glGenTextures(2, pressure_volumes_.data());
    glGenTextures(1, &divergence_volume_);
    glGenBuffers(1, &vapor_moisture_);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, vapor_moisture_);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        GLsizeiptr(simulation_x_ * simulation_y_ * simulation_z_ * sizeof(uint32_t)),
        nullptr,
        GL_DYNAMIC_DRAW);
    auto allocate_volume = [&](GLuint texture, GLint format, GLenum components) {
        glBindTexture(GL_TEXTURE_3D, texture);
        glTexImage3D(GL_TEXTURE_3D,
            0,
            format,
            simulation_x_,
            simulation_y_,
            simulation_z_,
            0,
            components,
            GL_FLOAT,
            nullptr);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
    };
    for (GLuint texture : density_volumes_)
        allocate_volume(texture, GL_R16F, GL_RED);
    for (GLuint texture : velocity_volumes_)
        allocate_volume(texture, GL_RGBA16F, GL_RGBA);
    for (GLuint texture : pressure_volumes_)
        allocate_volume(texture, GL_R16F, GL_RED);
    allocate_volume(divergence_volume_, GL_R16F, GL_RED);
    glUseProgram(simulation_);
    glUniform3i(glGetUniformLocation(simulation_, "volumeSize"),
        simulation_x_,
        simulation_y_,
        simulation_z_);
    uniform(simulation_, "windSpeed", 10.0f);
    uniform(simulation_, "cloudBase", 290.0f);
    uniform(simulation_, "cloudTop", 530.0f);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, terrain_height_texture_);
    glUniform1i(glGetUniformLocation(simulation_, "terrainHeight"), 4);
    auto initialize = [&](GLuint density, GLuint velocity, GLuint scalar, int pass) {
        glBindImageTexture(0, density, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
        glBindImageTexture(1, velocity, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        glBindImageTexture(2, scalar, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
        glUniform1i(glGetUniformLocation(simulation_, "pass"), pass);
        glDispatchCompute(
            (simulation_x_ + 3) / 4, (simulation_y_ + 3) / 4, (simulation_z_ + 3) / 4);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    };
    initialize(density_volumes_[0], velocity_volumes_[0], pressure_volumes_[0], 0);
    initialize(density_volumes_[1], velocity_volumes_[1], pressure_volumes_[1], 0);
    initialize(density_volumes_[0], velocity_volumes_[0], divergence_volume_, 6);
}

Clouds::~Clouds() {
    glDeleteFramebuffers(1, &scene_fbo_);
    glDeleteFramebuffers(1, &cloud_fbo_);
    GLuint textures[] = {
        scene_color_, scene_depth_, scene_emission_, cloud_color_, noise_texture_
    };
    glDeleteTextures(5, textures);
    glDeleteTextures(2, density_volumes_.data());
    glDeleteTextures(2, velocity_volumes_.data());
    glDeleteTextures(2, pressure_volumes_.data());
    glDeleteTextures(1, &divergence_volume_);
    glDeleteBuffers(1, &vapor_moisture_);
    glDeleteProgram(shader_);
    glDeleteProgram(composite_);
    glDeleteProgram(simulation_);
    glDeleteProgram(vapor_deposition_);
    glDeleteVertexArrays(1, &vao_);
}

GLuint Clouds::density_texture() const {
    return density_volumes_[size_t(density_index_)];
}

GLuint Clouds::scene_framebuffer() const {
    return scene_fbo_;
}

GLuint Clouds::scene_color_texture() const {
    return scene_color_;
}

GLuint Clouds::scene_depth_texture() const {
    return scene_depth_;
}

GLuint Clouds::scene_emission_texture() const {
    return scene_emission_;
}

void Clouds::clear_density() {
    glUseProgram(simulation_);
    glUniform3i(glGetUniformLocation(simulation_, "volumeSize"),
        simulation_x_,
        simulation_y_,
        simulation_z_);
    glUniform1i(glGetUniformLocation(simulation_, "pass"), 7);
    for (size_t i = 0; i < density_volumes_.size(); ++i) {
        glBindImageTexture(0, density_volumes_[i], 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
        glBindImageTexture(1, velocity_volumes_[i], 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        glBindImageTexture(2, pressure_volumes_[i], 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
        glDispatchCompute(
            (simulation_x_ + 3) / 4, (simulation_y_ + 3) / 4, (simulation_z_ + 3) / 4);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }
}

void Clouds::update(double elapsed,
    float time,
    const std::vector<Volcanoes::Meteor>& meteors,
    GLuint particle_buffer,
    bool absorb_vapor,
    float wind_speed,
    float cloud_base,
    float cloud_top) {
    constexpr float step = 1.0f / 15.0f;
    simulation_accumulator_ += elapsed;
    while (simulation_accumulator_ >= step) {
        simulation_accumulator_ -= step;
        constexpr uint32_t voxel_count = uint32_t(simulation_x_ * simulation_y_ * simulation_z_);
        glUseProgram(vapor_deposition_);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, particle_buffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, vapor_moisture_);
        glUniform1ui(glGetUniformLocation(vapor_deposition_, "particleCapacity"),
            Volcanoes::particle_capacity());
        glUniform1ui(glGetUniformLocation(vapor_deposition_, "voxelCount"), voxel_count);
        glUniform3i(glGetUniformLocation(vapor_deposition_, "volumeSize"),
            simulation_x_,
            simulation_y_,
            simulation_z_);
        uniform(vapor_deposition_, "cloudBase", cloud_base);
        uniform(vapor_deposition_, "cloudTop", cloud_top);
        glUniform1i(glGetUniformLocation(vapor_deposition_, "pass"), 0);
        glDispatchCompute((voxel_count + 127) / 128, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        if (absorb_vapor) {
            glUniform1i(glGetUniformLocation(vapor_deposition_, "pass"), 1);
            glDispatchCompute((Volcanoes::particle_capacity() + 127) / 128, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }
        glUseProgram(simulation_);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, vapor_moisture_);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, terrain_height_texture_);
        glUniform1i(glGetUniformLocation(simulation_, "terrainHeight"), 4);
        glUniform3i(glGetUniformLocation(simulation_, "volumeSize"),
            simulation_x_,
            simulation_y_,
            simulation_z_);
        uniform(simulation_, "dt", step);
        uniform(simulation_, "time", time);
        uniform(simulation_, "windSpeed", wind_speed);
        uniform(simulation_, "cloudBase", cloud_base);
        uniform(simulation_, "cloudTop", cloud_top);
        std::array<float, 32> positions{};
        std::array<float, 32> velocities{};
        int meteor_count = 0;
        for (const auto& meteor : meteors) {
            if (meteor_count >= 8 || meteor.position.y < cloud_base - 120.0f ||
                meteor.position.y > cloud_top + 120.0f || std::abs(meteor.position.x) > 2320.0f ||
                std::abs(meteor.position.z) > 2320.0f)
                continue;
            size_t offset = size_t(meteor_count) * 4;
            positions[offset] = meteor.position.x;
            positions[offset + 1] = meteor.position.y;
            positions[offset + 2] = meteor.position.z;
            positions[offset + 3] = 120.0f;
            velocities[offset] = meteor.velocity.x;
            velocities[offset + 1] = meteor.velocity.y;
            velocities[offset + 2] = meteor.velocity.z;
            velocities[offset + 3] =
                std::min(300.0f, std::sqrt(dot(meteor.velocity, meteor.velocity)) * 0.3f);
            ++meteor_count;
        }
        glUniform1i(glGetUniformLocation(simulation_, "meteorCount"), meteor_count);
        if (meteor_count > 0) {
            glUniform4fv(glGetUniformLocation(simulation_, "meteorPositionRadius[0]"),
                meteor_count,
                positions.data());
            glUniform4fv(glGetUniformLocation(simulation_, "meteorVelocityStrength[0]"),
                meteor_count,
                velocities.data());
        }
        auto source = [&](int unit, const char* name, GLuint texture) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_3D, texture);
            glUniform1i(glGetUniformLocation(simulation_, name), unit);
        };
        auto run = [&](int pass, GLuint density_out, GLuint velocity_out, GLuint scalar_out) {
            glBindImageTexture(0, density_out, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
            glBindImageTexture(1, velocity_out, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
            glBindImageTexture(2, scalar_out, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_R16F);
            glUniform1i(glGetUniformLocation(simulation_, "pass"), pass);
            glDispatchCompute(
                (simulation_x_ + 3) / 4, (simulation_y_ + 3) / 4, (simulation_z_ + 3) / 4);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        };
        source(0, "densityTexture", density_volumes_[size_t(density_index_)]);
        source(1, "velocityTexture", velocity_volumes_[size_t(velocity_index_)]);
        int next_velocity = 1 - velocity_index_;
        run(1,
            density_volumes_[size_t(1 - density_index_)],
            velocity_volumes_[size_t(next_velocity)],
            divergence_volume_);
        velocity_index_ = next_velocity;
        source(1, "velocityTexture", velocity_volumes_[size_t(velocity_index_)]);
        run(2,
            density_volumes_[size_t(1 - density_index_)],
            velocity_volumes_[size_t(1 - velocity_index_)],
            divergence_volume_);
        source(3, "divergenceTexture", divergence_volume_);
        for (int iteration = 0; iteration < 10; ++iteration) {
            source(2, "pressureTexture", pressure_volumes_[size_t(pressure_index_)]);
            int next_pressure = 1 - pressure_index_;
            run(3,
                density_volumes_[size_t(1 - density_index_)],
                velocity_volumes_[size_t(1 - velocity_index_)],
                pressure_volumes_[size_t(next_pressure)]);
            pressure_index_ = next_pressure;
        }
        source(2, "pressureTexture", pressure_volumes_[size_t(pressure_index_)]);
        int projected_velocity = 1 - velocity_index_;
        run(4,
            density_volumes_[size_t(1 - density_index_)],
            velocity_volumes_[size_t(projected_velocity)],
            divergence_volume_);
        velocity_index_ = projected_velocity;
        source(0, "densityTexture", density_volumes_[size_t(density_index_)]);
        source(1, "velocityTexture", velocity_volumes_[size_t(velocity_index_)]);
        int next_density = 1 - density_index_;
        run(5,
            density_volumes_[size_t(next_density)],
            velocity_volumes_[size_t(1 - velocity_index_)],
            divergence_volume_);
        density_index_ = next_density;
    }
    glActiveTexture(GL_TEXTURE0);
}

void Clouds::begin_scene(int w, int h) {
    if (w != width_ || h != height_) {
        width_ = w;
        height_ = h;
        auto allocate = [](GLuint texture, int w, int h, bool depth) {
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D,
                0,
                depth ? GL_DEPTH_COMPONENT24 : GL_RGBA16F,
                w,
                h,
                0,
                depth ? GL_DEPTH_COMPONENT : GL_RGBA,
                GL_FLOAT,
                nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, depth ? GL_NEAREST : GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, depth ? GL_NEAREST : GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        };
        allocate(scene_color_, w, h, false);
        allocate(scene_depth_, w, h, true);
        allocate(scene_emission_, w, h, false);
        glBindTexture(GL_TEXTURE_2D, scene_emission_);
        int mip_width = w;
        int mip_height = h;
        int level = 0;
        while (mip_width > 1 || mip_height > 1) {
            mip_width = std::max(1, mip_width / 2);
            mip_height = std::max(1, mip_height / 2);
            ++level;
            glTexImage2D(GL_TEXTURE_2D,
                level,
                GL_RGBA16F,
                mip_width,
                mip_height,
                0,
                GL_RGBA,
                GL_FLOAT,
                nullptr);
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, level);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        allocate(cloud_color_, (w + 1) / 2, (h + 1) / 2, false);
        glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo_);
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, scene_color_, 0);
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, scene_emission_, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, scene_depth_, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Cloud scene framebuffer is incomplete.");
        glBindFramebuffer(GL_FRAMEBUFFER, cloud_fbo_);
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, cloud_color_, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Cloud volume framebuffer is incomplete.");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo_);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
}

void Clouds::begin_emission() {
    const GLenum buffers[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, buffers);
    const float black[] = { 0, 0, 0, 0 };
    glClearBufferfv(GL_COLOR, 1, black);
}

void Clouds::end_emission() {
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
}

void Clouds::draw(Vec3 eye,
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
    float cloud_base,
    float cloud_top,
    float distance,
    GLuint destination) {
    glBindFramebuffer(GL_FRAMEBUFFER, cloud_fbo_);
    glViewport(0, 0, (width_ + 1) / 2, (height_ + 1) / 2);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glUseProgram(shader_);
    glBindVertexArray(vao_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene_depth_);
    glUniform1i(glGetUniformLocation(shader_, "sceneDepth"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, noise_texture_);
    glUniform1i(glGetUniformLocation(shader_, "noiseTexture"), 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_3D, density_texture());
    glUniform1i(glGetUniformLocation(shader_, "cloudDensityTexture"), 2);
    uniform(shader_, "eye", eye);
    uniform(shader_, "cameraForward", forward);
    uniform(shader_, "cameraRight", right);
    uniform(shader_, "cameraUp", up);
    uniform(shader_, "sunDirection", sun);
    uniform(shader_, "fogColor", fog);
    uniform(shader_, "daylight", daylight);
    uniform(shader_, "atmosphereOpacity", atmosphere_opacity);
    uniform(shader_, "cloudOpacity", cloud_opacity);
    uniform(shader_, "time", time);
    uniform(shader_, "cloudCoverage", coverage);
    uniform(shader_, "windSpeed", wind_speed);
    uniform(shader_, "cloudBase", cloud_base);
    uniform(shader_, "cloudTop", cloud_top);
    uniform(shader_, "aspect", float(width_) / height_);
    uniform(shader_, "tanHalfFov", std::tan(pi / 8));
    Mat4 projection = perspective(float(width_) / height_, distance);
    glUniform2f(glGetUniformLocation(shader_, "depthProjection"), projection[10], projection[14]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, destination);
    glViewport(0, 0, width_, height_);
    glUseProgram(composite_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene_color_);
    glUniform1i(glGetUniformLocation(composite_, "sceneColor"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, scene_depth_);
    glUniform1i(glGetUniformLocation(composite_, "sceneDepth"), 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, cloud_color_);
    glUniform1i(glGetUniformLocation(composite_, "cloudColor"), 2);
    glUniform2f(
        glGetUniformLocation(composite_, "depthProjection"), projection[10], projection[14]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    glActiveTexture(GL_TEXTURE0);
}

ScreenSpaceGI::ScreenSpaceGI(const std::filesystem::path& directory) {
    shader_ = program(directory, "ssgi");
    try {
        composite_ = program(directory, "ssgi_composite");
    } catch (...) {
        glDeleteProgram(shader_);
        throw;
    }
    glGenFramebuffers(1, &fbo_);
    glGenTextures(1, &texture_);
    glGenVertexArrays(1, &vao_);
}

ScreenSpaceGI::~ScreenSpaceGI() {
    glDeleteProgram(shader_);
    glDeleteProgram(composite_);
    glDeleteFramebuffers(1, &fbo_);
    glDeleteTextures(1, &texture_);
    glDeleteVertexArrays(1, &vao_);
}

void ScreenSpaceGI::resize(int w, int h) {
    int next_width = std::max(1, (w + 1) / 2);
    int next_height = std::max(1, (h + 1) / 2);
    if (next_width == width_ && next_height == height_)
        return;
    width_ = next_width;
    height_ = next_height;
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width_, height_, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("SSGI framebuffer is incomplete.");
}

void ScreenSpaceGI::draw(GLuint scene_fbo,
    GLuint depth,
    GLuint emission,
    int full_width,
    int full_height,
    Vec3 eye,
    Vec3 forward,
    Vec3 right,
    Vec3 up,
    float distance) {
    resize(full_width, full_height);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glBindTexture(GL_TEXTURE_2D, emission);
    glGenerateMipmap(GL_TEXTURE_2D);
    glViewport(0, 0, width_, height_);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glBindVertexArray(vao_);
    glUseProgram(shader_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, depth);
    glUniform1i(glGetUniformLocation(shader_, "sceneDepth"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, emission);
    glUniform1i(glGetUniformLocation(shader_, "sceneEmission"), 1);
    uniform(shader_, "eye", eye);
    uniform(shader_, "cameraForward", forward);
    uniform(shader_, "cameraRight", right);
    uniform(shader_, "cameraUp", up);
    uniform(shader_, "aspect", float(full_width) / full_height);
    uniform(shader_, "tanHalfFov", std::tan(pi / 8));
    Mat4 projection = perspective(float(full_width) / full_height, distance);
    glUniform2f(glGetUniformLocation(shader_, "depthProjection"), projection[10], projection[14]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo);
    glViewport(0, 0, full_width, full_height);
    glUseProgram(composite_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glUniform1i(glGetUniformLocation(composite_, "indirectLight"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, depth);
    glUniform1i(glGetUniformLocation(composite_, "sceneDepth"), 1);
    glUniform2f(
        glGetUniformLocation(composite_, "depthProjection"), projection[10], projection[14]);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glActiveTexture(GL_TEXTURE0);
}

PlanarReflection::PlanarReflection() {
    glGenFramebuffers(1, &fbo_);
    glGenTextures(1, &color_);
    glGenTextures(1, &depth_);
}

PlanarReflection::~PlanarReflection() {
    glDeleteFramebuffers(1, &fbo_);
    glDeleteTextures(1, &color_);
    glDeleteTextures(1, &depth_);
}

void PlanarReflection::resize(int full_width, int full_height) {
    int next_width = std::max(1, (full_width + 1) / 2);
    int next_height = std::max(1, (full_height + 1) / 2);
    if (next_width == width_ && next_height == height_)
        return;
    width_ = next_width;
    height_ = next_height;
    auto allocate =
        [&](GLuint texture, GLenum internal_format, GLenum format, GLenum type, GLint filter) {
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(
                GL_TEXTURE_2D, 0, internal_format, width_, height_, 0, format, type, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        };
    allocate(color_, GL_RGBA16F, GL_RGBA, GL_FLOAT, GL_LINEAR);
    allocate(depth_, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_FLOAT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth_, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Planar reflection framebuffer is incomplete.");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void PlanarReflection::begin(int full_width, int full_height) {
    resize(full_width, full_height);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width_, height_);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

GLuint PlanarReflection::color_texture() const {
    return color_;
}

int PlanarReflection::width() const {
    return width_;
}

int PlanarReflection::height() const {
    return height_;
}

Bloom::Bloom(const std::filesystem::path& directory) {
    try {
        downsample_ = program(directory, "bloom_downsample");
        upsample_ = program(directory, "bloom_upsample");
        composite_ = program(directory, "bloom_composite");
    } catch (...) {
        glDeleteProgram(downsample_);
        glDeleteProgram(upsample_);
        glDeleteProgram(composite_);
        throw;
    }
    glGenVertexArrays(1, &vao_);
    glGenFramebuffers(1, &hdr_fbo_);
    glGenTextures(1, &hdr_color_);
    glGenFramebuffers(max_mips_, fbos_.data());
    glGenTextures(max_mips_, colors_.data());
}

Bloom::~Bloom() {
    glDeleteProgram(downsample_);
    glDeleteProgram(upsample_);
    glDeleteProgram(composite_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteFramebuffers(1, &hdr_fbo_);
    glDeleteTextures(1, &hdr_color_);
    glDeleteFramebuffers(max_mips_, fbos_.data());
    glDeleteTextures(max_mips_, colors_.data());
}

GLuint Bloom::hdr_framebuffer() const {
    return hdr_fbo_;
}

GLuint Bloom::hdr_color_texture() const {
    return hdr_color_;
}

void Bloom::resize(int w, int h) {
    if (w == width_ && h == height_)
        return;
    auto allocate = [](GLuint fbo, GLuint texture, int w, int h) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Bloom framebuffer is incomplete.");
    };
    allocate(hdr_fbo_, hdr_color_, w, h);
    int mw = w;
    int mh = h;
    mip_count_ = 0;
    for (int i = 0; i < max_mips_; ++i) {
        mw = std::max(1, mw / 2);
        mh = std::max(1, mh / 2);
        mip_widths_[i] = mw;
        mip_heights_[i] = mh;
        allocate(fbos_[i], colors_[i], mw, mh);
        ++mip_count_;
        if (mw < 4 || mh < 4)
            break;
    }
    width_ = w;
    height_ = h;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Bloom::draw(GLuint scene,
    bool enabled,
    float exposure,
    float bloom_intensity,
    int output_width,
    int output_height) {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glBindVertexArray(vao_);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(downsample_);
    glUniform1i(glGetUniformLocation(downsample_, "source"), 0);
    // Threshold-free HDR pyramid, progressively downsampled with a 13-tap filter.
    for (int i = 0; enabled && i < mip_count_; ++i) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbos_[i]);
        glViewport(0, 0, mip_widths_[i], mip_heights_[i]);
        glBindTexture(GL_TEXTURE_2D, i == 0 ? scene : colors_[i - 1]);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    // Add each smaller mip back into its parent through a 3x3 tent filter.
    glUseProgram(upsample_);
    glUniform1i(glGetUniformLocation(upsample_, "source"), 0);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE);
    for (int i = enabled ? mip_count_ - 1 : 0; i > 0; --i) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbos_[i - 1]);
        glViewport(0, 0, mip_widths_[i - 1], mip_heights_[i - 1]);
        glBindTexture(GL_TEXTURE_2D, colors_[i]);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, output_width, output_height);
    glUseProgram(composite_);
    glBindTexture(GL_TEXTURE_2D, scene);
    glUniform1i(glGetUniformLocation(composite_, "scene"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, colors_[0]);
    glUniform1i(glGetUniformLocation(composite_, "bloom"), 1);
    uniform(composite_, "bloomStrength", enabled ? 0.04f * bloom_intensity : 0.0f);
    uniform(composite_, "exposure", exposure);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glActiveTexture(GL_TEXTURE0);
    glDepthMask(GL_TRUE);
}

} // namespace earth_sim
