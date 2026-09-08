#include "Physics.hpp"

namespace earth_sim {
Volcanoes::GpuSimulation::GpuSimulation(const std::filesystem::path& directory,
    const Terrain& terrain) {
    try {
        constexpr std::array<const char*, 12> pass_names{ "particle_clear_hash",
            "particle_integrate",
            "particle_build_hash",
            "particle_density_constraint",
            "particle_position_correction",
            "particle_apply_correction",
            "particle_reconstruct_velocity",
            "particle_viscosity_collision",
            "particle_surface_exchange",
            "particle_water_lava_interaction",
            "terrain_clear_flow",
            "terrain_erode_flow" };
        for (size_t pass = 0; pass < compute.size(); ++pass)
            compute[pass] = compute_program(directory, pass_names[pass]);
    } catch (...) {
        for (GLuint program : compute)
            glDeleteProgram(program);
        throw;
    }
    glGenBuffers(GLsizei(buffers.size()), buffers.data());
    const GLsizeiptr sizes[] = { GLsizeiptr(capacity_ * sizeof(GpuParticle)),
        GLsizeiptr(capacity_ * 4 * sizeof(float)),
        GLsizeiptr(capacity_ * 4 * sizeof(float)),
        GLsizeiptr(capacity_ * sizeof(float)),
        GLsizeiptr(buckets_ * sizeof(int)),
        GLsizeiptr(capacity_ * sizeof(int)) };
    for (size_t i = 0; i < buffers.size(); ++i) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[i]);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizes[i], nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, GLuint(i), buffers[i]);
    }
    glGenBuffers(1, &sediment);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, sediment);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        GLsizeiptr(capacity_ * 4 * sizeof(float)),
        nullptr,
        GL_DYNAMIC_DRAW);
    const float zero_float = 0;
    glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32F, GL_RED, GL_FLOAT, &zero_float);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, sediment);

    glGenBuffers(1, &terrain_delta);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, terrain_delta);
    constexpr size_t terrain_values = size_t(terrain_size_) * terrain_size_;
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        GLsizeiptr(terrain_values * sizeof(int32_t)),
        nullptr,
        GL_DYNAMIC_COPY);
    const int32_t zero_int = 0;
    glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32I, GL_RED_INTEGER, GL_INT, &zero_int);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, terrain_delta);

    glGenBuffers(1, &terrain_flow);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, terrain_flow);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        GLsizeiptr(terrain_values * 4 * sizeof(int32_t)),
        nullptr,
        GL_DYNAMIC_DRAW);
    glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32I, GL_RED_INTEGER, GL_INT, &zero_int);

    for (auto& readback : erosion_readbacks) {
        glGenBuffers(1, &readback.buffer);
        glBindBuffer(GL_COPY_WRITE_BUFFER, readback.buffer);
        glBufferData(GL_COPY_WRITE_BUFFER,
            GLsizeiptr(terrain_values * sizeof(int32_t)),
            nullptr,
            GL_STREAM_READ);
    }
    std::vector<GpuParticle> inactive(capacity_);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[0]);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER,
        0,
        GLsizeiptr(inactive.size() * sizeof(GpuParticle)),
        inactive.data());
    glGenTextures(1, &terrain_texture);
    upload_terrain(terrain);

    glGenTextures(1, &scorched_texture);
    glBindTexture(GL_TEXTURE_2D, scorched_texture);
    std::vector<uint32_t> unscorched(size_t(scorched_size_) * scorched_size_);
    glTexImage2D(GL_TEXTURE_2D,
        0,
        GL_R32UI,
        scorched_size_,
        scorched_size_,
        0,
        GL_RED_INTEGER,
        GL_UNSIGNED_INT,
        unscorched.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

Volcanoes::GpuSimulation::~GpuSimulation() {
    for (auto& readback : erosion_readbacks) {
        if (readback.fence)
            glDeleteSync(readback.fence);
        glDeleteBuffers(1, &readback.buffer);
    }
    glDeleteBuffers(1, &terrain_delta);
    glDeleteBuffers(1, &terrain_flow);
    glDeleteBuffers(1, &sediment);
    for (GLuint program : compute)
        glDeleteProgram(program);
    glDeleteBuffers(GLsizei(buffers.size()), buffers.data());
    glDeleteTextures(1, &terrain_texture);
    glDeleteTextures(1, &scorched_texture);
}

void Volcanoes::GpuSimulation::upload_terrain(const Terrain& terrain) {
    glBindTexture(GL_TEXTURE_2D, terrain_texture);
    glTexImage2D(GL_TEXTURE_2D,
        0,
        GL_R32F,
        Terrain::cell_count() + 1,
        Terrain::cell_count() + 1,
        0,
        GL_RED,
        GL_FLOAT,
        terrain.height_data().data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Volcanoes::GpuSimulation::reset_terrain(const Terrain& terrain) {
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT);
    upload_terrain(terrain);

    const float zero_float = 0;
    for (size_t i = 0; i < 4; ++i) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[i]);
        glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32F, GL_RED, GL_FLOAT, &zero_float);
    }
    const int32_t zero_int = 0;
    const int32_t no_particle = -1;
    for (size_t i = 4; i < buffers.size(); ++i) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[i]);
        glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32I, GL_RED_INTEGER, GL_INT, &no_particle);
    }
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, sediment);
    glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32F, GL_RED, GL_FLOAT, &zero_float);
    cursor = 0;

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, terrain_delta);
    glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32I, GL_RED_INTEGER, GL_INT, &zero_int);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, terrain_flow);
    glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32I, GL_RED_INTEGER, GL_INT, &zero_int);
    for (auto& readback : erosion_readbacks) {
        if (readback.fence)
            glDeleteSync(readback.fence);
        readback.fence = nullptr;
    }

    glBindTexture(GL_TEXTURE_2D, scorched_texture);
    std::vector<uint32_t> unscorched(size_t(scorched_size_) * scorched_size_);
    glTexSubImage2D(GL_TEXTURE_2D,
        0,
        0,
        0,
        scorched_size_,
        scorched_size_,
        GL_RED_INTEGER,
        GL_UNSIGNED_INT,
        unscorched.data());
}

