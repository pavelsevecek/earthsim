#include "Application.hpp"

#include "Rendering.hpp"

#include <imgui_impl_opengl3.h>

namespace earth_sim {
namespace {
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
    WaterRenderer water_renderer_;
    Terrain terrain_;
    Volcanoes volcanoes_;
    Lightning lightning_;
    Clouds clouds_;
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
    float cloud_base_ = 290.0f;
    static constexpr float cloud_thickness_ = 240.0f;
    float camera_exposure_ = 0.0f;
    float bloom_intensity_ = 0.15f;
    bool clouds_enabled_ = true;
    bool cloud_simulation_enabled_ = true;
    bool particle_simulation_enabled_ = true;
    bool bloom_enabled_ = true;
    bool ssgi_enabled_ = false;
    bool source_icons_visible_ = true;
    float time_speed_ = 1.0f;
    float day_phase_offset_ = 0.34f;
    bool day_night_paused_ = false;
    float meteor_size_ = 1.0f;
    Vec3 target_{ 0, 50, 0 };
    bool panning_ = false;
    bool rotating_ = false;
    enum class PlacementTool {
        None,
        Volcano,
        Spring,
        Meteor,
        TornadoOrigin,
        TornadoDirection,
        TerrainUp,
        TerrainDown,
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
    float particle_spacing_ = 3.0f;
    double previous_;
    double simulation_time_ = 0.0;
    double day_time_ = 0.0;

public:
    AppState(GLFWwindow* window, const char* executable_path)
        : window_(window)
        , directory_(shader_directory(executable_path))
        , terrain_renderer_(directory_)
        , sky_renderer_(directory_)
        , water_renderer_(directory_)
        , terrain_(random_terrain_seed())
        , volcanoes_(directory_, terrain_)
        , lightning_(directory_)
        , clouds_(directory_, volcanoes_.terrain_texture())
        , rain_(directory_)
        , shadows_(directory_)
        , screen_space_gi_(directory_)
        , reflection_()
        , bloom_(directory_)
        , previous_(glfwGetTime()) {}

