#pragma once

#include "Terrain.hpp"

namespace earth_sim {
class Boat {
    float speed_ = 0;
    float yaw_ = 0;

    static bool navigable(const Terrain& terrain, Vec3 position, float water_level) {
        // Conservatively cover the hull, including its bow, in every heading.
        for (int z = -1; z <= 1; ++z) {
            for (int x = -1; x <= 1; ++x) {
                Vec3 point = position + Vec3{ x * 5.0f, 0, z * 5.0f };
                Vec3 normal;
                if (std::abs(point.x) > 999 || std::abs(point.z) > 999 ||
                    terrain.surface(point.x, point.z, normal) > water_level - 1.5f)
                    return false;
            }
        }
        return true;
    }

public:
    Vec3 position{};
    Vec3 forward{ 0, 0, 1 };
    Vec3 right{ -1, 0, 0 };
    Vec3 up{ 0, 1, 0 };

    bool spawn(const Terrain& terrain, float water_level, std::mt19937& random) {
        std::uniform_real_distribution<float> coordinate(-990, 990);
        Vec3 candidate;
        bool found = false;
        for (int i = 0; i < 4096 && !found; ++i) {
            candidate = { coordinate(random), water_level, coordinate(random) };
            found = navigable(terrain, candidate, water_level);
        }
        if (!found)
            return false;
        position = candidate;
        up = { 0, 1, 0 };
        yaw_ = std::uniform_real_distribution<float>(-pi, pi)(random);
        forward = { std::sin(yaw_), 0, std::cos(yaw_) };
        right = cross(forward, up);
        speed_ = 0;
        return true;
    }

    void update(const Terrain& terrain, float elapsed, float throttle, float turn, float water_level) {
        position.y = water_level;
        // Small simulation-time steps prevent tunnelling through shores at high time speed.
        while (elapsed > 0) {
            float dt = std::min(elapsed, 1.0f / 60.0f);
            elapsed = std::max(0.0f, elapsed - dt);
            float target_speed = throttle * (throttle >= 0 ? 24.0f : 8.0f);
            speed_ += (target_speed - speed_) * -std::expm1(-dt * 0.8f);
            yaw_ = std::remainder(yaw_ + turn * std::clamp(speed_ / 8.0f, -1.0f, 1.0f) * dt, 2 * pi);
            forward = { std::sin(yaw_), 0, std::cos(yaw_) };
            right = cross(forward, up);
            Vec3 next = position + forward * (speed_ * dt);
            if (navigable(terrain, next, water_level))
                position = next;
            else
                speed_ = 0;
        }
    }

    void follow_surface(float height, Vec3 normal) {
        position.y = height;
        up = normal;
        Vec3 heading{ std::sin(yaw_), 0, std::cos(yaw_) };
        heading.y = -(up.x * heading.x + up.z * heading.z) / up.y;
        forward = normalize(heading);
        right = normalize(cross(forward, up));
        up = normalize(cross(right, forward));
    }
};
} // namespace earth_sim