void Volcanoes::GpuSimulation::spawn(const std::vector<GpuParticle>& records) {
    if (records.empty())
        return;
    size_t source = 0;
    while (source < records.size()) {
        size_t count = std::min(records.size() - source, size_t(capacity_ - cursor));
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[0]);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER,
            GLintptr(cursor * sizeof(GpuParticle)),
            GLsizeiptr(count * sizeof(GpuParticle)),
            records.data() + source);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, sediment);
        const float zero = 0;
        glClearBufferSubData(GL_SHADER_STORAGE_BUFFER,
            GL_R32F,
            GLintptr(cursor * 4 * sizeof(float)),
            GLsizeiptr(count * 4 * sizeof(float)),
            GL_RED,
            GL_FLOAT,
            &zero);
        cursor = (cursor + uint32_t(count)) % capacity_;
        source += count;
    }
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
}

bool Volcanoes::GpuSimulation::schedule_erosion_readback() {
    auto available = std::find_if(erosion_readbacks.begin(),
        erosion_readbacks.end(),
        [](const ErosionReadback& readback) { return readback.fence == nullptr; });
    if (available == erosion_readbacks.end())
        return false;
    constexpr GLsizeiptr bytes =
        GLsizeiptr(size_t(terrain_size_) * terrain_size_ * sizeof(int32_t));
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    glBindBuffer(GL_COPY_READ_BUFFER, terrain_delta);
    glBindBuffer(GL_COPY_WRITE_BUFFER, available->buffer);
    glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, bytes);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, terrain_delta);
    const int32_t zero = 0;
    glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32I, GL_RED_INTEGER, GL_INT, &zero);
    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    available->fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();
    return true;
}

bool Volcanoes::GpuSimulation::consume_erosion_readback(std::vector<int32_t>& deltas) {
    auto ready = std::find_if(
        erosion_readbacks.begin(), erosion_readbacks.end(), [](const ErosionReadback& readback) {
            if (!readback.fence)
                return false;
            GLenum status = glClientWaitSync(readback.fence, 0, 0);
            return status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED;
        });
    if (ready == erosion_readbacks.end())
        return false;
    constexpr size_t values = size_t(terrain_size_) * terrain_size_;
    constexpr GLsizeiptr bytes = GLsizeiptr(values * sizeof(int32_t));
    glBindBuffer(GL_COPY_WRITE_BUFFER, ready->buffer);
    const auto* mapped = static_cast<const int32_t*>(
        glMapBufferRange(GL_COPY_WRITE_BUFFER, 0, bytes, GL_MAP_READ_BIT));
    if (!mapped)
        throw std::runtime_error("Cannot map completed terrain erosion readback.");
    deltas.assign(mapped, mapped + values);
    glUnmapBuffer(GL_COPY_WRITE_BUFFER);
    glDeleteSync(ready->fence);
    ready->fence = nullptr;
    return true;
}

void Volcanoes::GpuSimulation::step(float dt,
    bool interactions,
    float erosion_speed,
    float lifetime,
    float water_level,
    float wind_speed,
    Vec3 wind_direction,
    const std::array<float, max_tornadoes_ * 4>& tornado_centers,
    const std::array<float, max_tornadoes_ * 4>& tornado_movements,
    size_t tornado_count) {
    for (size_t i = 0; i < buffers.size(); ++i)
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, GLuint(i), buffers[i]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, sediment);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, terrain_delta);
    glBindImageTexture(0, scorched_texture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, terrain_texture);
    auto run = [&](int pass, uint32_t count) {
        GLuint program = compute[size_t(pass)];
        glUseProgram(program);
        glUniform1i(glGetUniformLocation(program, "terrainHeight"), 0);
        glUniform1ui(glGetUniformLocation(program, "capacity"), pass >= 10 ? count : capacity_);
        glUniform1f(glGetUniformLocation(program, "dt"), dt);
        glUniform1f(glGetUniformLocation(program, "particleLifetime"), lifetime);
        glUniform1f(glGetUniformLocation(program, "waterLevel"), water_level);
        glUniform1f(glGetUniformLocation(program, "windSpeed"), wind_speed);
        glUniform2f(
            glGetUniformLocation(program, "windDirection"), wind_direction.x, wind_direction.z);
        glUniform1f(glGetUniformLocation(program, "erosionSpeed"), erosion_speed);
        glUniform1f(glGetUniformLocation(program, "terrainDeltaScale"), terrain_delta_scale_);
        glUniform1i(glGetUniformLocation(program, "tornadoCount"), GLint(tornado_count));
        if (tornado_count > 0) {
            glUniform4fv(glGetUniformLocation(program, "tornadoCenterId[0]"),
                GLsizei(tornado_count),
                tornado_centers.data());
            glUniform4fv(glGetUniformLocation(program, "tornadoMovementHeight[0]"),
                GLsizei(tornado_count),
                tornado_movements.data());
        }
        glUniform1i(glGetUniformLocation(program, "interactions"), interactions ? 1 : 0);
        glDispatchCompute((count + 127) / 128, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    };
    run(0, std::max(capacity_, buckets_));
    run(1, capacity_);
    // Phase changes need the hash even when fluid interactions are disabled.
    run(0, buckets_);
    run(2, capacity_);
    run(9, capacity_);
    for (int iteration = 0; interactions && iteration < 3; ++iteration) {
        run(0, buckets_);
        run(2, capacity_);
        run(3, capacity_);
        run(4, capacity_);
        run(5, capacity_);
    }
    if (interactions) {
        run(0, buckets_);
        run(2, capacity_);
    }
    run(6, capacity_);
    run(7, capacity_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, terrain_flow);
    constexpr uint32_t terrain_values = terrain_size_ * terrain_size_;
    run(10, terrain_values);
    run(8, capacity_);
    run(11, terrain_values);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                    GL_TEXTURE_FETCH_BARRIER_BIT);
}

// Integrate Planck radiance against the Wyman/Sloan/Shirley CIE 1931 fits:
// https://jcgt.org/published/0002/02/01/ (wavelengths in nm, temperature in K).
Vec3 Volcanoes::blackbody(float temperature) {
    Vec3 xyz{};
    for (int wavelength = 380; wavelength <= 780; wavelength += 5) {
        double nm = wavelength;
        auto gaussian = [nm](double center, double left, double right) {
            double t = (nm - center) * (nm < center ? left : right);
            return std::exp(-0.5 * t * t);
        };
        double x = 1.056 * gaussian(599.8, .0264, .0323) + .362 * gaussian(442, .0624, .0374) -
                   .065 * gaussian(501.1, .049, .0382);
        double y = .821 * gaussian(568.8, .0213, .0247) + .286 * gaussian(530.9, .0613, .0322);
        double z = 1.217 * gaussian(437, .0845, .0278) + .681 * gaussian(459, .0385, .0725);
        // A fixed radiance reference preserves cooling-related dimming.
        double radiance = std::pow(560 / nm, 5) * std::expm1(1.438776877e7 / (560 * 1600.0)) /
                          std::expm1(1.438776877e7 / (nm * temperature));
        xyz = xyz + Vec3{ float(x), float(y), float(z) } * float(radiance * 5);
    }
    return { std::max(0.0f, 3.2406f * xyz.x - 1.5372f * xyz.y - .4986f * xyz.z),
        std::max(0.0f, -.9689f * xyz.x + 1.8758f * xyz.y + .0415f * xyz.z),
        std::max(0.0f, .0557f * xyz.x - .2040f * xyz.y + 1.0570f * xyz.z) };
}

