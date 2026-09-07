#include "Application.hpp"

#include "Rendering.hpp"

#include <imgui_impl_opengl3.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace earth_sim {
namespace {
    GLuint load_texture(const std::filesystem::path& file) {
        int width = 0;
        int height = 0;
        int components = 0;
        unsigned char* pixels = stbi_load(file.string().c_str(), &width, &height, &components, 4);
        if (!pixels)
            throw std::runtime_error(
                "Cannot load image " + file.string() + ": " + stbi_failure_reason());

        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        stbi_image_free(pixels);
        return texture;
    }

    std::filesystem::path asset_file(const std::filesystem::path& shader_directory,
        const char* name) {
        auto adjacent = shader_directory.parent_path() / "assets" / name;
        if (std::filesystem::is_regular_file(adjacent))
            return adjacent;
        auto working_directory = std::filesystem::absolute(std::filesystem::path("assets") / name);
        if (std::filesystem::is_regular_file(working_directory))
            return working_directory;
        throw std::runtime_error("Cannot find UI asset: " + std::string(name));
    }

    bool world_to_screen(Vec3 position,
        Vec3 eye,
        Vec3 forward,
        Vec3 right,
        Vec3 up,
        ImVec2 display_size,
        ImVec2& screen) {
        Vec3 relative = position - eye;
        float depth = dot(relative, forward);
        if (depth <= 0.01f)
            return false;

        constexpr float tan_half_fov = 0.41421356237f;
        float half_height = std::max(display_size.y * 0.5f, 1.0f);
        float focal_length = half_height / tan_half_fov;
        screen = { display_size.x * 0.5f + dot(relative, right) * focal_length / depth,
            display_size.y * 0.5f - dot(relative, up) * focal_length / depth };
        return screen.x >= 0 && screen.x <= display_size.x && screen.y >= 0 &&
               screen.y <= display_size.y;
    }

    ImVec2 source_marker_center(ImVec2 anchor) {
        return { anchor.x, anchor.y - 12.0f * ui_scale };
    }

    void draw_source_marker(ImDrawList* draw_list, ImVec2 anchor, bool spring, bool selected) {
        float scale = ui_scale;
        float radius = 8.0f * scale;
        ImVec2 center = source_marker_center(anchor);
        ImU32 shadow = IM_COL32(0, 0, 0, 150);
        ImU32 fill = spring ? IM_COL32(35, 164, 230, 245) : IM_COL32(235, 82, 30, 245);
        ImU32 accent = spring ? IM_COL32(225, 249, 255, 255) : IM_COL32(255, 220, 70, 255);

        draw_list->AddTriangleFilled({ center.x - 4.0f * scale, center.y + 5.0f * scale },
            { center.x + 4.0f * scale, center.y + 5.0f * scale },
            { anchor.x, anchor.y + 2.0f * scale },
            shadow);
        draw_list->AddCircleFilled({ center.x + scale, center.y + scale }, radius, shadow, 16);
        draw_list->AddTriangleFilled({ center.x - 3.5f * scale, center.y + 5.0f * scale },
            { center.x + 3.5f * scale, center.y + 5.0f * scale },
            anchor,
            fill);
        draw_list->AddCircleFilled(center, radius, fill, 16);

        if (spring) {
            for (int row = -1; row <= 1; ++row) {
                float y = center.y + row * 3.0f * scale;
                draw_list->AddLine({ center.x - 4.5f * scale, y },
                    { center.x + 4.5f * scale, y },
                    accent,
                    1.2f * scale);
            }
        } else {
            ImVec2 flame_center{ center.x, center.y + 1.5f * scale };
            draw_list->AddTriangleFilled({ center.x - 4.0f * scale, center.y + 4.0f * scale },
                { center.x + 4.0f * scale, center.y + 4.0f * scale },
                { center.x + 1.0f * scale, center.y - 5.0f * scale },
                accent);
            draw_list->AddCircleFilled(flame_center, 3.7f * scale, accent, 12);
        }
        if (selected)
            draw_list->AddCircle(center, radius + 2.0f * scale, IM_COL32_WHITE, 20, 2.0f * scale);
    }
} // namespace

class AppState {
    GLFWwindow* window_;
    std::filesystem::path directory_;
    TerrainRenderer terrain_renderer_;
    SkyRenderer sky_renderer_;
    AircraftRenderer aircraft_renderer_;
    WaterRenderer water_renderer_;
    Terrain terrain_;
    Volcanoes volcanoes_;
    Lightning lightning_;
    Clouds clouds_;
    ExplosionClouds explosions_;
    Rain rain_;
    TerrainShadows shadows_;
    ScreenSpaceGI screen_space_gi_;
    PlanarReflection reflection_;
    Bloom bloom_;

    float yaw_ = 0.65f;
    float pitch_ = 0.48f;
    float distance_ = 1050;
    float atmosphere_opacity_ = 0.4f;
    float water_level_ = 25.f;
    float cloud_coverage_ = 0.5f;
    float cloud_opacity_ = 0.4f;
    float wind_speed_ = 10.0f;
    float wind_direction_ = std::atan2(0.35f, 0.85f);
    float cloud_base_ = 290.0f;
    static constexpr float cloud_thickness_ = 240.0f;
    float camera_exposure_ = 0.0f;
    float bloom_intensity_ = 0.15f;
    bool clouds_enabled_ = true;
    bool cloud_shadows_enabled_ = false;
    bool god_rays_enabled_ = false;
    bool cloud_simulation_enabled_ = true;
    bool explosion_simulation_enabled_ = true;
    bool particle_simulation_enabled_ = true;
    bool water_simulation_enabled_ = true;
    bool bloom_enabled_ = true;
    bool ssgi_enabled_ = false;
    bool camera_shake_enabled_ = true;
    bool source_icons_visible_ = true;
    bool flying_ = false;
    bool aircraft_destroyed_ = false;
    Vec3 aircraft_position_{};
    Vec3 aircraft_velocity_{ 0, 0, 95 };
    Vec3 aircraft_forward_{ 0, 0, 1 };
    Vec3 aircraft_right_{ -1, 0, 0 };
    Vec3 aircraft_up_{ 0, 1, 0 };
    Vec3 flight_camera_offset_{ 0, 18, -52 };
    Vec3 flight_camera_center_{};
    Vec3 flight_camera_horizontal_right_{ -1, 0, 0 };
    float flight_camera_yaw_offset_ = 0.0f;
    float flight_camera_pitch_offset_ = 0.0f;
    float flight_camera_distance_ = 55.0273f;
    float aircraft_vapor_emission_ = 0.0f;
    float time_speed_ = 1.0f;
    float day_phase_offset_ = 0.34f;
    bool day_night_paused_ = false;
    float meteor_size_ = 1.0f;
    float explosion_size_ = 1.0f;
    static constexpr float explosion_size_scale_ = 0.65f;
    float camera_shake_strength_ = 0.0f;
    double camera_shake_time_ = 0.0;
    Vec3 target_{ 0, 50, 0 };
    bool panning_ = false;
    bool rotating_ = false;
    enum class PlacementTool {
        None,
        Volcano,
        Spring,
        Meteor,
        Explosion,
        TornadoOrigin,
        TornadoDirection,
        TerrainUp,
        TerrainDown,
        FlattenTerrain,
        RoughenTerrain,
        AddWater,
        AddLava
    };
    enum class SourceType { None, Lava, Spring };
    PlacementTool placement_ = PlacementTool::None;
    SourceType selected_source_type_ = SourceType::None;
    size_t selected_source_index_ = 0;
    Vec3 tornado_origin_{};
    bool placement_miss_ = false;
    float brush_radius_ = 85.0f;
    float terrain_step_magnitude_ = 1.0f;
    float particle_spacing_ = 3.0f;
    GLuint placement_tools_texture_ = 0;
    double previous_;
    double simulation_time_ = 0.0;
    double day_time_ = 0.0;

public:
    AppState(GLFWwindow* window, const char* executable_path)
        : window_(window)
        , directory_(shader_directory(executable_path))
        , terrain_renderer_(directory_)
        , sky_renderer_(directory_)
        , aircraft_renderer_(directory_)
        , water_renderer_(directory_)
        , terrain_(random_terrain_seed())
        , volcanoes_(directory_, terrain_)
        , lightning_(directory_)
        , clouds_(directory_, volcanoes_.terrain_texture())
        , explosions_(directory_, volcanoes_.terrain_texture())
        , rain_(directory_)
        , shadows_(directory_)
        , screen_space_gi_(directory_)
        , reflection_()
        , bloom_(directory_)
        , previous_(glfwGetTime()) {
        placement_tools_texture_ = load_texture(asset_file(directory_, "placement-tools.png"));
    }

