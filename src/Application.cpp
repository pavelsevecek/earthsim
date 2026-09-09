#include "Application.hpp"

#include "OffroadVehicle.hpp"
#include "Rendering.hpp"
#include <limits>
#include <sstream>

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
    AircraftRenderer offroad_body_renderer_;
    AircraftRenderer offroad_wheel_renderer_;
    OffroadVehicle vehicle_;
    bool driving_ = false;
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
    bool autopilot_enabled_ = false;
    bool aircraft_destroyed_ = false;
    Vec3 aircraft_position_{};
    Vec3 aircraft_velocity_{ 0, 0, 47.5f };
    Vec3 aircraft_forward_{ 0, 0, 1 };
    Vec3 aircraft_right_{ -1, 0, 0 };
    Vec3 aircraft_up_{ 0, 1, 0 };
    Vec3 flight_camera_offset_{ 0, 18, -52 };
    Vec3 flight_camera_center_{};
    Vec3 flight_camera_horizontal_right_{ -1, 0, 0 };
    float flight_camera_yaw_offset_ = 0.0f;
    float flight_camera_pitch_offset_ = 0.0f;
    float flight_camera_distance_ = 55.0273f;
    float drive_camera_yaw_ = 0;
    float drive_camera_elevation_ = 0.32f;
    bool drive_camera_initialized_ = false;
    float aircraft_vapor_emission_ = 0.0f;
    float time_speed_ = 1.0f;
    float day_phase_offset_ = 0.34f;
    float latitude_ = 0.0f;
    bool day_night_paused_ = false;
    float meteor_size_ = 1.0f;
    float explosion_size_ = 1.0f;
    static constexpr float explosion_size_scale_ = 0.65f;
    float camera_shake_strength_ = 0.0f;
    double camera_shake_time_ = 0.0;
    Vec3 target_{ 0, 50, 0 };
    bool panning_ = false;
    bool rotating_ = false;
    Vec3 camera_rotation_pending_{};
    Vec3 camera_pan_pending_{};
    float camera_zoom_pending_ = 0.0f;
    enum class PlacementTool {
        None,
        Volcano,
        Spring,
        Meteor,
        Explosion,
        Lightning,
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
    std::mt19937 random_{ std::random_device{}() };

public:
    AppState(GLFWwindow* window, const char* executable_path)
        : window_(window)
        , directory_(shader_directory(executable_path))
        , terrain_renderer_(directory_)
        , sky_renderer_(directory_)
        , aircraft_renderer_(directory_)
        , offroad_body_renderer_(directory_, AircraftRenderer::Shape::OffroadBody)
        , offroad_wheel_renderer_(directory_, AircraftRenderer::Shape::OffroadWheel)
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

    void draw_vehicle(const Mat4& vp, Vec3 sun, float daylight, bool mirrored = false) {
        auto rotate = [](Vec3 vector, Vec3 axis, float angle) {
            return vector * std::cos(angle) + cross(axis, vector) * std::sin(angle) +
                   axis * (dot(axis, vector) * (1 - std::cos(angle)));
        };
        offroad_body_renderer_.draw(vp,
            vehicle_.body_position(),
            vehicle_.forward,
            vehicle_.right,
            vehicle_.up,
            sun,
            daylight,
            mirrored);
        for (int i = 0; i < 4; ++i) {
            Vec3 wheel_forward =
                rotate(vehicle_.forward, vehicle_.up, i < 2 ? vehicle_.steering : 0);
            Vec3 wheel_right = normalize(cross(wheel_forward, vehicle_.up));
            Vec3 wheel_up = rotate(vehicle_.up, wheel_right, -vehicle_.wheel_angle);
            wheel_forward = normalize(cross(wheel_up, wheel_right));
            offroad_wheel_renderer_.draw(vp,
                vehicle_.wheels[i],
                wheel_forward,
                wheel_right,
                wheel_up,
                sun,
                daylight,
                mirrored);
        }
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
        volcanoes_.update(
            terrain_, elapsed, water_level_, wind_speed_, wind_direction, meteor_size_);
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
    if (cloud_simulation_enabled_ && !flying_ && !driving_)
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
    bool place_click = !flying_ && !driving_ && placement_ != PlacementTool::None &&
                       !io.WantCaptureMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool target_click = !flying_ && !driving_ && placement_ == PlacementTool::None &&
                        !io.WantCaptureMouse && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if (placement_ != PlacementTool::None && !io.WantCaptureMouse)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (!io.WantCaptureMouse) {
        if (!flying_ && !driving_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            placement_ == PlacementTool::None && !target_click)
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
    if (!focused || target_click) {
        camera_rotation_pending_ = {};
        camera_pan_pending_ = {};
        camera_zoom_pending_ = 0.0f;
    }
    // Integrate an exponential input filter in real time. Pending displacement
    // gives a short coast even when the mouse stops while a button is held,
    // without increasing the total drag distance or wheel zoom.
    constexpr float camera_smoothing_time = 0.075f;
    float camera_dt = float(frame_elapsed);
    float camera_blend = -std::expm1(-camera_dt / camera_smoothing_time);
    auto smooth_motion = [&](float input, float& pending) {
        if (camera_dt <= 0.0f) {
            pending += input;
            return 0.0f;
        }
        float motion = input +
                       (pending - input * camera_smoothing_time / camera_dt) * camera_blend;
        pending += input - motion;
        if (input == 0.0f && std::abs(pending) < 0.0001f) {
            motion += pending;
            pending = 0.0f;
        }
        return motion;
    };
    bool accept_rotation = rotating_ && !target_click;
    float rotation_x = smooth_motion(
        accept_rotation ? io.MouseDelta.x : 0.0f, camera_rotation_pending_.x);
    float rotation_y = smooth_motion(
        accept_rotation ? io.MouseDelta.y : 0.0f, camera_rotation_pending_.y);
    float zoom = smooth_motion(
        focused && !io.WantCaptureMouse && !target_click ? io.MouseWheel : 0.0f,
        camera_zoom_pending_);
    if (flying_ || driving_) {
        flight_camera_yaw_offset_ =
            std::remainder(flight_camera_yaw_offset_ - rotation_x * 0.005f, 2 * pi);
        flight_camera_pitch_offset_ =
            std::remainder(flight_camera_pitch_offset_ - rotation_y * 0.005f, 2 * pi);
    } else {
        yaw_ = std::remainder(yaw_ - rotation_x * 0.005f, 2 * pi);
        pitch_ = std::remainder(pitch_ + rotation_y * 0.005f, 2 * pi);
    }
    if (zoom != 0.0f) {
        if (flying_ || driving_) {
            float zoomed = flight_camera_distance_ * std::exp(-zoom * 0.12f);
            if (std::isfinite(zoomed))
                flight_camera_distance_ = std::clamp(zoomed, 14.0f, 500.0f);
        } else {
            float zoomed = distance_ * std::exp(-zoom * 0.12f);
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
    if (!flying_ && !driving_) {
        // Match screen-space dragging at the orbit target, including on HiDPI displays.
        float units_per_pixel = 2 * distance_ * std::tan(pi / 8) / std::max(io.DisplaySize.y, 1.0f);
        Vec3 pan_input = panning_ ? right * (-io.MouseDelta.x * units_per_pixel) +
                                       up * (io.MouseDelta.y * units_per_pixel)
                                 : Vec3{};
        target_ = target_ + Vec3{ smooth_motion(pan_input.x, camera_pan_pending_.x),
                                smooth_motion(pan_input.y, camera_pan_pending_.y),
                                smooth_motion(pan_input.z, camera_pan_pending_.z) };
    }
    // Shorten the orbit arm at the ocean surface, retaining the requested zoom
    // so it returns naturally when the user rotates away from the water.
    constexpr float ocean_camera_clearance = 1.0f;
    float camera_floor = water_level_ + ocean_camera_clearance;
    auto ocean_safe_offset = [&](Vec3 pivot, Vec3 offset) {
        if (offset.y < 0.0f) {
            float fraction = std::clamp((pivot.y - camera_floor) / -offset.y, 0.0f, 1.0f);
            offset = offset * fraction;
        }
        return offset;
    };
    // A submerged focus cannot be made safe by zooming toward it. Keep the
    // orbit pivot above the surface, including after panning or terrain focus.
    if (target_.y < camera_floor + 1.0f) {
        target_.y = camera_floor + 1.0f;
        camera_pan_pending_.y = std::max(camera_pan_pending_.y, 0.0f);
    }
    Vec3 orbit_offset = ocean_safe_offset(target_, orbit * distance_);
    float effective_camera_distance = std::sqrt(dot(orbit_offset, orbit_offset));
    Vec3 eye = target_ + orbit_offset;
    if (flying_) {
        float roll = 0.0f;
        float pitch = 0.0f;
        bool keyboard_control_available = !io.WantCaptureKeyboard && focused;
        bool manual_steering = keyboard_control_available &&
                               (ImGui::IsKeyDown(ImGuiKey_W) ||
                                   ImGui::IsKeyDown(ImGuiKey_S) ||
                                   ImGui::IsKeyDown(ImGuiKey_A) ||
                                   ImGui::IsKeyDown(ImGuiKey_D));
        if (autopilot_enabled_ && manual_steering)
            autopilot_enabled_ = false;
        if (autopilot_enabled_ && !aircraft_destroyed_) {
            float minimum_clearance = std::numeric_limits<float>::max();
            constexpr float look_ahead_times[] = { 0.0f, 1.0f, 2.0f, 3.0f };
            for (float look_ahead : look_ahead_times) {
                Vec3 sample_position = aircraft_position_ + aircraft_velocity_ * look_ahead;
                Vec3 terrain_normal;
                float surface = std::max(
                    terrain_.surface(sample_position.x, sample_position.z, terrain_normal),
                    water_level_);
                minimum_clearance = std::min(minimum_clearance, sample_position.y - surface);
            }
            if (minimum_clearance < 45.0f)
                pitch = 1.0f;

            constexpr float map_half_extent = 500.0f;
            Vec3 predicted_position = aircraft_position_ + aircraft_velocity_ * 4.0f;
            float edge_distance = std::max(std::max(std::abs(aircraft_position_.x),
                                               std::abs(aircraft_position_.z)),
                std::max(std::abs(predicted_position.x), std::abs(predicted_position.z)));
            Vec3 level_up = Vec3{ 0, 1, 0 } -
                            aircraft_forward_ * dot(Vec3{ 0, 1, 0 }, aircraft_forward_);
            if (dot(level_up, level_up) > 0.0001f) {
                level_up = normalize(level_up);
                Vec3 desired_up = level_up;
                Vec3 horizontal_heading{ aircraft_forward_.x, 0, aircraft_forward_.z };
                Vec3 toward_center{ -aircraft_position_.x, 0, -aircraft_position_.z };
                if (edge_distance > map_half_extent &&
                    dot(horizontal_heading, horizontal_heading) > 0.0001f &&
                    dot(toward_center, toward_center) > 0.0001f) {
                    horizontal_heading = normalize(horizontal_heading);
                    toward_center = normalize(toward_center);
                    float turn_factor = 3.f * (edge_distance / map_half_extent - 1.f);
                    if (dot(horizontal_heading, toward_center) < 0.5f) {
                        Vec3 level_right = normalize(cross(aircraft_forward_, level_up));
                        float turn_side = dot(toward_center, level_right) >= 0.0f ? 1.0f : -1.0f;
                        desired_up = normalize(level_up + level_right * (turn_side * turn_factor));
                    }
                }
                float up_alignment = dot(aircraft_up_, desired_up);
                if (up_alignment < 0.995f) {
                    float roll_direction =
                        dot(aircraft_forward_, cross(aircraft_up_, desired_up));
                    if (std::abs(roll_direction) < 0.0001f && up_alignment < 0.0f)
                        roll_direction = 1.0f;
                    roll = roll_direction >= 0.0f ? 1.0f : -1.0f;
                }
            }
        } else if (keyboard_control_available) {
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
            float lift_scale = std::clamp(speed / 47.5f, 0.0f, 1.5f);
            float lift = 22.0f * lift_scale * lift_scale;
            Vec3 velocity_direction =
                speed > 0.001f ? aircraft_velocity_ * (1.0f / speed) : aircraft_forward_;
            Vec3 lift_direction =
                aircraft_up_ - velocity_direction * dot(aircraft_up_, velocity_direction);
            if (dot(lift_direction, lift_direction) > 0.0001f)
                lift_direction = normalize(lift_direction);
            else
                lift_direction = aircraft_up_;
            Vec3 acceleration = aircraft_forward_ * ((47.5f - forward_speed) * 1.8f) -
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
            const Vec3 collision_points[] = { { 0, -0.375f, 0 },
                { 0, 0, 4.5f },
                { 0, 0, -3.5f },
                { -5, 0, -1.1f },
                { 5, 0, -1.1f } };
            float terrain_correction = 0.0f;
            float water_correction = 0.0f;
            Vec3 terrain_impact = next_position;
            Vec3 water_impact = { next_position.x, water_level_, next_position.z };
            for (Vec3 local : collision_points) {
                Vec3 point = next_position + aircraft_right_ * local.x + aircraft_up_ * local.y +
                             aircraft_forward_ * local.z;
                Vec3 terrain_normal;
                float terrain_height = terrain_.surface(point.x, point.z, terrain_normal);
                float point_terrain_correction = terrain_height + 0.25f - point.y;
                if (point_terrain_correction > terrain_correction) {
                    terrain_correction = point_terrain_correction;
                    terrain_impact = { point.x, terrain_height, point.z };
                }
                float point_water_correction = water_level_ + 0.25f - point.y;
                if (point_water_correction > water_correction) {
                    water_correction = point_water_correction;
                    water_impact = { point.x, water_level_, point.z };
                }
            }
            float vertical_correction = std::max(terrain_correction, water_correction);
            if (terrain_correction > 0.0f) {
                aircraft_destroyed_ = true;
                aircraft_velocity_ = {};
                volcanoes_.impact(terrain_, terrain_impact, 0.175f);
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
                aircraft_vapor_emission_ += 50.0f * flight_dt;
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

        // Apply the smoothed user orbit after the aircraft chase smoothing.
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
        Vec3 camera_pivot = aircraft_position_;
        camera_pivot.y = std::max(camera_pivot.y, camera_floor + 1.0f);
        camera_offset = ocean_safe_offset(camera_pivot, camera_offset);
        effective_camera_distance = std::sqrt(dot(camera_offset, camera_offset));
        eye = camera_pivot + camera_offset;
        float framing_offset = 3.5f * flight_camera_distance_ / 55.0273f;
        Vec3 camera_target = camera_pivot + camera_up * framing_offset;
        forward = normalize(camera_target - eye);
        right = normalize(cross(forward, camera_up));
        up = normalize(cross(right, forward));
    }
    if (driving_) {
        bool controls = focused && !io.WantCaptureKeyboard;
        float throttle =
            controls ? float(ImGui::IsKeyDown(ImGuiKey_W)) - float(ImGui::IsKeyDown(ImGuiKey_S))
                     : 0;
        float turn = controls
                         ? float(ImGui::IsKeyDown(ImGuiKey_A)) - float(ImGui::IsKeyDown(ImGuiKey_D))
                         : 0;
        if (!vehicle_.destroyed) {
            vehicle_.update(terrain_,
                float(elapsed),
                throttle,
                turn,
                controls && ImGui::IsKeyDown(ImGuiKey_Space),
                water_level_);
            if (vehicle_.destroyed)
                volcanoes_.water_impact(vehicle_.water_impact, 0.35f);
        }
        Vec3 heading{ vehicle_.forward.x, 0, vehicle_.forward.z };
        if (dot(heading, heading) > 0.01f)
            flight_camera_horizontal_right_ = normalize(cross(heading, Vec3{ 0, 1, 0 }));
        // Retain the last camera heading while a jump or tumble points the nose vertically.
        heading = normalize(cross(Vec3{ 0, 1, 0 }, flight_camera_horizontal_right_));
        float heading_yaw = std::atan2(heading.x, heading.z) + flight_camera_yaw_offset_;
        float elevation = std::clamp(0.32f + flight_camera_pitch_offset_, 0.08f, 1.35f);
        // Use real time for a consistent camera response, even when physics is paused.
        auto smooth_angle = [](float current, float target, float blend) {
            return std::remainder(current + std::remainder(target - current, 2 * pi) * blend, 2 * pi);
        };
        bool initialize = !drive_camera_initialized_;
        float orbit_blend = initialize ? 1.0f : -std::expm1(-camera_dt / 0.28f);
        drive_camera_yaw_ = smooth_angle(drive_camera_yaw_, heading_yaw, orbit_blend);
        drive_camera_elevation_ += (elevation - drive_camera_elevation_) * orbit_blend;
        heading_yaw = drive_camera_yaw_;
        elevation = drive_camera_elevation_;
        Vec3 offset = Vec3{ -std::sin(heading_yaw) * std::cos(elevation),
            std::sin(elevation),
            -std::cos(heading_yaw) * std::cos(elevation) } *
                      flight_camera_distance_;
        Vec3 pivot = vehicle_.body_position();
        eye = pivot + offset;
        // Check the whole chase arm so intervening hills cannot hide the vehicle.
        for (int i = 1; i <= 64; ++i) {
            Vec3 point = pivot + offset * (float(i) / 64);
            Vec3 normal;
            if (point.y < std::max(terrain_.surface(point.x, point.z, normal), water_level_) + 1) {
                eye = pivot + offset * (float(i - 1) / 64);
                break;
            }
        }
        Vec3 normal;
        eye.y =
            std::max(eye.y, std::max(terrain_.surface(eye.x, eye.z, normal), water_level_) + 1.0f);
        if (std::hypot(eye.x - pivot.x, eye.z - pivot.z) < 0.1f)
            eye = eye - heading * 0.2f;
        effective_camera_distance = std::sqrt(dot(eye - pivot, eye - pivot));
        // Smooth the orbit only. A separately filtered look direction lags behind
        // RMB movement and lets the vehicle drift away from the screen center.
        forward = normalize(pivot - eye);
        drive_camera_initialized_ = true;
        right = normalize(cross(forward, Vec3{ 0, 1, 0 }));
        up = normalize(cross(right, forward));
    }
    if (!flying_ && !driving_ && camera_shake_enabled_ && camera_shake_strength_ > 0.001f) {
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
    if (!flying_ && !driving_ && source_icons_visible_ && placement_ == PlacementTool::None &&
        !io.WantCaptureMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
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
    if (!flying_ && !driving_ && !io.WantCaptureKeyboard &&
        ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
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
    float latitude_radians = latitude_ * pi / 180.0f;
    // Preserve the existing east/north orientation, tilting the celestial pole
    // above the northern horizon by the observer's latitude (zero axial tilt).
    Vec3 north = normalize({ -0.30f, 0.0f, 1.0f });
    Vec3 world_up = { 0.0f, 1.0f, 0.0f };
    Vec3 celestial_pole = north * std::cos(latitude_radians) + world_up * std::sin(latitude_radians);
    Vec3 equatorial_up = world_up * std::cos(latitude_radians) - north * std::sin(latitude_radians);
    Vec3 sun = normalize(Vec3{ std::cos(angle), 0.0f, 0.30f * std::cos(angle) } +
        equatorial_up * std::sin(angle));
    float daylight = smooth(-0.15f, 0.22f, sun.y);
    Vec3 fog =
        Vec3{ 0.012f, 0.019f, 0.040f } * (1 - daylight) + Vec3{ 0.42f, 0.59f, 0.72f } * daylight;
    float sunset = std::exp(-std::abs(sun.y) * 10) * 0.32f;
    fog = fog * (1 - sunset) + Vec3{ 0.70f, 0.23f, 0.09f } * sunset;
    float near_plane = (flying_ || driving_)
                           ? std::clamp(effective_camera_distance * 0.01f, 0.01f, 0.5f)
                           : std::clamp(effective_camera_distance * 0.0001f, 0.001f, 0.5f);
    float far_plane = (flying_ || driving_) ? 6000.0f : std::max(6000.0f, distance_ + 4000.0f);
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
        celestial_pole,
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
        (flying_ && !aircraft_destroyed_) || (driving_ && !vehicle_.destroyed),
        driving_ ? vehicle_.body_position() : aircraft_position_,
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
    if (driving_ && !vehicle_.destroyed) {
        glDisable(GL_CLIP_DISTANCE0);
        draw_vehicle(reflection_vp, sun, daylight, true);
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
        celestial_pole,
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
        atmosphere_opacity_,
        (flying_ && !aircraft_destroyed_) || (driving_ && !vehicle_.destroyed),
        driving_ ? vehicle_.body_position() : aircraft_position_);
    if (flying_ && !aircraft_destroyed_)
        aircraft_renderer_.draw(vp,
            aircraft_position_,
            aircraft_forward_,
            aircraft_right_,
            aircraft_up_,
            sun,
            daylight);
    if (driving_ && !vehicle_.destroyed)
        draw_vehicle(vp, sun, daylight);
    bool impact_preview = placement_ == PlacementTool::Meteor ||
        placement_ == PlacementTool::Explosion;
    float preview_radius = brush_radius_;
    if (placement_ == PlacementTool::Meteor)
        preview_radius = Volcanoes::meteor_radius(meteor_size_);
    else if (placement_ == PlacementTool::Explosion)
        preview_radius = Volcanoes::impact_radius(explosion_size_ * explosion_size_scale_);
    else if (placement_ == PlacementTool::TornadoOrigin)
        preview_radius = 6.0f; // Radius at the base of the tornado funnel.
    ImU32 preview_color = impact_preview ? IM_COL32(255, 150, 35, 255) : IM_COL32_WHITE;
    bool brush_preview =
        !flying_ && !driving_ && !io.WantCaptureMouse &&
        (placement_ == PlacementTool::TerrainUp || placement_ == PlacementTool::TerrainDown ||
            placement_ == PlacementTool::FlattenTerrain ||
            placement_ == PlacementTool::RoughenTerrain || placement_ == PlacementTool::AddWater ||
            placement_ == PlacementTool::AddLava || impact_preview ||
            placement_ == PlacementTool::TornadoOrigin ||
            placement_ == PlacementTool::TornadoDirection);
    bool brush_hit = false;
    Vec3 brush_center{};
    if (place_click || target_click || brush_preview) {
        // Pick the surface for actions and the brush preview, before particles/clouds/UI.
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
                if (brush_preview) {
                    brush_hit = true;
                    brush_center = position;
                }
                if (target_click)
                    target_ = position;
                else if (!place_click) {
                    // Hovering previews the brush without applying the selected tool.
                } else if (placement_ == PlacementTool::TerrainUp ||
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
                    } else if (placement_ == PlacementTool::Lightning)
                        lightning_.spawn_at(terrain_,
                            position,
                            water_level_,
                            cloud_base_,
                            cloud_top,
                            volcanoes_);
                    if (placement_ != PlacementTool::Meteor &&
                        placement_ != PlacementTool::Lightning)
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

    if (brush_hit && placement_ == PlacementTool::TornadoDirection) {
        ImDrawList* preview = ImGui::GetBackgroundDrawList();
        auto draw_terrain_line = [&](Vec3 start, Vec3 end) {
            float length = std::hypot(end.x - start.x, end.z - start.z);
            int segments = std::max(1, int(std::ceil(length / 2.0f)));
            ImVec2 previous_screen{};
            bool previous_visible = false;
            for (int i = 0; i <= segments; ++i) {
                Vec3 point = start + (end - start) * (float(i) / float(segments));
                ImVec2 screen{};
                bool visible = false;
                if (point.x >= -1000.0f && point.x <= 1000.0f &&
                    point.z >= -1000.0f && point.z <= 1000.0f) {
                    Vec3 normal;
                    point.y = terrain_.surface(point.x, point.z, normal);
                    visible = world_to_screen(
                        point, eye, forward, right, up, io.DisplaySize, screen);
                }
                if (visible && previous_visible)
                    preview->AddLine(previous_screen, screen, IM_COL32_WHITE, 1.5f * ui_scale);
                previous_screen = screen;
                previous_visible = visible;
            }
        };
        Vec3 direction{ brush_center.x - tornado_origin_.x, 0,
            brush_center.z - tornado_origin_.z };
        float length = std::sqrt(dot(direction, direction));
        if (length >= 5.0f) { // Match the minimum distance required to place a tornado.
            direction = direction * (1.0f / length);
            Vec3 side{ -direction.z, 0, direction.x };
            float head_length = std::min(25.0f, length * 0.3f);
            Vec3 head_base = brush_center - direction * head_length;
            draw_terrain_line(tornado_origin_, brush_center);
            draw_terrain_line(brush_center, head_base + side * (head_length * 0.5f));
            draw_terrain_line(brush_center, head_base - side * (head_length * 0.5f));
        }
    } else if (brush_hit && placement_ != PlacementTool::None) {
        ImDrawList* preview = ImGui::GetBackgroundDrawList();
        constexpr int segments = 256;
        ImVec2 previous_screen{};
        bool previous_visible = false;
        for (int i = 0; i <= segments; ++i) {
            float angle = 2.0f * pi * float(i % segments) / float(segments);
            Vec3 point = brush_center +
                Vec3{ std::cos(angle) * preview_radius, 0, std::sin(angle) * preview_radius };
            ImVec2 screen{};
            bool visible = false;
            if (point.x >= -1000.0f && point.x <= 1000.0f &&
                point.z >= -1000.0f && point.z <= 1000.0f) {
                Vec3 normal;
                point.y = terrain_.surface(point.x, point.z, normal);
                visible = world_to_screen(
                    point, eye, forward, right, up, io.DisplaySize, screen);
            }
            if (visible && previous_visible)
                preview->AddLine(previous_screen, screen, preview_color, 1.5f * ui_scale);
            previous_screen = screen;
            previous_visible = visible;
        }
    }

    if (!flying_ && !driving_ && source_icons_visible_) {
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

    if (!flying_ && !driving_) {
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
            { PlacementTool::Lightning, "##lightning", "Lightning strike", 11 },
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
                : placement_ == PlacementTool::Lightning ? "Click terrain to call down lightning."
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
        ImGui::TextUnformatted(driving_ ? "Drive    W/S: forward/reverse    A/D: steer    Space: "
                                          "brake    RMB: camera    Wheel: zoom"
                                        : "Flight mode    W: descend    S: climb    A/D: roll    "
                                          "RMB: camera    Wheel: zoom");
    }
    const ImVec2 close_button_size(36.0f * ui_scale, 36.0f * ui_scale);
    const ImVec2 flight_button_size(62.0f * ui_scale, 36.0f * ui_scale);
    if (flying_) {
        const ImVec2 autopilot_button_size(105.0f * ui_scale, 36.0f * ui_scale);
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - close_button_size.x -
                                      flight_button_size.x - autopilot_button_size.x -
                                      30.0f * ui_scale,
            7.0f * ui_scale));
        if (ImGui::Button(
                autopilot_enabled_ ? "Autopilot: On" : "Autopilot", autopilot_button_size))
            autopilot_enabled_ = !autopilot_enabled_;
    }
    if (!flying_ && !driving_) {
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - close_button_size.x -
                                       2 * flight_button_size.x - 30.0f * ui_scale,
            7.0f * ui_scale));
        if (ImGui::Button("Drive", flight_button_size)) {
            vehicle_.spawn(terrain_, water_level_, random_);
            driving_ = true;
            drive_camera_initialized_ = false;
            cloud_simulation_enabled_ = false;
            placement_ = PlacementTool::None;
            placement_miss_ = false;
            selected_source_type_ = SourceType::None;
            panning_ = rotating_ = false;
            camera_rotation_pending_ = {};
            camera_pan_pending_ = {};
            camera_zoom_pending_ = 0;
            flight_camera_distance_ = 24;
            flight_camera_yaw_offset_ = flight_camera_pitch_offset_ = 0;
            flight_camera_center_ = vehicle_.position;
        }
    }
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - close_button_size.x -
                                  flight_button_size.x - 20.0f * ui_scale,
        7.0f * ui_scale));
    if (ImGui::Button((flying_ || driving_) ? "Stop" : "Fly", flight_button_size)) {
        camera_rotation_pending_ = {};
        camera_pan_pending_ = {};
        camera_zoom_pending_ = 0.0f;
        if (flying_ || driving_) {
            cloud_simulation_enabled_ = true;
            if (driving_) {
                target_ = vehicle_.position;
                distance_ = 100.0f;
            }
            driving_ = false;
            flying_ = false;
            autopilot_enabled_ = false;
        } else {
            cloud_simulation_enabled_ = false;
            placement_ = PlacementTool::None;
            placement_miss_ = false;
            selected_source_type_ = SourceType::None;
            panning_ = false;
            rotating_ = false;
            constexpr float spawn_edge = 950.0f;
            std::uniform_int_distribution<int> edge_distribution(0, 3);
            std::uniform_real_distribution<float> edge_position_distribution(-900.0f, 900.0f);
            float edge_position = edge_position_distribution(random_);
            Vec3 spawn_position{};
            switch (edge_distribution(random_)) {
            case 0:
                spawn_position = { -spawn_edge, 0, edge_position };
                break;
            case 1:
                spawn_position = { spawn_edge, 0, edge_position };
                break;
            case 2:
                spawn_position = { edge_position, 0, -spawn_edge };
                break;
            default:
                spawn_position = { edge_position, 0, spawn_edge };
                break;
            }
            aircraft_forward_ = normalize(Vec3{ -spawn_position.x, 0, -spawn_position.z });
            aircraft_right_ = normalize(cross(aircraft_forward_, Vec3{ 0, 1, 0 }));
            aircraft_up_ = { 0, 1, 0 };
            aircraft_velocity_ = aircraft_forward_ * 47.5f;
            aircraft_destroyed_ = false;
            Vec3 terrain_normal;
            float surface = terrain_.surface(spawn_position.x, spawn_position.z, terrain_normal);
            spawn_position.y = std::max(surface + 160.0f, water_level_ + 120.0f);
            aircraft_position_ = spawn_position;
            flight_camera_distance_ = 55.0273f;
            flight_camera_offset_ =
                normalize(aircraft_forward_ * -52.0f + Vec3{ 0, 18.0f, 0 }) *
                flight_camera_distance_;
            flight_camera_center_ = aircraft_position_;
            flight_camera_horizontal_right_ = aircraft_right_;
            flight_camera_yaw_offset_ = 0.0f;
            flight_camera_pitch_offset_ = 0.0f;
            aircraft_vapor_emission_ = 0.0f;
            autopilot_enabled_ = false;
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
    const float panel_height = io.DisplaySize.y * 0.9f;
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(0.0f, panel_height), ImVec2(io.DisplaySize.x, panel_height));

    static std::string label = "###earthsim";
    bool expanded = ImGui::Begin(label.c_str(),
        nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    float time_of_day_hours = day * 24.0f;
    if (expanded) {
        label = "###earthsim";
    } else {
        std::stringstream ss;
        ss << std::setprecision(2) << std::fixed << time_of_day_hours;
        label = "Time of day: " + ss.str() + " h###earthsim";
    }

    ImGui::SetNextItemWidth(180 * ui_scale);
    if (ImGui::SliderFloat("Time of day",
            &time_of_day_hours,
            0.0f,
            23.999f,
            "%.2f h",
            ImGuiSliderFlags_AlwaysClamp))
        day_phase_offset_ += time_of_day_hours / 24.0f - day;
    ImGui::SetNextItemWidth(180 * ui_scale);
    ImGui::SliderFloat(
        "Latitude", &latitude_, -90.0f, 90.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Negative: south; positive: north. Axial tilt is zero.");
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
    float meteor_frequency = volcanoes_.meteor_frequency();
    if (ImGui::DragFloat(
            "Meteor frequency", &meteor_frequency, 1.f, 0.f, 1.e6f, "%.1f / min"))
        volcanoes_.set_meteor_frequency(meteor_frequency);
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
    if (flying_ || driving_)
        ImGui::BeginDisabled();
    ImGui::Checkbox("Cloud simulation", &cloud_simulation_enabled_);
    if (flying_ || driving_)
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