Vec3 Volcanoes::glow(float temperature) const {
    float index = std::clamp((temperature - 300) / 1500, 0.0f, 1.0f) * 255;
    size_t low = std::min(size_t(index), size_t(254));
    return blackbody_colors_[low] * (1 - (index - low)) +
           blackbody_colors_[low + 1] * (index - low);
}

float Volcanoes::range(float low, float high) {
    return std::uniform_real_distribution<float>(low, high)(random_);
}

double Volcanoes::meteor_interval() {
    double rate = std::max(double(meteor_frequency_) / 60.0, 1e-6);
    return std::exponential_distribution<double>(rate)(random_);
}

Volcanoes::Volcanoes(const std::filesystem::path& directory, const Terrain& terrain)
    : gpu_(directory, terrain) {
    Vec3 reference = blackbody(1600);
    float scale = 1 / std::max({ reference.x, reference.y, reference.z });
    for (size_t i = 0; i < blackbody_colors_.size(); ++i)
        blackbody_colors_[i] = blackbody(300 + 1500 * float(i) / 255) * scale;
    sprites_.reserve(32);
    glGenTextures(1, &blackbody_texture_);
    glBindTexture(GL_TEXTURE_1D, blackbody_texture_);
    glTexImage1D(GL_TEXTURE_1D,
        0,
        GL_RGB32F,
        GLsizei(blackbody_colors_.size()),
        0,
        GL_RGB,
        GL_FLOAT,
        blackbody_colors_.data());
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    shader_ = program(directory, "particles");
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Sprite), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 2, GL_FLOAT, GL_FALSE, sizeof(Sprite), reinterpret_cast<void*>(offsetof(Sprite, size)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(Sprite),
        reinterpret_cast<void*>(offsetof(Sprite, emission_color)));
    glVertexAttribDivisor(0, 1);
    glVertexAttribDivisor(1, 1);
    glVertexAttribDivisor(2, 1);
}

Volcanoes::~Volcanoes() {
    glDeleteTextures(1, &blackbody_texture_);
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(shader_);
}

void Volcanoes::reset_for_new_terrain(const Terrain& terrain) {
    vents_.clear();
    springs_.clear();
    impact_strengths_.clear();
    lava_emission_ = 0;
    spring_emission_ = 0;
    erosion_readback_accumulator_ = 0;
    gpu_.reset_terrain(terrain);
}

void Volcanoes::launch_meteor(Vec3 target, float size_scale) {
    float azimuth = range(0, 2 * pi);
    float tilt = range(15, 55) * pi / 180;
    Vec3 approach{
        std::sin(tilt) * std::cos(azimuth), std::cos(tilt), std::sin(tilt) * std::sin(azimuth)
    };
    meteors_.push_back(
        { target + approach * (700 / approach.y), approach * (-range(650, 900)), 0, size_scale });
}

void Volcanoes::terrain_changed(Terrain& terrain) {
    gpu_.upload_terrain(terrain);
    for (auto& vent : vents_) {
        Vec3 n;
        vent.y = terrain.surface(vent.x, vent.z, n) + source_clearance_;
    }
    for (auto& spring : springs_) {
        Vec3 n;
        spring.y = terrain.surface(spring.x, spring.z, n) + source_clearance_;
    }
}

void Volcanoes::add_volcano(Vec3 position) {
    position.y += source_clearance_;
    vents_.push_back(position);
}

void Volcanoes::add_spring(Vec3 position) {
    position.y += source_clearance_;
    springs_.push_back(position);
}

void Volcanoes::remove_lava_source(size_t index) {
    if (index < vents_.size())
        vents_.erase(vents_.begin() + index);
}

void Volcanoes::remove_spring_source(size_t index) {
    if (index < springs_.size())
        springs_.erase(springs_.begin() + index);
}

void Volcanoes::add_tornado(Vec3 position, Vec3 direction_point, float water_level) {
    Vec3 direction{ direction_point.x - position.x, 0, direction_point.z - position.z };
    float direction_length = std::sqrt(dot(direction, direction));
    if (direction_length < 0.001f)
        return;
    direction = direction * (1.0f / direction_length);

    if (tornadoes_.size() == max_tornadoes_)
        tornadoes_.erase(tornadoes_.begin());

    position.y = std::max(position.y, water_level + source_clearance_);
    float speed = range(12.0f, 24.0f);
    float lifetime = range(20.0f, 45.0f);
    uint32_t id = next_tornado_id_++;
    tornadoes_.push_back({ position, direction * speed, 0, lifetime, id });

    constexpr size_t particle_count = 1500;
    constexpr float height = 150.0f;
    std::vector<GpuParticle> records;
    records.reserve(particle_count);
    for (size_t i = 0; i < particle_count; ++i) {
        float y = range(2.0f, height);
        float outer_radius = 6.0f + y * 0.2f;
        float radius = outer_radius * std::sqrt(range(0.0f, 1.0f));
        float phase = range(0, 2 * pi);
        Vec3 p = position + Vec3{ std::cos(phase) * radius, y, std::sin(phase) * radius };
        float size = range(4.0f, 6.f);
        records.push_back(
            { { p.x, p.y, p.z, 0 }, { 0, 0, 0, lifetime }, { size, float(id), 64, 1 } });
    }
    gpu_.spawn(records);
}

size_t Volcanoes::volcano_count() const {
    return vents_.size();
}

size_t Volcanoes::spring_count() const {
    return springs_.size();
}

size_t Volcanoes::tornado_count() const {
    return tornadoes_.size();
}

const std::vector<Vec3>& Volcanoes::lava_sources() const {
    return vents_;
}

const std::vector<Vec3>& Volcanoes::spring_sources() const {
    return springs_;
}

const std::vector<Volcanoes::Meteor>& Volcanoes::meteors() const {
    return meteors_;
}

std::vector<Volcanoes::WaterImpact> Volcanoes::take_water_impacts() {
    std::vector<WaterImpact> impacts;
    impacts.swap(water_impacts_);
    return impacts;
}