    void frame();
};

void AppState::frame() {

    double now = glfwGetTime();
    // Bound stall recovery consistently for the sky, clouds, and particle physics.
    double elapsed = std::clamp(now - previous_, 0.0, 0.1) * double(time_speed_);
    previous_ = now;
    simulation_time_ += elapsed;
    if (!day_night_paused_)
        day_time_ += elapsed;
    float cloud_top = cloud_base_ + cloud_thickness_;
    if (particle_simulation_enabled_)
        volcanoes_.update(terrain_, elapsed, water_level_, wind_speed_);
    lightning_.update(terrain_, elapsed, water_level_, cloud_base_, cloud_top, volcanoes_);
    static const std::vector<Volcanoes::Meteor> no_moving_meteors;
    if (cloud_simulation_enabled_)
        clouds_.update(elapsed,
            float(simulation_time_),
            particle_simulation_enabled_ ? volcanoes_.meteors() : no_moving_meteors,
            volcanoes_.particle_buffer(),
            particle_simulation_enabled_,
            wind_speed_,
            cloud_base_,
            cloud_top);
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
        } else
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
    bool place_click = placement_ != PlacementTool::None && !io.WantCaptureMouse &&
                       ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool target_click = placement_ == PlacementTool::None && !io.WantCaptureMouse &&
                        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if (placement_ != PlacementTool::None && !io.WantCaptureMouse)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (!io.WantCaptureMouse) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && placement_ == PlacementTool::None &&
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
        yaw_ = std::remainder(yaw_ - io.MouseDelta.x * 0.005f, 2 * pi);
        pitch_ = std::remainder(pitch_ + io.MouseDelta.y * 0.005f, 2 * pi);
    }
    if (!io.WantCaptureMouse) {
        float zoomed = distance_ * std::exp(-io.MouseWheel * 0.12f);
        // Reject only floating-point overflow/underflow, with no distance limits.
        if (std::isfinite(zoomed) && zoomed > 0)
            distance_ = zoomed;
    }
    Vec3 orbit{
        std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_), std::cos(pitch_) * std::cos(yaw_)
    };
    // An analytic basis stays stable when orbiting through either pole.
    Vec3 forward = orbit * (-1);
    Vec3 right{ std::cos(yaw_), 0, -std::sin(yaw_) };
    Vec3 up = cross(right, forward);
    if (panning_) {
        // Match screen-space dragging at the orbit target, including on HiDPI displays.
        float units_per_pixel = 2 * distance_ * std::tan(pi / 8) / std::max(io.DisplaySize.y, 1.0f);
        target_ = target_ + right * (-io.MouseDelta.x * units_per_pixel) +
                  up * (io.MouseDelta.y * units_per_pixel);
    }
    Vec3 eye = target_ + orbit * distance_;
    if (source_icons_visible_ && placement_ == PlacementTool::None && !io.WantCaptureMouse &&
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
    if (!io.WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        if (selected_source_type_ == SourceType::Lava)
            volcanoes_.remove_lava_source(selected_source_index_);
        else if (selected_source_type_ == SourceType::Spring)
            volcanoes_.remove_spring_source(selected_source_index_);
        selected_source_type_ = SourceType::None;
    }
    double wrapped_day = std::fmod(day_time_ / 60.0 + day_phase_offset_, 1.0);
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
    Mat4 projection = perspective(float(w) / float(h), distance_);
    Mat4 vp = multiply(projection, look_at(eye, forward, right, up));
    if (particle_simulation_enabled_)
        rain_.update(elapsed,
            clouds_enabled_,
            cloud_coverage_,
            eye,
            water_level_,
            float(simulation_time_),
            wind_speed_,
            cloud_base_,
            cloud_top,
            volcanoes_.terrain_texture(),
            clouds_.density_texture(),
            volcanoes_.particle_buffer(),
            volcanoes_.particle_head_buffer(),
            volcanoes_.particle_next_buffer());

    shadows_.render(terrain_, sun);
    reflection_.begin(w, h);
    Vec3 reflected_eye{ eye.x, 2.0f * water_level_ - eye.y, eye.z };
    Vec3 reflected_forward{ forward.x, -forward.y, forward.z };
    Vec3 reflected_up{ up.x, -up.y, up.z };
    // Preserve horizontal screen orientation. The reflected basis is intentionally
    // mirrored; reflection rendering disables face culling below.
    Vec3 reflected_right{ right.x, -right.y, right.z };
    Mat4 reflection_projection =
        perspective(float(reflection_.width()) / float(reflection_.height()), distance_);
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
        reflection_vp,
        reflected_eye,
        sun,
        fog,
        daylight,
        atmosphere_opacity_,
        true,
        water_level_,
        eye.y >= water_level_ ? 1.0f : -1.0f);
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
    terrain_renderer_.draw(
        terrain_, shadows_, rain_, vp, eye, sun, fog, daylight, atmosphere_opacity_);
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
                         placement_ == PlacementTool::TerrainDown) {
                    float elevation = placement_ == PlacementTool::TerrainUp ? 18.0f : -18.0f;
                    terrain_.deform(position, brush_radius_, elevation);
                    volcanoes_.terrain_changed(terrain_);
                    placement_miss_ = false;
                } else if (placement_ == PlacementTool::AddWater ||
                           placement_ == PlacementTool::AddLava) {
                    if (placement_ == PlacementTool::AddWater)
                        volcanoes_.add_water(
                            terrain_, position, brush_radius_, particle_spacing_);
                    else
                        volcanoes_.add_lava(
                            terrain_, position, brush_radius_, particle_spacing_);
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
        eye,
        sun,
        fog,
        daylight,
        atmosphere_opacity_,
        water_level_,
        float(simulation_time_));
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
            distance_);
    }
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
            cloud_base_,
            cloud_top,
            distance_,
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

    if (source_icons_visible_) {
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

    ImGui::SetNextWindowPos(ImVec2(20 * ui_scale, 20 * ui_scale), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.78f);
    ImGui::Begin("EarthSim",
        nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
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
    ImGui::SetNextItemWidth(180 * ui_scale);
    ImGui::Checkbox("Bloom", &bloom_enabled_);
    ImGui::SliderFloat(
        "Bloom intensity", &bloom_intensity_, 0.0f, 3.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
    ImGui::Text("Active volcanoes: %d", int(volcanoes_.volcano_count()));
    ImGui::Text("Active springs: %d", int(volcanoes_.spring_count()));
    ImGui::Text("Active tornadoes: %d", int(volcanoes_.tornado_count()));
    if (ImGui::Button(source_icons_visible_ ? "Hide source icons" : "Show source icons"))
        source_icons_visible_ = !source_icons_visible_;
    if (ImGui::Button("Create volcano")) {
        placement_ = PlacementTool::Volcano;
        placement_miss_ = false;
        panning_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Create spring")) {
        placement_ = PlacementTool::Spring;
        placement_miss_ = false;
        panning_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Meteor strike")) {
        placement_ = PlacementTool::Meteor;
        placement_miss_ = false;
        panning_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Create tornado")) {
        placement_ = PlacementTool::TornadoOrigin;
        placement_miss_ = false;
        panning_ = false;
    }


    if (ImGui::Button("Terrain up")) {
        placement_ = PlacementTool::TerrainUp;
        placement_miss_ = false;
        panning_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Terrain down")) {
        placement_ = PlacementTool::TerrainDown;
        placement_miss_ = false;
        panning_ = false;
    }
    ImGui::SameLine();

    if (ImGui::Button("Add water")) {
        placement_ = PlacementTool::AddWater;
        placement_miss_ = false;
        panning_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add lava")) {
        placement_ = PlacementTool::AddLava;
        placement_miss_ = false;
        panning_ = false;
    }
    ImGui::SameLine();

    if (ImGui::Button("None")) {
        placement_ = PlacementTool::None;
        placement_miss_ = false;
        panning_ = false;
    }
    ImGui::SetNextItemWidth(180 * ui_scale);
    ImGui::SliderFloat(
        "Meteor size", &meteor_size_, 0.1f, 5.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(180 * ui_scale);
    ImGui::SliderFloat("Brush radius",
        &brush_radius_,
        10.0f,
        300.0f,
        "%.0f",
        ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(180 * ui_scale);
    ImGui::SliderFloat("Particle spacing",
        &particle_spacing_,
        1.0f,
        12.0f,
        "%.1f",
        ImGuiSliderFlags_AlwaysClamp);
    if (placement_ != PlacementTool::None) {
        const char* placement_prompt =
            placement_ == PlacementTool::Volcano  ? "Click terrain to place a volcano."
            : placement_ == PlacementTool::Spring ? "Click terrain to place a spring."
            : placement_ == PlacementTool::Meteor ? "Click terrain to target a meteor."
            : placement_ == PlacementTool::TornadoOrigin
                ? "Click terrain to set the tornado origin."
            : placement_ == PlacementTool::TornadoDirection
                ? "Click terrain to set the tornado direction."
            : placement_ == PlacementTool::TerrainUp   ? "Click terrain to raise it."
            : placement_ == PlacementTool::TerrainDown ? "Click terrain to lower it."
            : placement_ == PlacementTool::AddWater    ? "Click terrain to add water."
                                                       : "Click terrain to add lava.";
        ImGui::TextUnformatted(placement_prompt);
        ImGui::TextDisabled("Esc cancels placement.");
        if (ImGui::Button("Cancel placement")) {
            placement_ = PlacementTool::None;
            placement_miss_ = false;
        }
        if (placement_miss_)
            ImGui::TextColored(ImVec4(1, 0.65f, 0.3f, 1), "No terrain here. Click the landscape.");
        else if (placement_ == PlacementTool::TornadoDirection)
            ImGui::TextDisabled("Choose a point at least 5 units from the origin.");
    }
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
    ImGui::Checkbox("Cloud simulation", &cloud_simulation_enabled_);
    if (ImGui::Button("Clear clouds"))
        clouds_.clear_density();
    ImGui::SameLine();
    if (ImGui::Button("Reset clouds"))
        clouds_.reset(wind_speed_, cloud_base_, cloud_top);
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
    ImGui::SliderFloat(
        "Cloud height", &cloud_base_, 0.0f, 1000.0f, "%.0f", ImGuiSliderFlags_AlwaysClamp);

    // ImGui::Checkbox("SSGI", &ssgiEnabled);
    ImGui::Separator();
    ImGui::TextUnformatted(
        "Drag left mouse to pan\nDouble-click terrain to focus\nDrag right mouse to "
        "rotate\nScroll to zoom\nEsc to exit");
    ImGui::Text("FPS: %.1f", io.Framerate);
    ImGui::End();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x, 0.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.78f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("EarthSim quit",
        nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    if (ImGui::Button("X", ImVec2(36.0f * ui_scale, 30.0f * ui_scale)))
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    ImGui::End();
    ImGui::PopStyleVar(2);
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