    ~AppState() {
        if (placement_tools_texture_)
            glDeleteTextures(1, &placement_tools_texture_);
    }

    void frame();
};

void AppState::frame() {

    double now = glfwGetTime();
    // Bound stall recovery consistently for the sky, clouds, and particle physics.
    double frame_elapsed = std::clamp(now - previous_, 0.0, 0.1);
    double elapsed = frame_elapsed * double(time_speed_);
    previous_ = now;
    simulation_time_ += elapsed;
    if (!day_night_paused_)
        day_time_ += elapsed;
    Vec3 wind_direction{ std::cos(wind_direction_), 0.0f, std::sin(wind_direction_) };
    float cloud_top = cloud_base_ + cloud_thickness_;
    if (particle_simulation_enabled_)
        volcanoes_.update(terrain_, elapsed, water_level_, wind_speed_, wind_direction);
    auto impact_strengths = volcanoes_.take_impact_strengths();
    if (camera_shake_enabled_) {
        for (float strength : impact_strengths)
            camera_shake_strength_ =
                std::sqrt(camera_shake_strength_ * camera_shake_strength_ + strength * strength);
    } else
        camera_shake_strength_ = 0.0f;
    camera_shake_time_ += frame_elapsed;
    camera_shake_strength_ *= std::exp(-5.0f * float(frame_elapsed));
    water_renderer_.update(elapsed,
        volcanoes_.take_water_impacts(),
        water_simulation_enabled_,
        wind_speed_,
        wind_direction,
        water_level_,
        volcanoes_.terrain_texture());
    lightning_.update(terrain_, elapsed, water_level_, cloud_base_, cloud_top, volcanoes_);
    static const std::vector<Volcanoes::Meteor> no_moving_meteors;
    if (cloud_simulation_enabled_ && !flying_)
        clouds_.update(elapsed,
            float(simulation_time_),
            particle_simulation_enabled_ ? volcanoes_.meteors() : no_moving_meteors,
            volcanoes_.particle_buffer(),
            particle_simulation_enabled_,
            wind_speed_,
            wind_direction,
            cloud_base_,
            cloud_top);
    if (explosion_simulation_enabled_)
        explosions_.update(elapsed, wind_speed_, wind_direction);
    int framebuffer_width = 0;
    int framebuffer_height = 0;
    glfwGetFramebufferSize(window_, &framebuffer_width, &framebuffer_height);
    if (framebuffer_width <= 0 || framebuffer_height <= 0) {
        glfwWaitEventsTimeout(0.05);
        return;
    }
    // Keep UI at native resolution while rendering the HDR scene at half width
    // and height. The final tone-map pass performs the upscale.
    int w = std::max(1, (framebuffer_width + 1) / 2);
    int h = std::max(1, (framebuffer_height + 1) / 2);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    auto& io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (placement_ != PlacementTool::None) {
            placement_ = PlacementTool::None;
            placement_miss_ = false;
        }
    }
    bool place_click = !flying_ && placement_ != PlacementTool::None && !io.WantCaptureMouse &&
                       ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool target_click = !flying_ && placement_ == PlacementTool::None && !io.WantCaptureMouse &&
                        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if (placement_ != PlacementTool::None && !io.WantCaptureMouse)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (!io.WantCaptureMouse) {
        if (!flying_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            placement_ == PlacementTool::None &&
            !target_click)
            panning_ = true;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            rotating_ = true;
    }
    if (target_click)
        panning_ = false;
    bool focused = glfwGetWindowAttrib(window_, GLFW_FOCUSED) != 0;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || !focused)
        panning_ = false;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) || !focused)
        rotating_ = false;
    if (rotating_) {
        if (flying_) {
            flight_camera_yaw_offset_ =
                std::remainder(flight_camera_yaw_offset_ - io.MouseDelta.x * 0.005f, 2 * pi);
            flight_camera_pitch_offset_ =
                std::remainder(flight_camera_pitch_offset_ - io.MouseDelta.y * 0.005f, 2 * pi);
        } else {
            yaw_ = std::remainder(yaw_ - io.MouseDelta.x * 0.005f, 2 * pi);
            pitch_ = std::remainder(pitch_ + io.MouseDelta.y * 0.005f, 2 * pi);
        }
    }
    if (!io.WantCaptureMouse) {
        if (flying_) {
            float zoomed = flight_camera_distance_ * std::exp(-io.MouseWheel * 0.12f);
            if (std::isfinite(zoomed))
                flight_camera_distance_ = std::clamp(zoomed, 14.0f, 500.0f);
        } else {
            float zoomed = distance_ * std::exp(-io.MouseWheel * 0.12f);
            // Reject only floating-point overflow/underflow, with no distance limits.
            if (std::isfinite(zoomed) && zoomed > 0)
                distance_ = zoomed;
        }
    }
    Vec3 orbit{
        std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_), std::cos(pitch_) * std::cos(yaw_)
    };
    // An analytic basis stays stable when orbiting through either pole.
    Vec3 forward = orbit * (-1);
    Vec3 right{ std::cos(yaw_), 0, -std::sin(yaw_) };
    Vec3 up = cross(right, forward);
    if (!flying_ && panning_) {
        // Match screen-space dragging at the orbit target, including on HiDPI displays.
        float units_per_pixel = 2 * distance_ * std::tan(pi / 8) / std::max(io.DisplaySize.y, 1.0f);
        target_ = target_ + right * (-io.MouseDelta.x * units_per_pixel) +
                  up * (io.MouseDelta.y * units_per_pixel);
    }
    Vec3 eye = target_ + orbit * distance_;
    if (flying_) {
        float roll = 0.0f;
        float pitch = 0.0f;
        if (!io.WantCaptureKeyboard && focused) {
            roll = (ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f) -
                   (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f);
            pitch = (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f) -
                    (ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f);
        }
        // Advance the aircraft with simulation time so the global time-speed control
        // affects flight, while keeping the chase camera responsive in real time.
        float flight_dt = float(elapsed);
        float camera_dt = float(frame_elapsed);
        auto rotate = [](Vec3 vector, Vec3 axis, float angle) {
            float cosine = std::cos(angle);
            float sine = std::sin(angle);
            return vector * cosine + cross(axis, vector) * sine +
                   axis * (dot(axis, vector) * (1.0f - cosine));
        };
        if (!aircraft_destroyed_) {
            aircraft_forward_ =
                normalize(rotate(aircraft_forward_, aircraft_right_, pitch * 0.78f * flight_dt));
            aircraft_up_ =
                normalize(rotate(aircraft_up_, aircraft_right_, pitch * 0.78f * flight_dt));
            aircraft_right_ =
                normalize(rotate(aircraft_right_, aircraft_forward_, roll * 1.45f * flight_dt));
            aircraft_up_ =
                normalize(rotate(aircraft_up_, aircraft_forward_, roll * 1.45f * flight_dt));
            aircraft_right_ = normalize(cross(aircraft_forward_, aircraft_up_));
            aircraft_up_ = normalize(cross(aircraft_right_, aircraft_forward_));

            float forward_speed = dot(aircraft_velocity_, aircraft_forward_);
            Vec3 lateral_velocity = aircraft_velocity_ - aircraft_forward_ * forward_speed;
            float speed = std::sqrt(dot(aircraft_velocity_, aircraft_velocity_));
            float lift_scale = std::clamp(speed / 95.0f, 0.0f, 1.5f);
            float lift = 22.0f * lift_scale * lift_scale;
            Vec3 velocity_direction =
                speed > 0.001f ? aircraft_velocity_ * (1.0f / speed) : aircraft_forward_;
            Vec3 lift_direction =
                aircraft_up_ - velocity_direction * dot(aircraft_up_, velocity_direction);
            if (dot(lift_direction, lift_direction) > 0.0001f)
                lift_direction = normalize(lift_direction);
            else
                lift_direction = aircraft_up_;
            Vec3 acceleration = aircraft_forward_ * ((95.0f - forward_speed) * 1.8f) -
                                lateral_velocity * 1.25f + lift_direction * lift +
                                Vec3{ 0, -22.0f, 0 };
            aircraft_velocity_ = aircraft_velocity_ + acceleration * flight_dt;
            float alignment = 1.0f - std::exp(-0.9f * flight_dt);
            velocity_direction = normalize(aircraft_velocity_);
            aircraft_forward_ = normalize(aircraft_forward_ * (1.0f - alignment) +
                                          velocity_direction * alignment);
            aircraft_right_ = normalize(cross(aircraft_forward_, aircraft_up_));
            aircraft_up_ = normalize(cross(aircraft_right_, aircraft_forward_));
            Vec3 next_position = aircraft_position_ + aircraft_velocity_ * flight_dt;
            const Vec3 collision_points[] = { { 0, -0.75f, 0 },
                { 0, 0, 9 },
                { 0, 0, -7 },
                { -10, 0, -2.2f },
                { 10, 0, -2.2f } };
            float terrain_correction = 0.0f;
            float water_correction = 0.0f;
            Vec3 terrain_impact = next_position;
            Vec3 water_impact = { next_position.x, water_level_, next_position.z };
            for (Vec3 local : collision_points) {
                Vec3 point = next_position + aircraft_right_ * local.x + aircraft_up_ * local.y +
                             aircraft_forward_ * local.z;
                Vec3 terrain_normal;
                float terrain_height = terrain_.surface(point.x, point.z, terrain_normal);
                float point_terrain_correction = terrain_height + 0.5f - point.y;
                if (point_terrain_correction > terrain_correction) {
                    terrain_correction = point_terrain_correction;
                    terrain_impact = { point.x, terrain_height, point.z };
                }
                float point_water_correction = water_level_ + 0.5f - point.y;
                if (point_water_correction > water_correction) {
                    water_correction = point_water_correction;
                    water_impact = { point.x, water_level_, point.z };
                }
            }
            float vertical_correction = std::max(terrain_correction, water_correction);
            if (terrain_correction > 0.0f) {
                aircraft_destroyed_ = true;
                aircraft_velocity_ = {};
                volcanoes_.impact(terrain_, terrain_impact, 0.35f);
            } else if (water_correction > 0.0f) {
                aircraft_destroyed_ = true;
                aircraft_velocity_ = {};
                volcanoes_.water_impact(water_impact, 0.35f);
            }
            if (vertical_correction > 0.0f) {
                next_position.y += vertical_correction;
                aircraft_velocity_.y = std::max(aircraft_velocity_.y, 0.0f);
            }
            aircraft_position_ = next_position;

            if (!aircraft_destroyed_ && particle_simulation_enabled_) {
                aircraft_vapor_emission_ += 100.0f * flight_dt;
                size_t vapor_pairs = size_t(aircraft_vapor_emission_);
                aircraft_vapor_emission_ -= float(vapor_pairs);
                if (vapor_pairs > 0)
                    volcanoes_.add_aircraft_vapor(aircraft_position_,
                        aircraft_forward_,
                        aircraft_right_,
                        aircraft_up_,
                        vapor_pairs);
            }
        }

        const Vec3 world_up{ 0, 1, 0 };
        Vec3 desired_offset = aircraft_forward_ * -52.0f + world_up * 18.0f;
        desired_offset = normalize(desired_offset) * flight_camera_distance_;
        float rotation_blend = 1.0f - std::exp(-1.6f * camera_dt);
        flight_camera_offset_ =
            normalize(flight_camera_offset_ * (1.0f - rotation_blend) +
                      desired_offset * rotation_blend) *
            flight_camera_distance_;
        float position_blend = 1.0f - std::exp(-4.0f * camera_dt);
        flight_camera_center_ = flight_camera_center_ +
                                (aircraft_position_ - flight_camera_center_) * position_blend;

        // Apply the user orbit after chase smoothing so mouse movement is immediate.
        Vec3 camera_offset =
            flight_camera_center_ - aircraft_position_ + flight_camera_offset_;
        camera_offset = rotate(
            camera_offset, world_up, flight_camera_yaw_offset_);
        Vec3 camera_forward = normalize(camera_offset * -1.0f);
        Vec3 horizontal_right = cross(camera_forward, world_up);
        if (dot(horizontal_right, horizontal_right) > 0.0001f)
            flight_camera_horizontal_right_ = normalize(horizontal_right);
        Vec3 orbit_right = flight_camera_horizontal_right_;
        camera_offset = rotate(
            camera_offset, orbit_right, flight_camera_pitch_offset_);
        camera_forward = normalize(camera_offset * -1.0f);
        Vec3 camera_up = normalize(cross(orbit_right, camera_forward));
        eye = aircraft_position_ + camera_offset;
        float framing_offset = 3.5f * flight_camera_distance_ / 55.0273f;
        Vec3 camera_target = aircraft_position_ + camera_up * framing_offset;
        forward = normalize(camera_target - eye);
        right = normalize(cross(forward, camera_up));
        up = normalize(cross(right, forward));
    }
    if (!flying_ && camera_shake_enabled_ && camera_shake_strength_ > 0.001f) {
        // High-frequency, non-repeating local rotations feel like an impact without
        // disturbing the orbit camera's persistent target, yaw, or pitch.
        float t = float(camera_shake_time_);
        float amplitude = 0.012f * camera_shake_strength_;
        float shake_yaw = std::sin(t * 37.0f) * amplitude;
        float shake_pitch = std::sin(t * 53.0f + 1.7f) * amplitude * 0.8f;
        float shake_roll = std::sin(t * 43.0f + 3.1f) * amplitude * 0.6f;
        forward = normalize(forward + right * shake_yaw + up * shake_pitch);
        Vec3 rolled_up = normalize(up + right * shake_roll);
        right = normalize(cross(forward, rolled_up));
        up = cross(right, forward);
    }
    if (!flying_ && source_icons_visible_ && placement_ == PlacementTool::None &&
        !io.WantCaptureMouse &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        SourceType closest_type = SourceType::None;
        size_t closest_index = 0;
        float closest_distance_squared = std::pow(11.0f * ui_scale, 2.0f);
        auto consider_sources = [&](const std::vector<Vec3>& sources, SourceType type) {
            for (size_t i = 0; i < sources.size(); ++i) {
                ImVec2 anchor;
                if (!world_to_screen(sources[i], eye, forward, right, up, io.DisplaySize, anchor))
                    continue;
                ImVec2 center = source_marker_center(anchor);
                float dx = io.MousePos.x - center.x;
                float dy = io.MousePos.y - center.y;
                float distance_squared = dx * dx + dy * dy;
                if (distance_squared <= closest_distance_squared) {
                    closest_distance_squared = distance_squared;
                    closest_type = type;
                    closest_index = i;
                }
            }
        };
        consider_sources(volcanoes_.lava_sources(), SourceType::Lava);
        consider_sources(volcanoes_.spring_sources(), SourceType::Spring);
        selected_source_type_ = closest_type;
        selected_source_index_ = closest_index;
        if (closest_type != SourceType::None) {
            panning_ = false;
            target_click = false;
        }
    }
    if (!flying_ && !io.WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        if (selected_source_type_ == SourceType::Lava)
            volcanoes_.remove_lava_source(selected_source_index_);
        else if (selected_source_type_ == SourceType::Spring)
            volcanoes_.remove_spring_source(selected_source_index_);
        selected_source_type_ = SourceType::None;
    }
    double wrapped_day = std::fmod(day_time_ / 360.0 + day_phase_offset_, 1.0);
    if (wrapped_day < 0.0)
        wrapped_day += 1.0;
    float day = float(wrapped_day);
    float angle = 2 * pi * (day - 0.25f);
    Vec3 sun = normalize({ std::cos(angle), std::sin(angle), 0.30f * std::cos(angle) });
    float daylight = smooth(-0.15f, 0.22f, sun.y);
    Vec3 fog =
        Vec3{ 0.012f, 0.019f, 0.040f } * (1 - daylight) + Vec3{ 0.42f, 0.59f, 0.72f } * daylight;
    float sunset = std::exp(-std::abs(sun.y) * 10) * 0.32f;
    fog = fog * (1 - sunset) + Vec3{ 0.70f, 0.23f, 0.09f } * sunset;
    float near_plane = flying_ ? std::max(0.5f, flight_camera_distance_ * 0.01f)
                               : std::max(0.001f, distance_ * 0.0001f);
    float far_plane = flying_ ? 6000.0f : std::max(6000.0f, distance_ + 4000.0f);
    Mat4 projection = perspective(float(w) / float(h), near_plane, far_plane);
    Mat4 vp = multiply(projection, look_at(eye, forward, right, up));
    if (particle_simulation_enabled_)
        rain_.update(elapsed,
            clouds_enabled_,
            cloud_coverage_,
            eye,
            water_level_,
            float(simulation_time_),
            wind_speed_,
            wind_direction,
            cloud_base_,
            cloud_top,
            volcanoes_.terrain_texture(),
            clouds_.density_texture(),
            volcanoes_.particle_buffer(),
            volcanoes_.particle_head_buffer(),
            volcanoes_.particle_next_buffer());

    shadows_.render(terrain_, sun);
    clouds_.update_shadow(sun,
        cloud_opacity_,
        cloud_coverage_,
        cloud_base_,
        cloud_top,
        clouds_enabled_ && (cloud_shadows_enabled_ || god_rays_enabled_));
    reflection_.begin(w, h);
    Vec3 reflected_eye{ eye.x, 2.0f * water_level_ - eye.y, eye.z };
    Vec3 reflected_forward{ forward.x, -forward.y, forward.z };
    Vec3 reflected_up{ up.x, -up.y, up.z };
    // Preserve horizontal screen orientation. The reflected basis is intentionally
    // mirrored; reflection rendering disables face culling below.
    Vec3 reflected_right{ right.x, -right.y, right.z };
    Mat4 reflection_projection = perspective(
        float(reflection_.width()) / float(reflection_.height()), near_plane, far_plane);
    Mat4 reflection_vp = multiply(reflection_projection,
        look_at(reflected_eye, reflected_forward, reflected_right, reflected_up));
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    sky_renderer_.draw(reflected_forward,
        reflected_right,
        reflected_up,
        sun,
        fog,
        daylight,
        atmosphere_opacity_,
        float(simulation_time_),
        float(reflection_.width()) / float(reflection_.height()));
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CLIP_DISTANCE0);
    terrain_renderer_.draw(terrain_,
        shadows_,
        rain_,
        volcanoes_.scorched_texture(),
        clouds_.shadow_texture(),
        cloud_shadows_enabled_,
        reflection_vp,
        reflected_eye,
        sun,
        fog,
        daylight,
        atmosphere_opacity_,
        true,
        water_level_,
        eye.y >= water_level_ ? 1.0f : -1.0f);
    if (flying_ && !aircraft_destroyed_) {
        glDisable(GL_CLIP_DISTANCE0);
        aircraft_renderer_.draw(reflection_vp,
            aircraft_position_,
            aircraft_forward_,
            aircraft_right_,
            aircraft_up_,
            sun,
            daylight,
            true);
        glEnable(GL_CLIP_DISTANCE0);
    }
    float reflection_clip_direction = eye.y >= water_level_ ? 1.0f : -1.0f;
    volcanoes_.draw(reflection_vp,
        shadows_.light_matrix(0),
        shadows_.depth_texture(),
        0,
        reflection_vp,
        reflected_right,
        reflected_up,
        reflected_eye,
        sun,
        daylight,
        atmosphere_opacity_,
        float(simulation_time_),
        true,
        true,
        true,
        water_level_,
        reflection_clip_direction);
    rain_.draw(reflection_vp,
        reflected_eye,
        reflected_right,
        reflected_up,
        daylight,
        true,
        water_level_,
        reflection_clip_direction);
    glDisable(GL_CLIP_DISTANCE0);
    if (clouds_enabled_)
        clouds_.draw_reflection(reflection_.framebuffer(),
            reflection_.depth_texture(),
            reflection_.width(),
            reflection_.height(),
            reflected_eye,
            reflected_forward,
            reflected_right,
            reflected_up,
            sun,
            fog,
            daylight,
            atmosphere_opacity_,
            cloud_opacity_,
            float(simulation_time_),
            cloud_coverage_,
            wind_speed_,
            wind_direction,
            cloud_base_,
            cloud_top,
            reflection_projection);
    glEnable(GL_CLIP_DISTANCE0);
    lightning_.draw(reflection_vp,
        reflected_eye,
        reflected_right,
        true,
        water_level_,
        reflection_clip_direction);
    glDisable(GL_CLIP_DISTANCE0);
    bloom_.resize(w, h);
    // HDR scene/depth are required even when the optional cloud pass is disabled.
    clouds_.begin_scene(w, h);
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    sky_renderer_.draw(forward,
        right,
        up,
        sun,
        fog,
        daylight,
        atmosphere_opacity_,
        float(simulation_time_),
        float(w) / float(h));
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    terrain_renderer_.draw(terrain_,
        shadows_,
        rain_,
        volcanoes_.scorched_texture(),
        clouds_.shadow_texture(),
        cloud_shadows_enabled_,
        vp,
        eye,
        sun,
        fog,
        daylight,
        atmosphere_opacity_);
    if (flying_ && !aircraft_destroyed_)
        aircraft_renderer_.draw(vp,
            aircraft_position_,
            aircraft_forward_,
            aircraft_right_,
            aircraft_up_,
            sun,
            daylight);
    if (place_click || target_click) {
        // Read only for a surface action, before particles/clouds/UI are drawn.
        // The scene depth selects the visible triangle, including mountain occlusion.
        int px = int(std::floor(io.MousePos.x * float(w) / io.DisplaySize.x));
        int py = h - 1 - int(std::floor(io.MousePos.y * float(h) / io.DisplaySize.y));
        if (place_click)
            placement_miss_ = true;
        if (px >= 0 && px < w && py >= 0 && py < h) {
            float depth = 1;
            glReadPixels(px, py, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
            if (depth < 1) {
                float sx = 2 * (float(px) + 0.5f) / w - 1;
                float sy = 2 * (float(py) + 0.5f) / h - 1;
                Vec3 ray = forward + right * (sx * float(w) / h * std::tan(pi / 8)) +
                           up * (sy * std::tan(pi / 8));
                float view_distance = projection[14] / (2 * depth - 1 + projection[10]);
                Vec3 position = eye + ray * view_distance;
                position.x = std::clamp(position.x, -1000.0f, 1000.0f);
                position.z = std::clamp(position.z, -1000.0f, 1000.0f);
                Vec3 normal;
                position.y = terrain_.surface(position.x, position.z, normal);
                if (target_click)
                    target_ = position;
                else if (placement_ == PlacementTool::TerrainUp ||
                         placement_ == PlacementTool::TerrainDown ||
                         placement_ == PlacementTool::FlattenTerrain ||
                         placement_ == PlacementTool::RoughenTerrain) {
                    if (placement_ == PlacementTool::FlattenTerrain)
                        terrain_.flatten(position, brush_radius_, terrain_step_magnitude_);
                    else if (placement_ == PlacementTool::RoughenTerrain)
                        terrain_.roughen(position, brush_radius_, terrain_step_magnitude_);
                    else {
                        float elevation =
                            (placement_ == PlacementTool::TerrainUp ? 18.0f : -18.0f) *
                            terrain_step_magnitude_;
                        terrain_.deform(position, brush_radius_, elevation);
                    }
                    volcanoes_.terrain_changed(terrain_);
                    placement_miss_ = false;
                } else if (placement_ == PlacementTool::AddWater ||
                           placement_ == PlacementTool::AddLava) {
                    if (placement_ == PlacementTool::AddWater)
                        volcanoes_.add_water(terrain_, position, brush_radius_, particle_spacing_);
                    else
                        volcanoes_.add_lava(terrain_, position, brush_radius_, particle_spacing_);
                    placement_miss_ = false;
                } else if (placement_ == PlacementTool::TornadoOrigin) {
                    tornado_origin_ = position;
                    placement_ = PlacementTool::TornadoDirection;
                    placement_miss_ = false;
                } else if (placement_ == PlacementTool::TornadoDirection) {
                    Vec3 direction = position - tornado_origin_;
                    float horizontal_distance_squared =
                        direction.x * direction.x + direction.z * direction.z;
                    placement_miss_ = false;
                    if (horizontal_distance_squared >= 25.0f) {
                        volcanoes_.add_tornado(tornado_origin_, position, water_level_);
                        placement_ = PlacementTool::None;
                        placement_miss_ = false;
                    }
                } else {
                    if (placement_ == PlacementTool::Volcano)
                        volcanoes_.add_volcano(position);
                    else if (placement_ == PlacementTool::Spring)
                        volcanoes_.add_spring(position);
                    else if (placement_ == PlacementTool::Meteor)
                        volcanoes_.launch_meteor(position, meteor_size_);
                    else if (placement_ == PlacementTool::Explosion) {
                        float physical_size = explosion_size_ * explosion_size_scale_;
                        volcanoes_.impact(terrain_, position, physical_size);
                        volcanoes_.explosion_water_impact(
                            { position.x, water_level_, position.z }, physical_size);
                        explosions_.explode(position, physical_size);
                    }
                    if (placement_ != PlacementTool::Meteor)
                        placement_ = PlacementTool::None;
                    placement_miss_ = false;
                }
            }
        }
    }
    water_renderer_.draw(vp,
        reflection_vp,
        reflection_.color_texture(),
        volcanoes_.terrain_texture(),
        clouds_.shadow_texture(),
        cloud_shadows_enabled_,
        eye,
        sun,
        fog,
        daylight,
        atmosphere_opacity_,
        water_level_,
        float(simulation_time_),
        water_simulation_enabled_);
    bool vapor_after_clouds = clouds_enabled_ && eye.y < cloud_base_;
    if (!vapor_after_clouds)
        rain_.draw(vp, eye, right, up, daylight);
    if (ssgi_enabled_)
        clouds_.begin_emission();
    volcanoes_.draw(vp,
        shadows_.light_matrix(0),
        shadows_.depth_texture(),
        reflection_.color_texture(),
        reflection_vp,
        right,
        up,
        eye,
        sun,
        daylight,
        atmosphere_opacity_,
        float(simulation_time_),
        !vapor_after_clouds);
    if (ssgi_enabled_) {
        clouds_.end_emission();
        screen_space_gi_.draw(clouds_.scene_framebuffer(),
            clouds_.scene_depth_texture(),
            clouds_.scene_emission_texture(),
            w,
            h,
            eye,
            forward,
            right,
            up,
            projection);
    }
    explosions_.draw(clouds_.scene_framebuffer(),
        clouds_.scene_depth_texture(),
        w,
        h,
        eye,
        forward,
        right,
        up,
        sun,
        daylight,
        atmosphere_opacity_,
        projection);
    if (clouds_enabled_) {
        clouds_.draw(eye,
            forward,
            right,
            up,
            sun,
            fog,
            daylight,
            atmosphere_opacity_,
            cloud_opacity_,
            float(simulation_time_),
            cloud_coverage_,
            wind_speed_,
            wind_direction,
            cloud_base_,
            cloud_top,
            god_rays_enabled_,
            projection,
            bloom_.hdr_framebuffer());
        glBindFramebuffer(GL_FRAMEBUFFER, bloom_.hdr_framebuffer());
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, clouds_.scene_depth_texture(), 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glViewport(0, 0, w, h);
        if (vapor_after_clouds)
            volcanoes_.draw_vapor(vp,
                shadows_.light_matrix(0),
                shadows_.depth_texture(),
                reflection_.color_texture(),
                reflection_vp,
                right,
                up,
                eye,
                sun,
                daylight,
                atmosphere_opacity_,
                float(simulation_time_));
        if (vapor_after_clouds)
            rain_.draw(vp, eye, right, up, daylight);
        lightning_.draw(vp, eye, right);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
    } else
        lightning_.draw(vp, eye, right);
    bloom_.draw(clouds_enabled_ ? bloom_.hdr_color_texture() : clouds_.scene_color_texture(),
        bloom_enabled_,
        std::pow(2.0f, camera_exposure_),
        bloom_intensity_,
        framebuffer_width,
        framebuffer_height);

    if (!flying_ && source_icons_visible_) {
        ImDrawList* source_icons = ImGui::GetBackgroundDrawList();
        for (size_t i = 0; i < volcanoes_.lava_sources().size(); ++i) {
            ImVec2 screen;
            if (world_to_screen(
                    volcanoes_.lava_sources()[i], eye, forward, right, up, io.DisplaySize, screen))
                draw_source_marker(source_icons,
                    screen,
                    false,
                    selected_source_type_ == SourceType::Lava && selected_source_index_ == i);
        }
        for (size_t i = 0; i < volcanoes_.spring_sources().size(); ++i) {
            ImVec2 screen;
            if (world_to_screen(volcanoes_.spring_sources()[i],
                    eye,
                    forward,
                    right,
                    up,
                    io.DisplaySize,
                    screen))
                draw_source_marker(source_icons,
                    screen,
                    true,
                    selected_source_type_ == SourceType::Spring && selected_source_index_ == i);
        }
    }

    constexpr float toolbar_height = 58.0f;
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, toolbar_height * ui_scale), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.90f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * ui_scale, 7.0f * ui_scale));
    ImGui::Begin("Placement toolbar",
        nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoMove);

    if (!flying_) {
        struct ToolButton {
            PlacementTool tool;
            const char* id;
            const char* tooltip;
            int atlas_cell;
        };
        const ToolButton tools[] = {
            { PlacementTool::Volcano, "##volcano", "Create volcano", 0 },
            { PlacementTool::Spring, "##spring", "Create spring", 1 },
            { PlacementTool::Meteor, "##meteor", "Meteor strike", 2 },
            { PlacementTool::Explosion, "##explosion", "Explosion", 3 },
            { PlacementTool::TornadoOrigin, "##tornado", "Create tornado", 4 },
            { PlacementTool::TerrainUp, "##terrain-up", "Raise terrain", 5 },
            { PlacementTool::TerrainDown, "##terrain-down", "Lower terrain", 6 },
            { PlacementTool::FlattenTerrain, "##flatten", "Flatten terrain", 7 },
            { PlacementTool::RoughenTerrain, "##roughen", "Roughen terrain", 8 },
            { PlacementTool::AddWater, "##water", "Add water", 9 },
            { PlacementTool::AddLava, "##lava", "Add lava", 10 },
        };
        const ImTextureID placement_texture =
            static_cast<ImTextureID>(static_cast<intptr_t>(placement_tools_texture_));
        const ImVec2 icon_size(36.0f * ui_scale, 36.0f * ui_scale);
        for (const ToolButton& button : tools) {
            int column = button.atlas_cell % 4;
            int row = button.atlas_cell / 4;
            ImVec2 uv0(float(column) / 4.0f, float(row) / 3.0f);
            ImVec2 uv1(float(column + 1) / 4.0f, float(row + 1) / 3.0f);
            bool selected =
                placement_ == button.tool || (button.tool == PlacementTool::TornadoOrigin &&
                                                 placement_ == PlacementTool::TornadoDirection);
            if (selected)
                ImGui::PushStyleColor(
                    ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::ImageButton(button.id, placement_texture, icon_size, uv0, uv1)) {
                placement_ = button.tool;
                placement_miss_ = false;
                panning_ = false;
            }
            if (selected)
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", button.tooltip);
            ImGui::SameLine();
        }

        ImGui::AlignTextToFramePadding();
        if (placement_ == PlacementTool::None) {
            ImGui::TextDisabled("Select a placement tool");
        } else {
            const char* placement_prompt =
                placement_ == PlacementTool::Volcano     ? "Click terrain to place a volcano."
                : placement_ == PlacementTool::Spring    ? "Click terrain to place a spring."
                : placement_ == PlacementTool::Meteor    ? "Click terrain to target a meteor."
                : placement_ == PlacementTool::Explosion ? "Click terrain to detonate an explosion."
                : placement_ == PlacementTool::TornadoOrigin
                    ? "Click terrain to set the tornado origin."
                : placement_ == PlacementTool::TornadoDirection
                    ? "Click terrain to set the tornado direction."
                : placement_ == PlacementTool::TerrainUp      ? "Click terrain to raise it."
                : placement_ == PlacementTool::TerrainDown    ? "Click terrain to lower it."
                : placement_ == PlacementTool::FlattenTerrain ? "Click terrain to flatten it."
                : placement_ == PlacementTool::RoughenTerrain ? "Click terrain to roughen it."
                : placement_ == PlacementTool::AddWater       ? "Click terrain to add water."
                                                              : "Click terrain to add lava.";
            ImGui::BeginGroup();
            ImGui::TextUnformatted(placement_prompt);
            ImGui::SameLine();
            if (ImGui::SmallButton("Cancel placement")) {
                placement_ = PlacementTool::None;
                placement_miss_ = false;
            }
            if (placement_miss_)
                ImGui::TextColored(ImVec4(1, 0.65f, 0.3f, 1),
                    "No terrain here. Choose a point on the landscape.");
            else if (placement_ == PlacementTool::TornadoDirection)
                ImGui::TextDisabled("Choose a point at least 5 units from the origin.");
            else
                ImGui::TextDisabled("Esc cancels placement.");
            ImGui::EndGroup();
        }
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(
            "Flight mode    W: descend    S: climb    A/D: roll    RMB: camera    Wheel: zoom");
    }
    const ImVec2 close_button_size(36.0f * ui_scale, 36.0f * ui_scale);
    const ImVec2 flight_button_size(62.0f * ui_scale, 36.0f * ui_scale);
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - close_button_size.x -
                                  flight_button_size.x - 20.0f * ui_scale,
        7.0f * ui_scale));
    if (ImGui::Button(flying_ ? "Stop" : "Fly", flight_button_size)) {
        if (flying_) {
            flying_ = false;
            cloud_simulation_enabled_ = true;
        } else {
            cloud_simulation_enabled_ = false;
            placement_ = PlacementTool::None;
            placement_miss_ = false;
            selected_source_type_ = SourceType::None;
            panning_ = false;
            rotating_ = false;
            Vec3 direction{ forward.x, 0, forward.z };
            if (dot(direction, direction) < 0.001f)
                direction = { 0, 0, 1 };
            direction = normalize(direction);
            aircraft_forward_ = direction;
            aircraft_right_ = normalize(cross(aircraft_forward_, Vec3{ 0, 1, 0 }));
            aircraft_up_ = { 0, 1, 0 };
            aircraft_velocity_ = aircraft_forward_ * 95.0f;
            aircraft_destroyed_ = false;
            Vec3 terrain_normal;
            float surface = terrain_.surface(target_.x, target_.z, terrain_normal);
            aircraft_position_ =
                { target_.x, std::max(surface + 160.0f, water_level_ + 120.0f), target_.z };
            flight_camera_distance_ = 55.0273f;
            flight_camera_offset_ =
                normalize(aircraft_forward_ * -52.0f + Vec3{ 0, 18.0f, 0 }) *
                flight_camera_distance_;
            flight_camera_center_ = aircraft_position_;
            flight_camera_horizontal_right_ = aircraft_right_;
            flight_camera_yaw_offset_ = 0.0f;
            flight_camera_pitch_offset_ = 0.0f;
            aircraft_vapor_emission_ = 0.0f;
            flying_ = true;
        }
    }
    ImGui::SetCursorPos(
        ImVec2(ImGui::GetWindowWidth() - close_button_size.x - 10.0f * ui_scale, 7.0f * ui_scale));
    if (ImGui::Button("X", close_button_size))
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    ImGui::End();
    ImGui::PopStyleVar(2);

    ImGui::SetNextWindowPos(
        ImVec2(20 * ui_scale, (toolbar_height + 14.0f) * ui_scale), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.78f);
    // ImGuiViewport* viewport = ImGui::GetMainViewport();
    // ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x / 2, viewport->WorkSize.y - 10),
    // ImGuiCond_Always);
    ImGui::Begin("##EarthSim",
        nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    float time_of_day_hours = day * 24.0f;
    ImGui::SetNextItemWidth(180 * ui_scale);
    if (ImGui::SliderFloat("Time of day",
            &time_of_day_hours,
            0.0f,
            23.999f,
            "%.2f h",
            ImGuiSliderFlags_AlwaysClamp))
        day_phase_offset_ += time_of_day_hours / 24.0f - day;
    if (ImGui::Button(day_night_paused_ ? "Resume day-night cycle" : "Pause day-night cycle"))
        day_night_paused_ = !day_night_paused_;
    ImGui::SetNextItemWidth(180 * ui_scale);
    ImGui::SliderFloat(
        "Time speed", &time_speed_, 0.0f, 4.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
    if (time_speed_ > 0)
        ImGui::TextDisabled("1 day = %.1f real seconds", 60.0f / time_speed_);
    else
        ImGui::TextDisabled("Simulation paused");
    ImGui::SetNextItemWidth(180 * ui_scale);
    ImGui::SliderFloat("Camera exposure",
        &camera_exposure_,
        -10.0f,
        10.0f,
        "%+.1f EV",
        ImGuiSliderFlags_AlwaysClamp);
    ImGui::Checkbox("Camera shake", &camera_shake_enabled_);
    ImGui::SetNextItemWidth(180 * ui_scale);
    ImGui::Checkbox("Bloom", &bloom_enabled_);
    ImGui::SliderFloat(
        "Bloom intensity", &bloom_intensity_, 0.0f, 3.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
    ImGui::Text("Active volcanoes: %d", int(volcanoes_.volcano_count()));
    ImGui::Text("Active springs: %d", int(volcanoes_.spring_count()));
    ImGui::Text("Active tornadoes: %d", int(volcanoes_.tornado_count()));
    if (ImGui::Button("Reset terrain")) {
        terrain_.reseed(random_terrain_seed());
        volcanoes_.reset_for_new_terrain(terrain_);
        selected_source_type_ = SourceType::None;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Generate new terrain and remove all volcanoes, springs, and scorching");
    if (ImGui::Button(source_icons_visible_ ? "Hide source icons" : "Show source icons"))
        source_icons_visible_ = !source_icons_visible_;
    ImGui::SetNextItemWidth(180 * ui_scale);
    ImGui::SliderFloat(
        "Meteor size", &meteor_size_, 0.1f, 5.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(180 * ui_scale);
    ImGui::SliderFloat(
        "Explosion size", &explosion_size_, 0.25f, 5.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
    ImGui::Checkbox("Explosion simulation", &explosion_simulation_enabled_);
    ImGui::SameLine();
    if (ImGui::Button("Clear explosion"))
        explosions_.clear();
    ImGui::SetNextItemWidth(180 * ui_scale);
    ImGui::SliderFloat(
        "Brush radius", &brush_radius_, 10.0f, 300.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(180 * ui_scale);
    ImGui::SliderFloat("Terrain step",
        &terrain_step_magnitude_,
        0.1f,
        5.0f,
        "%.2fx",
        ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(180 * ui_scale);
    ImGui::SliderFloat(
        "Particle spacing", &particle_spacing_, 1.0f, 12.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(180 * ui_scale);
    float lava_spawn_rate = volcanoes_.lava_spawn_rate();
    if (ImGui::DragFloat("Volcano spawn rate", &lava_spawn_rate, 1.0f, 0.0f, 1000.0f, "%.1f / s"))
        volcanoes_.set_lava_spawn_rate(lava_spawn_rate);
    ImGui::SetNextItemWidth(180 * ui_scale);
    float spring_spawn_rate = volcanoes_.spring_spawn_rate();
    if (ImGui::DragFloat("Spring spawn rate", &spring_spawn_rate, 1.0f, 0.0f, 1000.0f, "%.1f / s"))
        volcanoes_.set_spring_spawn_rate(spring_spawn_rate);
    ImGui::SetNextItemWidth(180 * ui_scale);
    float particle_lifetime = volcanoes_.particle_lifetime();
    if (ImGui::DragFloat("Particle life", &particle_lifetime, 1.f, 1.0f, 3600.0f, "%.1f s"))
        volcanoes_.set_particle_lifetime(particle_lifetime);
    ImGui::SetNextItemWidth(180 * ui_scale);
    float particle_brightness = volcanoes_.particle_brightness();
    if (ImGui::SliderFloat("Particle brightness",
            &particle_brightness,
            0.0f,
            100.0f,
            "%.2fx",
            ImGuiSliderFlags_AlwaysClamp))
        volcanoes_.set_particle_brightness(particle_brightness);
    bool erosion_simulation_enabled = volcanoes_.erosion_simulation_enabled();
    if (ImGui::Checkbox("Erosion simulation", &erosion_simulation_enabled))
        volcanoes_.set_erosion_simulation_enabled(erosion_simulation_enabled);
    ImGui::SetNextItemWidth(180 * ui_scale);
    float erosion_speed = volcanoes_.erosion_speed();
    if (ImGui::DragFloat("Erosion speed", &erosion_speed, 1.f, 0.0f, 1000.0f, "%.2fx"))
        volcanoes_.set_erosion_speed(erosion_speed);
    ImGui::Checkbox("Particle simulation", &particle_simulation_enabled_);
    bool particle_interactions = volcanoes_.particle_interactions();
    if (ImGui::Checkbox("Particle interactions", &particle_interactions))
        volcanoes_.set_particle_interactions(particle_interactions);
    ImGui::SetNextItemWidth(180 * ui_scale);
    ImGui::SliderFloat("Atmosphere opacity",
        &atmosphere_opacity_,
        0.0f,
        1.0f,
        "%.2f",
        ImGuiSliderFlags_AlwaysClamp);
    ImGui::Checkbox("Water simulation", &water_simulation_enabled_);
    ImGui::SetNextItemWidth(180 * ui_scale);
    ImGui::SliderFloat(
        "Water level", &water_level_, -100.0f, 250.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(180 * ui_scale);
    float lightning_frequency = lightning_.frequency();
    if (ImGui::DragFloat(
            "Lightning frequency", &lightning_frequency, 1.f, 0.f, 1.e6f, "%.1f / min"))
        lightning_.set_frequency(lightning_frequency);
    ImGui::SetNextItemWidth(180 * ui_scale);
    float rain_intensity = rain_.intensity();
    if (ImGui::DragFloat("Rain intensity", &rain_intensity, 1.f, 0.0f, 10.0f, "%.2f"))
        rain_.set_intensity(rain_intensity);
    ImGui::Checkbox("Clouds", &clouds_enabled_);
    ImGui::Checkbox("Cloud shadows", &cloud_shadows_enabled_);
    ImGui::Checkbox("God rays", &god_rays_enabled_);
    if (flying_)
        ImGui::BeginDisabled();
    ImGui::Checkbox("Cloud simulation", &cloud_simulation_enabled_);
    if (flying_)
        ImGui::EndDisabled();
    if (ImGui::Button("Clear clouds"))
        clouds_.clear_density();
    ImGui::SameLine();
    if (ImGui::Button("Reset clouds"))
        clouds_.reset(wind_speed_, wind_direction, cloud_base_, cloud_top);
    ImGui::SetNextItemWidth(180 * ui_scale);
    ImGui::SliderFloat(
        "Cloud coverage", &cloud_coverage_, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(180 * ui_scale);
    ImGui::SliderFloat(
        "Cloud opacity", &cloud_opacity_, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(180 * ui_scale);
    ImGui::SliderFloat(
        "Wind speed", &wind_speed_, 0.0f, 500.0f, "%.1f units/s", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(180 * ui_scale);
    ImGui::SliderAngle("Wind direction",
        &wind_direction_,
        -180.0f,
        180.0f,
        "%.0f deg",
        ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(180 * ui_scale);
    ImGui::SliderFloat(
        "Cloud height", &cloud_base_, 0.0f, 1000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);

    // ImGui::Checkbox("SSGI", &ssgiEnabled);
    ImGui::Separator();
    ImGui::TextUnformatted(
        "Drag left mouse to pan\nDouble-click terrain to focus\nDrag right mouse to "
        "rotate\nScroll to zoom");
    ImGui::Text("FPS: %.1f", io.Framerate);
    ImGui::End();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window_);
}

Application::Application(GLFWwindow* window, const char* executable_path) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 10;
    style.WindowPadding = ImVec2(18, 16);
    style.ItemSpacing = ImVec2(8, 8);
    style.ScaleAllSizes(ui_scale);
    ImFontConfig font_config;
    font_config.SizePixels = 13.0f * ui_scale;
    ImGui::GetIO().Fonts->AddFontDefault(&font_config);
    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
        ImGui::DestroyContext();
        throw std::runtime_error("Failed to initialize ImGui GLFW backend.");
    }
    if (!ImGui_ImplOpenGL3_Init("#version 430")) {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        throw std::runtime_error("Failed to initialize ImGui OpenGL backend.");
    }
    try {
        state_ = std::make_unique<AppState>(window, executable_path);
    } catch (...) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        throw;
    }
}

Application::~Application() {
    state_.reset();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Application::frame() {
    state_->frame();
}
} // namespace earth_sim