std::vector<float> Volcanoes::take_impact_strengths() {
    std::vector<float> strengths;
    strengths.swap(impact_strengths_);
    return strengths;
}

GLuint Volcanoes::terrain_texture() const {
    return gpu_.terrain_texture;
}

GLuint Volcanoes::scorched_texture() const {
    return gpu_.scorched_texture;
}

GLuint Volcanoes::particle_buffer() const {
    return gpu_.buffers[0];
}

GLuint Volcanoes::particle_head_buffer() const {
    return gpu_.buffers[4];
}

GLuint Volcanoes::particle_next_buffer() const {
    return gpu_.buffers[5];
}

uint32_t Volcanoes::particle_capacity() {
    return GpuSimulation::capacity_;
}

float Volcanoes::particle_lifetime() const {
    return particle_lifetime_;
}

void Volcanoes::set_particle_lifetime(float lifetime) {
    particle_lifetime_ = lifetime;
}

float Volcanoes::particle_brightness() const {
    return particle_brightness_;
}

void Volcanoes::set_particle_brightness(float brightness) {
    particle_brightness_ = brightness;
}

float Volcanoes::lava_spawn_rate() const {
    return lava_spawn_rate_;
}

void Volcanoes::set_lava_spawn_rate(float rate) {
    lava_spawn_rate_ = std::max(0.0f, rate);
}

float Volcanoes::spring_spawn_rate() const {
    return spring_spawn_rate_;
}

void Volcanoes::set_spring_spawn_rate(float rate) {
    spring_spawn_rate_ = std::max(0.0f, rate);
}

float Volcanoes::meteor_frequency() const {
    return meteor_frequency_;
}

void Volcanoes::set_meteor_frequency(float frequency) {
    meteor_frequency_ = frequency;
}

float Volcanoes::erosion_speed() const {
    return erosion_speed_;
}

void Volcanoes::set_erosion_speed(float speed) {
    erosion_speed_ = std::max(0.0f, speed);
}

bool Volcanoes::erosion_simulation_enabled() const {
    return erosion_simulation_enabled_;
}

void Volcanoes::set_erosion_simulation_enabled(bool enabled) {
    erosion_simulation_enabled_ = enabled;
}

bool Volcanoes::particle_interactions() const {
    return particle_interactions_;
}

void Volcanoes::set_particle_interactions(bool enabled) {
    particle_interactions_ = enabled;
}

void Volcanoes::add_surface_particles(const Terrain& terrain,
    Vec3 center,
    float radius,
    float spacing,
    bool water) {
    constexpr float golden_angle = 2.39996323f;
    const float area = pi * radius * radius;
    const size_t particle_count = std::min(size_t(GpuSimulation::capacity_),
        std::max(size_t(1), size_t(std::ceil(area / (spacing * spacing)))));

    std::vector<GpuParticle> records;
    records.reserve(particle_count);
    const float phase = range(0, 2 * pi);
    for (size_t i = 0; i < particle_count; ++i) {
        // A sunflower distribution fills the whole brush evenly without the clumps
        // and bare patches produced by independent random samples.
        float radial_distance = radius * std::sqrt((float(i) + 0.5f) / float(particle_count));
        float angle = phase + float(i) * golden_angle;
        Vec3 position =
            center +
            Vec3{ std::cos(angle) * radial_distance, 0, std::sin(angle) * radial_distance };
        if (std::abs(position.x) >= 999.0f || std::abs(position.z) >= 999.0f)
            continue;

        float size = range(1.2f, 2.2f);
        Vec3 normal;
        float ground = terrain.surface(position.x, position.z, normal);
        float particle_radius = std::max(0.65f, size * 0.52f);
        position.y = ground + particle_radius / std::max(normal.y, 0.25f) + 0.25f;
        float temperature = water ? 300.0f : range(1450.0f, 1650.0f);
        float flags = water ? 23.0f : 7.0f;
        records.push_back({ { position.x, position.y, position.z, 0 },
            { 0, 0, 0, 0 },
            { size, temperature, flags, 1 } });
    }
    gpu_.spawn(records);
}

void Volcanoes::add_water(const Terrain& terrain, Vec3 center, float radius, float spacing) {
    add_surface_particles(terrain, center, radius, spacing, true);
}

void Volcanoes::add_lava(const Terrain& terrain, Vec3 center, float radius, float spacing) {
    add_surface_particles(terrain, center, radius, spacing, false);
}

void Volcanoes::impact(Terrain& terrain, Vec3 position, float size_scale) {
    impact_strengths_.push_back(size_scale);
    terrain.carve_crater(position, impact_radius(size_scale));
    constexpr size_t ejecta_count = 1200;
    std::vector<GpuParticle> records;
    records.reserve(ejecta_count);
    for (size_t i = 0; i < ejecta_count; ++i) {
        float angle = range(0, 2 * pi);
        float speed = range(10, 50) * size_scale;
        // Cone surface: exactly 45 degrees from global +Y, independent of slope.
        float horizontal_speed = speed * std::sin(pi / 4);
        Vec3 velocity{ std::cos(angle) * horizontal_speed,
            speed * std::cos(pi / 4),
            std::sin(angle) * horizontal_speed };
        float radius = range(0, 18);
        Vec3 p = position + Vec3{ std::cos(angle) * radius, 2, std::sin(angle) * radius };
        Vec3 n;
        p.y = std::max(p.y, terrain.surface(p.x, p.z, n) + 2);
        p = p + velocity * 0.2f;
        float size = range(1.2f, 2.8f) * size_scale;
        float temperature = range(1550, 1800);
        records.push_back({ { p.x, p.y, p.z, 0 },
            { velocity.x, velocity.y, velocity.z, 0 },
            { size, temperature, 7, 1 } });
    }
    gpu_.spawn(records);
}

void Volcanoes::water_impact(Vec3 position, float size_scale) {
    impact_strengths_.push_back(size_scale);
    water_impacts_.push_back({ position, size_scale });
    constexpr size_t vapor_count = 1200;
    std::vector<GpuParticle> records;
    records.reserve(vapor_count);
    for (size_t i = 0; i < vapor_count; ++i) {
        float angle = range(0, 2 * pi);
        float radius = std::sqrt(range(0, 1)) * 32.0f;
        Vec3 radial{ std::cos(angle), 0, std::sin(angle) };
        Vec3 p = position + radial * radius;
        p.y += range(1.0f, 8.0f);
        float horizontal_speed = range(2.0f, 14.0f);
        Vec3 velocity = radial * horizontal_speed;
        velocity.y = 6.0f;
        float size = range(2.0f, 5.0f);
        records.push_back({ { p.x, p.y, p.z, 0 },
            { velocity.x, velocity.y, velocity.z, 0 },
            { size, 300, 32, 1 } });
    }
    gpu_.spawn(records);
}

void Volcanoes::explosion_water_impact(Vec3 position, float size_scale) {
    water_impacts_.push_back({ position, size_scale, true });
}

void Volcanoes::lightning_water_impact(Vec3 position, size_t particle_count) {
    std::vector<GpuParticle> records;
    records.reserve(particle_count);
    for (size_t i = 0; i < particle_count; ++i) {
        Vec3 p = position + Vec3{ range(-3.0f, 3.0f), range(1.0f, 5.0f), range(-3.0f, 3.0f) };
        Vec3 velocity{ range(-8.0f, 8.0f), range(4.0f, 14.0f), range(-8.0f, 8.0f) };
        float size = range(1.5f, 3.5f);
        // Vapor plus a burst tag that preserves the randomized launch velocity.
        records.push_back({ { p.x, p.y, p.z, 0 },
            { velocity.x, velocity.y, velocity.z, 0 },
            { size, 300, 160, 1 } });
    }
    gpu_.spawn(records);
}

void Volcanoes::add_aircraft_vapor(
    Vec3 position, Vec3 forward, Vec3 right, Vec3 up, size_t particle_pairs) {
    std::vector<GpuParticle> records;
    records.reserve(particle_pairs * 2);
    for (size_t i = 0; i < particle_pairs * 2; ++i) {
        float side = (i & 1) == 0 ? -1.0f : 1.0f;
        Vec3 p = position - forward * 3.75f + right * (side * 3.75f) +
                  right * range(-0.175f, 0.175f) + up * range(-0.125f, 0.125f);
        Vec3 velocity = forward * range(27.5f, 34.0f) + right * range(-0.3f, 0.3f) +
                        up * range(-0.15f, 0.4f);
        float size = range(0.3f, 0.6f);
        // Vapor plus a burst tag retains the aircraft's wake velocity.
        records.push_back({ { p.x, p.y, p.z, 0 },
            { velocity.x, velocity.y, velocity.z, 0 },
            { size, 300, 160, 1 } });
    }
    gpu_.spawn(records);
}

void Volcanoes::update(Terrain& terrain,
    double elapsed,
    float water_level,
    float wind_speed,
    Vec3 wind_direction,
    float meteor_size_scale) {
    constexpr float dt = 1.0f / 120.0f;
    // The shared clock bounds real elapsed time before applying the speed multiplier.
    std::vector<int32_t> erosion_delta;
    bool terrain_eroded = false;
    while (gpu_.consume_erosion_readback(erosion_delta))
        terrain_eroded |=
            terrain.apply_height_deltas(erosion_delta, 1.0f / GpuSimulation::terrain_delta_scale_);
    bool crater_changed = terrain.update_craters(elapsed);
    if (terrain_eroded || crater_changed)
        terrain_changed(terrain);

    if (meteor_frequency_ != scheduled_meteor_frequency_) {
        scheduled_meteor_frequency_ = meteor_frequency_;
        until_next_meteor_ = -1;
    }
    if (meteor_frequency_ <= 0) {
        until_next_meteor_ = -1;
    } else {
        if (until_next_meteor_ < 0)
            until_next_meteor_ = meteor_interval();
        until_next_meteor_ -= elapsed;
        while (until_next_meteor_ <= 0) {
            float target_x = range(-900.0f, 900.0f);
            float target_z = range(-900.0f, 900.0f);
            Vec3 normal;
            float target_y = terrain.surface(target_x, target_z, normal);
            launch_meteor({ target_x, target_y, target_z }, meteor_size_scale);
            until_next_meteor_ += meteor_interval();
        }
    }

    accumulator_ += elapsed;
    erosion_readback_accumulator_ += elapsed;
    while (accumulator_ >= dt) {
        accumulator_ -= dt;
        std::array<float, max_tornadoes_ * 4> tornado_centers{};
        std::array<float, max_tornadoes_ * 4> tornado_movements{};
        for (size_t i = 0; i < tornadoes_.size();) {
            auto& tornado = tornadoes_[i];
            Vec3 normal;
            terrain.surface(tornado.position.x, tornado.position.z, normal);
            float speed = std::sqrt(dot(tornado.velocity, tornado.velocity));
            Vec3 heading = tornado.velocity * (1.0f / speed);
            Vec3 gradient{
                -normal.x / std::max(normal.y, 0.001f), 0, -normal.z / std::max(normal.y, 0.001f)
            };
            float uphill_slope = dot(gradient, heading);
            if (uphill_slope > 0.1f) {
                Vec3 contour{ -gradient.z, 0, gradient.x };
                contour = normalize(contour);
                if (dot(contour, heading) < 0)
                    contour = contour * -1.0f;
                float turn = 1.0f - std::exp(-dt);
                Vec3 smoothed_heading = normalize(heading * (1.0f - turn) + contour * turn);
                tornado.velocity = smoothed_heading * speed;
            }
            Vec3 previous = tornado.position;
            tornado.position = tornado.position + tornado.velocity * dt;
            tornado.position.y =
                std::max(terrain.surface(tornado.position.x, tornado.position.z, normal),
                    water_level + source_clearance_);
            tornado.age += dt;
            if (tornado.age >= tornado.lifetime || std::abs(tornado.position.x) >= 990.0f ||
                std::abs(tornado.position.z) >= 990.0f) {
                tornadoes_.erase(tornadoes_.begin() + i);
                continue;
            }
            size_t offset = i * 4;
            tornado_centers[offset] = tornado.position.x;
            tornado_centers[offset + 1] = tornado.position.y;
            tornado_centers[offset + 2] = tornado.position.z;
            tornado_centers[offset + 3] = float(tornado.id);
            Vec3 movement = tornado.position - previous;
            tornado_movements[offset] = movement.x;
            tornado_movements[offset + 1] = movement.y;
            tornado_movements[offset + 2] = movement.z;
            tornado_movements[offset + 3] = 150.0f;
            ++i;
        }
        for (size_t i = 0; i < meteors_.size();) {
            auto& meteor = meteors_[i];
            Vec3 previous_position = meteor.position;
            Vec3 next = meteor.position + meteor.velocity * dt;
            Vec3 contact;
            bool hit_water = false;
            if (previous_position.y > water_level && next.y <= water_level) {
                float crossing =
                    (previous_position.y - water_level) / (previous_position.y - next.y);
                Vec3 water_contact = previous_position + (next - previous_position) * crossing;
                Vec3 normal;
                if (terrain.surface(water_contact.x, water_contact.z, normal) < water_level) {
                    contact = water_contact;
                    hit_water = true;
                }
            }
            bool hit = hit_water || terrain.segment_hit(previous_position, next, contact);
            if (hit)
                next = contact;
            meteor.trail_emission += 600 * dt;
            while (meteor.trail_emission >= 1) {
                meteor.trail_emission -= 1;
                Vec3 p = previous_position + (next - previous_position) * range(0, 1);
                Vec3 v{ range(-1.5f, 1.5f), range(-0.5f, 0.5f), range(-1.5f, 1.5f) };
                float life = range(1.5f, 3.0f);
                float size = range(1.5f, 3.5f) * meteor.size_scale;
                gpu_.spawn({ GpuParticle{
                    { p.x, p.y, p.z, 0 }, { v.x, v.y, v.z, life }, { size, 1800, 8, 1 } } });
            }
            meteor.position = next;
            if (hit) {
                if (hit_water)
                    water_impact(next, meteor.size_scale);
                else
                    impact(terrain, next, meteor.size_scale);
                meteors_.erase(meteors_.begin() + i);
            } else if (next.y < terrain.min_height() - 1000)
                meteors_.erase(meteors_.begin() + i);
            else
                ++i;
        }
        lava_emission_ += lava_spawn_rate_ * dt;
        spring_emission_ += spring_spawn_rate_ * dt;
        std::vector<GpuParticle> spawned;
        while (lava_emission_ >= 1) {
            lava_emission_ -= 1;
            for (Vec3 vent : vents_) {
                Vec3 velocity{ range(-1.2f, 1.2f), range(0.0f, 1.5f), range(-1.2f, 1.2f) };
                float size = range(1.2f, 2.2f);
                float temperature = range(1450, 1650);
                Vec3 spawn =
                    vent + Vec3{ range(-2.0f, 2.0f), range(0.25f, 5.f), range(-2.0f, 2.0f) };
                Vec3 normal;
                float ground = terrain.surface(spawn.x, spawn.z, normal);
                float radius = std::max(0.65f, size * 0.52f);
                spawn.y = std::max(spawn.y, ground + radius / std::max(normal.y, 0.25f) + range(0.25f, 5.f));
                spawned.push_back({ { spawn.x, spawn.y, spawn.z, 0 },
                    { velocity.x, velocity.y, velocity.z, 0 },
                    { size, temperature, 7, 1 } });
            }
        }
        while (spring_emission_ >= 1) {
            spring_emission_ -= 1;
            for (Vec3 spring : springs_) {
                Vec3 velocity{ range(-1.2f, 1.2f), range(0.0f, 1.5f), range(-1.2f, 1.2f) };
                float size = range(1.2f, 2.2f);
                Vec3 spawn =
                    spring + Vec3{ range(-2.0f, 2.0f), range(0.25f, 5.f), range(-2.0f, 2.0f) };
                Vec3 normal;
                float ground = terrain.surface(spawn.x, spawn.z, normal);
                float radius = std::max(0.65f, size * 0.52f);
                spawn.y = std::max(
                    spawn.y, ground + radius / std::max(normal.y, 0.25f) + range(0.25f, 5.f));
                spawn = spawn + velocity * range(0, 1);
                velocity = { 0, 0, 0 };
                // Gravity, terrain collision, and fluid interaction plus the water tag.
                spawned.push_back({ { spawn.x, spawn.y, spawn.z, 0 },
                    { velocity.x, velocity.y, velocity.z, 0 },
                    { size, 300, 23, 1 } });
            }
        }
        gpu_.spawn(spawned);
        gpu_.step(dt,
            particle_interactions_,
            erosion_simulation_enabled_ ? erosion_speed_ : 0.0f,
            particle_lifetime_,
            water_level,
            wind_speed,
            wind_direction,
            tornado_centers,
            tornado_movements,
            tornadoes_.size());
    }
    if (erosion_simulation_enabled_ && erosion_readback_accumulator_ >= 0.1 &&
        gpu_.schedule_erosion_readback())
        erosion_readback_accumulator_ = std::fmod(erosion_readback_accumulator_, 0.1);
}

void Volcanoes::prepare_draw(const Mat4& vp,
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
    float clip_direction) {
    glUseProgram(shader_);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "viewProjection"), 1, GL_FALSE, vp.data());
    uniform(shader_, "cameraRight", right);
    uniform(shader_, "cameraUp", up);
    uniform(shader_, "eye", eye);
    uniform(shader_, "sunDirection", sun);
    uniform(shader_, "daylight", daylight);
    uniform(shader_, "atmosphereOpacity", atmosphere_opacity);
    uniform(shader_, "particleBrightness", pow(2.f, particle_brightness_));
    glUniform1f(glGetUniformLocation(shader_, "particleLifetime"), particle_lifetime_);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_1D, blackbody_texture_);
    glUniform1i(glGetUniformLocation(shader_, "blackbodyColors"), 4);
    glUniformMatrix4fv(
        glGetUniformLocation(shader_, "lightViewProjection"), 1, GL_FALSE, light_vp.data());
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D_ARRAY, shadow_map);
    glUniform1i(glGetUniformLocation(shader_, "terrainShadowMap"), 5);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, reflection_texture);
    glUniform1i(glGetUniformLocation(shader_, "reflectionTexture"), 6);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "reflectionViewProjection"),
        1,
        GL_FALSE,
        reflection_vp.data());
    uniform(shader_, "time", time);
    glUniform1i(glGetUniformLocation(shader_, "reflectionCapture"), reflection_capture ? 1 : 0);
    glUniform1i(glGetUniformLocation(shader_, "clipEnabled"), clip_enabled ? 1 : 0);
    uniform(shader_, "clipHeight", clip_height);
    uniform(shader_, "clipDirection", clip_direction);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, gpu_.buffers[0]);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
}

void Volcanoes::draw(const Mat4& vp,
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
    bool reflection_capture,
    bool clip_enabled,
    float clip_height,
    float clip_direction) {
    prepare_draw(vp,
        light_vp,
        shadow_map,
        reflection_texture,
        reflection_vp,
        right,
        up,
        eye,
        sun,
        daylight,
        atmosphere_opacity,
        time,
        reflection_capture,
        clip_enabled,
        clip_height,
        clip_direction);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    // GPU particles source every per-instance value from the SSBO. Leaving the
    // CPU-sprite attributes enabled would make this draw fetch thousands of
    // records from a VBO that can still have a zero-sized data store.
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glUniform1i(glGetUniformLocation(shader_, "gpuParticles"), 1);
    glUniform1i(glGetUniformLocation(shader_, "renderMode"), 0);
    glUniform1i(glGetUniformLocation(shader_, "opaquePass"), 1);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, GLsizei(GpuSimulation::capacity_));

    sprites_.clear();
    for (Vec3 vent : vents_)
        sprites_.push_back({ vent + Vec3{ 0, 3.0f, 0 }, 5, 1, glow(1600) });
    for (const auto& meteor : meteors_)
        sprites_.push_back({ meteor.position, 2 * meteor_radius(meteor.size_scale), 1, glow(1800) * 3 });
    if (!sprites_.empty()) {
        glBufferData(GL_ARRAY_BUFFER,
            GLsizeiptr(sprites_.size() * sizeof(Sprite)),
            sprites_.data(),
            GL_STREAM_DRAW);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glUniform1i(glGetUniformLocation(shader_, "gpuParticles"), 0);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, GLsizei(sprites_.size()));
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glDisableVertexAttribArray(2);
    }

    glUniform1i(glGetUniformLocation(shader_, "gpuParticles"), 1);
    glUniform1i(glGetUniformLocation(shader_, "renderMode"), 2);
    glUniform1i(glGetUniformLocation(shader_, "opaquePass"), 0);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, GLsizei(GpuSimulation::capacity_));

    if (draw_vapor) {
        glUniform1i(glGetUniformLocation(shader_, "renderMode"), 3);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, GLsizei(GpuSimulation::capacity_));
    }

    glUniform1i(glGetUniformLocation(shader_, "gpuParticles"), 1);
    glUniform1i(glGetUniformLocation(shader_, "renderMode"), 1);
    glUniform1i(glGetUniformLocation(shader_, "opaquePass"), 0);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, GLsizei(GpuSimulation::capacity_));
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
}

void Volcanoes::draw_vapor(const Mat4& vp,
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
    float time) {
    prepare_draw(vp,
        light_vp,
        shadow_map,
        reflection_texture,
        reflection_vp,
        right,
        up,
        eye,
        sun,
        daylight,
        atmosphere_opacity,
        time,
        false,
        false,
        0.0f,
        1.0f);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    glUniform1i(glGetUniformLocation(shader_, "gpuParticles"), 1);
    glUniform1i(glGetUniformLocation(shader_, "renderMode"), 3);
    glUniform1i(glGetUniformLocation(shader_, "opaquePass"), 0);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, GLsizei(GpuSimulation::capacity_));
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
}

Lightning::Lightning(const std::filesystem::path& directory) {
    shader_ = program(directory, "lightning");
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(Segment), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 4, GL_FLOAT, GL_FALSE, sizeof(Segment), reinterpret_cast<void*>(offsetof(Segment, end)));
    glVertexAttribDivisor(0, 1);
    glVertexAttribDivisor(1, 1);
}

Lightning::~Lightning() {
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(shader_);
}

float Lightning::range(float low, float high) {
    return std::uniform_real_distribution<float>(low, high)(random_);
}

std::vector<Vec3> Lightning::path(Vec3 start, Vec3 end, int count, float jitter) {
    std::vector<Vec3> points;
    points.reserve(size_t(count + 1));
    points.push_back(start);
    for (int i = 1; i < count; ++i) {
        float t = float(i) / count;
        float envelope = std::sin(pi * t);
        Vec3 point = start + (end - start) * t;
        point = point + Vec3{ range(-jitter, jitter) * envelope,
            range(-jitter * 0.3f, jitter * 0.3f) * envelope,
            range(-jitter, jitter) * envelope };
        points.push_back(point);
    }
    points.push_back(end);
    return points;
}

void Lightning::append_path(Strike& strike,
    const std::vector<Vec3>& points,
    float strength,
    float width) {
    for (size_t i = 1; i < points.size(); ++i)
        strike.segments.push_back(
            { points[i - 1], strength * range(0.82f, 1.0f), points[i], width });
}

void Lightning::spawn(const Terrain& terrain,
    float water_level,
    float cloud_base,
    float cloud_top,
    Volcanoes& particles) {
    Vec3 origin{ range(-820, 820),
        range(cloud_base + 0.33f * (cloud_top - cloud_base), cloud_top - 30.0f),
        range(-820, 820) };
    float target_x = std::clamp(origin.x + range(-180, 180), -980.0f, 980.0f);
    float target_z = std::clamp(origin.z + range(-180, 180), -980.0f, 980.0f);
    spawn_between(terrain, origin, { target_x, 0, target_z }, water_level, particles);
}

void Lightning::spawn_at(const Terrain& terrain,
    Vec3 target,
    float water_level,
    float cloud_base,
    float cloud_top,
    Volcanoes& particles) {
    target.x = std::clamp(target.x, -980.0f, 980.0f);
    target.z = std::clamp(target.z, -980.0f, 980.0f);
    Vec3 origin{ std::clamp(target.x + range(-180, 180), -820.0f, 820.0f),
        range(cloud_base + 0.33f * (cloud_top - cloud_base), cloud_top - 30.0f),
        std::clamp(target.z + range(-180, 180), -820.0f, 820.0f) };
    spawn_between(terrain, origin, target, water_level, particles);
}

void Lightning::spawn_between(
    const Terrain& terrain, Vec3 origin, Vec3 target, float water_level, Volcanoes& particles) {
    target.x = std::clamp(target.x, -980.0f, 980.0f);
    target.z = std::clamp(target.z, -980.0f, 980.0f);
    Vec3 normal;
    float ground = terrain.surface(target.x, target.z, normal);
    bool hits_water = ground < water_level;
    target.y = (hits_water ? water_level : ground) + 1.0f;
    Strike strike;
    auto main_path = path(origin, target, 22, 24.0f);
    append_path(strike, main_path, 1.0f, 0.85f);
    int branch_count = int(range(1.0f, 5.0f));
    for (int branch = 0; branch < branch_count; ++branch) {
        int index = int(range(3.0f, float(main_path.size() - 4)));
        Vec3 start = main_path[size_t(index)];
        Vec3 end = start + Vec3{ range(-120, 120), -range(55, 150), range(-120, 120) };
        float ground = terrain.surface(
            std::clamp(end.x, -999.0f, 999.0f), std::clamp(end.z, -999.0f, 999.0f), normal);
        end.y = std::max(end.y, ground + 8.0f);
        append_path(strike, path(start, end, int(range(5.0f, 9.0f)), 13.0f), 0.55f, 0.48f);
    }
    strikes_.push_back(std::move(strike));
    if (hits_water) {
        size_t vapor_count = size_t(std::uniform_int_distribution<int>(100, 200)(random_));
        particles.lightning_water_impact(target, vapor_count);
    }
}

double Lightning::interval() {
    double rate = std::max(double(frequency_) / 60.0, 1e-6);
    return std::exponential_distribution<double>(rate)(random_);
}

float Lightning::frequency() const {
    return frequency_;
}

void Lightning::set_frequency(float frequency) {
    frequency_ = frequency;
}

void Lightning::update(const Terrain& terrain,
    double elapsed,
    float water_level,
    float cloud_base,
    float cloud_top,
    Volcanoes& particles) {
    for (auto& strike : strikes_)
        strike.age += float(elapsed);
    strikes_.erase(std::remove_if(strikes_.begin(),
                       strikes_.end(),
                       [](const Strike& strike) { return strike.age >= 0.4f; }),
        strikes_.end());
    if (frequency_ != scheduled_frequency_) {
        scheduled_frequency_ = frequency_;
        until_next_ = -1;
    }
    if (frequency_ <= 0) {
        until_next_ = -1;
        return;
    }
    if (until_next_ < 0)
        until_next_ = interval();
    until_next_ -= elapsed;
    while (until_next_ <= 0) {
        spawn(terrain, water_level, cloud_base, cloud_top, particles);
        until_next_ += interval();
    }
}

void Lightning::draw(const Mat4& vp,
    Vec3 eye,
    Vec3 camera_right,
    bool clip_enabled,
    float clip_height,
    float clip_direction) {
    visible_segments_.clear();
    for (const auto& strike : strikes_) {
        float fade = 1.0f - smooth(0.1f, 0.4f, strike.age);
        for (auto segment : strike.segments) {
            segment.strength *= fade;
            visible_segments_.push_back(segment);
        }
    }
    if (visible_segments_.empty())
        return;
    glUseProgram(shader_);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "viewProjection"), 1, GL_FALSE, vp.data());
    uniform(shader_, "eye", eye);
    uniform(shader_, "cameraRight", camera_right);
    glUniform1i(glGetUniformLocation(shader_, "clipEnabled"), clip_enabled ? 1 : 0);
    uniform(shader_, "clipHeight", clip_height);
    uniform(shader_, "clipDirection", clip_direction);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
        GLsizeiptr(visible_segments_.size() * sizeof(Segment)),
        visible_segments_.data(),
        GL_STREAM_DRAW);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_ONE, GL_ONE);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, GLsizei(visible_segments_.size()));
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

Rain::Rain(const std::filesystem::path& directory) {
    compute_ = compute_program(directory, "rain_compute");
    try {
        shader_ = program(directory, "rain");
    } catch (...) {
        glDeleteProgram(compute_);
        throw;
    }
    glGenBuffers(1, &drops_);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, drops_);
    std::vector<GpuDrop> inactive(capacity_);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        GLsizeiptr(inactive.size() * sizeof(GpuDrop)),
        inactive.data(),
        GL_DYNAMIC_DRAW);
    glGenBuffers(1, &wetness_);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, wetness_);
    std::vector<uint32_t> dry(wetness_size_ * wetness_size_);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
        GLsizeiptr(dry.size() * sizeof(uint32_t)),
        dry.data(),
        GL_DYNAMIC_DRAW);
    glGenVertexArrays(1, &vao_);
}

Rain::~Rain() {
    glDeleteBuffers(1, &wetness_);
    glDeleteBuffers(1, &drops_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(shader_);
    glDeleteProgram(compute_);
}

float Rain::intensity() const {
    return intensity_;
}

void Rain::set_intensity(float intensity) {
    intensity_ = intensity;
}

void Rain::update(double elapsed,
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
    GLuint next) {
    constexpr float dt = 1.0f / 120.0f;
    accumulator_ += elapsed;
    glUseProgram(compute_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, drops_);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, main_particles);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, heads);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, next);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, wetness_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, terrain_texture);
    glUniform1i(glGetUniformLocation(compute_, "terrainHeight"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, cloud_density);
    glUniform1i(glGetUniformLocation(compute_, "cloudDensityTexture"), 1);
    glUniform1ui(glGetUniformLocation(compute_, "capacity"), capacity_);
    uniform(compute_, "intensity", clouds_enabled ? intensity_ : 0.0f);
    uniform(compute_, "cloudCoverage", cloud_coverage);
    uniform(compute_, "waterLevel", water_level);
    uniform(compute_, "windSpeed", wind_speed);
    glUniform2f(
        glGetUniformLocation(compute_, "windDirection"), wind_direction.x, wind_direction.z);
    uniform(compute_, "cloudBase", cloud_base);
    uniform(compute_, "cloudTop", cloud_top);
    uniform(compute_, "time", time);
    uniform(compute_, "eye", eye);
    glUniform1f(glGetUniformLocation(compute_, "dt"), dt);
    auto run = [&](int pass, uint32_t count) {
        glUniform1i(glGetUniformLocation(compute_, "pass"), pass);
        glDispatchCompute((count + 127) / 128, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    };
    while (accumulator_ >= dt) {
        accumulator_ -= dt;
        glUniform1ui(glGetUniformLocation(compute_, "frameSeed"), frame_seed_++);
        run(0, wetness_size_ * wetness_size_);
        run(1, capacity_);
    }
    glActiveTexture(GL_TEXTURE0);
}

void Rain::bind_wetness() const {
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, wetness_);
}

void Rain::draw(const Mat4& vp,
    Vec3 eye,
    Vec3 right,
    Vec3 up,
    float daylight,
    bool clip_enabled,
    float clip_height,
    float clip_direction) {
    glUseProgram(shader_);
    glUniformMatrix4fv(glGetUniformLocation(shader_, "viewProjection"), 1, GL_FALSE, vp.data());
    uniform(shader_, "eye", eye);
    uniform(shader_, "cameraRight", right);
    uniform(shader_, "cameraUp", up);
    uniform(shader_, "daylight", daylight);
    glUniform1i(glGetUniformLocation(shader_, "clipEnabled"), clip_enabled ? 1 : 0);
    uniform(shader_, "clipHeight", clip_height);
    uniform(shader_, "clipDirection", clip_direction);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, drops_);
    glBindVertexArray(vao_);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, GLsizei(capacity_));
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

} // namespace earth_sim
