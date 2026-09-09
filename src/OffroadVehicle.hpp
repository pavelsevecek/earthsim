#pragma once

#include "Terrain.hpp"
#include <limits>

namespace earth_sim {
// World-space linear/angular momentum, with isotropic spherical inertia.
class OffroadVehicle {
    static constexpr float mass_ = 1200.0f;
    static constexpr float inertia_radius_ = 2.5f;
    static constexpr float inertia_ = (2.0f / 5.0f) * mass_ * inertia_radius_ * inertia_radius_;
    static constexpr float center_of_mass_drop_ = 1.f;
    static constexpr float wheel_radius_ = 0.9f;
    static constexpr float droop_ = 1.05f;
    static constexpr float bump_ = 0.25f;
    static constexpr float spring_ = mass_ * 15.0f / 4.0f;
    static constexpr float damper_ = mass_ * 4.0f / 4.0f;
    static constexpr double step_ = 1.0 / 240.0;
    Vec3 momentum_{};
    Vec3 angular_momentum_{};
    double accumulator_ = 0;

    static Vec3 rotate(Vec3 v, Vec3 axis, float angle) {
        return v * std::cos(angle) + cross(axis, v) * std::sin(angle) +
               axis * (dot(axis, v) * (1 - std::cos(angle)));
    }

    Vec3 wheel_mount(int i) const {
        return body_position() + right * (i % 2 ? 1.8f : -1.8f) + forward * (i < 2 ? 2.0f : -2.0f);
    }

    Vec3 point_velocity(Vec3 arm) const {
        return momentum_ * (1.0f / mass_) + cross(angular_momentum_ * (1.0f / inertia_), arm);
    }

    float inverse_effective_mass(Vec3 arm, Vec3 direction) const {
        Vec3 angular = cross(arm, direction);
        return 1.0f / mass_ + dot(angular, angular) / inertia_;
    }

    void impulse(Vec3 arm, Vec3 value) {
        momentum_ = momentum_ + value;
        angular_momentum_ = angular_momentum_ + cross(arm, value);
    }

    static float clearance(const Terrain& terrain, Vec3 center, float radius, Vec3& normal) {
        float ground = terrain.surface(center.x, center.z, normal);
        return (center.y - ground) * normal.y - radius;
    }

    float wheel_travel(const Terrain& terrain, int i) const {
        Vec3 mount = wheel_mount(i), normal;
        // A tipped-over suspension must not attach itself to the ground overhead.
        clearance(terrain, mount, 0, normal);
        if (dot(up, normal) <= 0.1f)
            return droop_;
        float lo = bump_, hi = droop_;
        for (int iteration = 0; iteration < 10; ++iteration) {
            float travel = (lo + hi) * 0.5f;
            if (clearance(terrain, mount - up * travel, wheel_radius_, normal) < 0)
                hi = travel;
            else
                lo = travel;
        }
        return (lo + hi) * 0.5f;
    }

    void update_wheels(const Terrain& terrain) {
        for (int i = 0; i < 4; ++i)
            wheels[i] = wheel_mount(i) - up * wheel_travel(terrain, i);
    }

    void wheel_forces(const Terrain& terrain, int i, float dt, float throttle, bool brake) {
        Vec3 mount = wheel_mount(i), normal;
        float gap = clearance(terrain, mount - up * droop_, wheel_radius_, normal);
        float alignment = dot(up, normal);
        if (gap >= 0 || alignment <= 0.1f)
            return;
        float compression = std::clamp(-gap / alignment, 0.0f, droop_ - bump_);
        Vec3 hub = mount - up * (droop_ - compression);
        clearance(terrain, hub, wheel_radius_, normal);
        Vec3 arm = hub - normal * wheel_radius_ - position;
        float normal_speed = dot(point_velocity(arm), normal);
        float inverse_mass = inverse_effective_mass(arm, normal);
        // Implicit spring/damper impulse includes rotational contact velocity and
        // effective mass. It pushes only, so takeoff preserves both momenta.
        float support = std::max(0.0f,
            (spring_ * compression - damper_ * normal_speed) * dt /
                (1 + (damper_ + spring_ * dt) * inverse_mass * dt));
        impulse(arm, normal * support);

        Vec3 tire_forward = rotate(forward, up, i < 2 ? steering : 0);
        tire_forward = tire_forward - normal * dot(tire_forward, normal);
        if (dot(tire_forward, tire_forward) < 0.0001f)
            return;
        tire_forward = normalize(tire_forward);
        Vec3 lateral = normalize(cross(tire_forward, normal));
        Vec3 velocity = point_velocity(arm);
        float longitudinal_speed = dot(velocity, tire_forward);
        float lateral_impulse = -dot(velocity, lateral) * (-std::expm1(-12.0f * dt)) /
                                inverse_effective_mass(arm, lateral);
        float drive = throttle * mass_ * 16.0f * dt;
        if ((throttle > 0 && longitudinal_speed > 36) || (throttle < 0 && longitudinal_speed < -12))
            drive = 0;
        float stopping_impulse = -longitudinal_speed / inverse_effective_mass(arm, tire_forward);
        float resistance = brake ? std::abs(stopping_impulse) : support * 0.025f;
        float longitudinal_impulse = drive + std::clamp(stopping_impulse, -resistance, resistance);
        // An anisotropic friction ellipse allows lateral sliding before rollover,
        // while preserving longitudinal traction for climbing and braking.
        float usage = std::hypot(longitudinal_impulse / 1.25f, lateral_impulse / 0.85f);
        float scale = usage > support && usage > 0 ? support / usage : 1.0f;
        Vec3 tire_impulse =
            (tire_forward * longitudinal_impulse + lateral * lateral_impulse) * scale;
        impulse(arm, tire_impulse);
    }

    void collide(const Terrain& terrain, Vec3 center, float radius) {
        Vec3 normal;
        float gap = clearance(terrain, center, radius, normal);
        if (gap >= 0)
            return;
        Vec3 arm = center - normal * radius - position;
        float incoming = dot(point_velocity(arm), normal);
        if (incoming < 0) {
            float restitution = incoming < -2.0f ? 0.08f : 0.0f;
            float normal_impulse =
                -(1 + restitution) * incoming / inverse_effective_mass(arm, normal);
            impulse(arm, normal * normal_impulse);
            Vec3 velocity = point_velocity(arm);
            Vec3 tangent = velocity - normal * dot(velocity, normal);
            float tangent_speed = std::sqrt(dot(tangent, tangent));
            if (tangent_speed > 0.0001f) {
                tangent = tangent * (1.0f / tangent_speed);
                float friction = std::min(
                    tangent_speed / inverse_effective_mass(arm, tangent), 0.6f * normal_impulse);
                impulse(arm, tangent * -friction);
            }
        }
        // Separate overlap correction from velocity: removing penetration must not
        // manufacture an upward velocity or erase angular momentum.
        position = position + normal * (-gap * 0.65f);
    }

    void integrate(float dt) {
        position = position + momentum_ * (dt / mass_);
        Vec3 omega = angular_momentum_ * (1.0f / inertia_);
        float angular_speed = std::sqrt(dot(omega, omega));
        if (angular_speed > 0.000001f) {
            Vec3 axis = omega * (1.0f / angular_speed);
            // Exponential-map rotation is exact for torque-free spherical inertia.
            forward = rotate(forward, axis, angular_speed * dt);
            up = rotate(up, axis, angular_speed * dt);
        }
        forward = normalize(forward);
        right = normalize(cross(forward, up));
        up = normalize(cross(right, forward));
    }

public:
    // Physics position is the center of mass; geometry retains its original origin.
    Vec3 position{};
    Vec3 forward{ 0, 0, 1 }, right{ -1, 0, 0 }, up{ 0, 1, 0 };
    std::array<Vec3, 4> wheels{};
    float speed = 0;
    float steering = 0;
    float wheel_angle = 0;
    bool destroyed = false;
    Vec3 water_impact{};

    Vec3 body_position() const {
        return position + up * center_of_mass_drop_;
    }

    void spawn(const Terrain& terrain, float water_level, std::mt19937& random) {
        momentum_ = angular_momentum_ = {};
        destroyed = false;
        water_impact = {};
        accumulator_ = 0;
        speed = steering = wheel_angle = 0;
        up = { 0, 1, 0 };
        std::uniform_real_distribution<float> coordinate(-980.0f, 980.0f);
        std::uniform_real_distribution<float> direction(-pi, pi);
        float heading = direction(random);
        // Rejection sampling gives dry, gentle ground across the whole terrain
        // an equal chance, independent of the camera focus. Keep a fallback for
        // landscapes with no suitable dry ground, and always bound the search.
        float best = std::numeric_limits<float>::max();
        for (int attempt = 0; attempt < 1024; ++attempt) {
            Vec3 candidate{ coordinate(random), 0, coordinate(random) };
            Vec3 normal;
            candidate.y = terrain.surface(candidate.x, candidate.z, normal);
            float score =
                std::max(water_level + 2 - candidate.y, 0.0f) * 1000000.0f + (1 - normal.y);
            if (score < best) {
                best = score;
                position = candidate;
            }
            if (candidate.y >= water_level + 2 && normal.y >= 0.8f) {
                position = candidate;
                break;
            }
        }
        Vec3 normal;
        terrain.surface(position.x, position.z, normal);
        up = normal;
        Vec3 heading_forward{ std::sin(heading), 0, std::cos(heading) };
        right = normalize(cross(heading_forward, up));
        forward = normalize(cross(up, right));
        // Start at static spring compression with no initial penetration.
        position.y = 0;
        float spawn_height = -std::numeric_limits<float>::max();
        for (int i = 0; i < 4; ++i) {
            Vec3 hub = wheel_mount(i) - up * (droop_ - 9.81f * mass_ / (4 * spring_));
            float ground = terrain.surface(hub.x, hub.z, normal);
            spawn_height =
                std::max(spawn_height, ground + wheel_radius_ / std::max(normal.y, 0.1f) - hub.y);
        }
        position.y = spawn_height;
        update_wheels(terrain);
    }

    void update(const Terrain& terrain,
        float elapsed,
        float throttle,
        float turn,
        bool brake,
        float water_level) {
        if (destroyed || elapsed <= 0)
            return;
        accumulator_ += elapsed;
        while (accumulator_ >= step_) {
            accumulator_ -= step_;
            constexpr float dt = float(step_);
            steering += (turn * 0.52f - steering) * (-std::expm1(-8 * dt));
            momentum_.y -= mass_ * 9.81f * dt;
            for (int i = 0; i < 4; ++i)
                wheel_forces(terrain, i, dt, throttle, brake);
            integrate(dt);
            // Ocean contact destroys the chassis, including when it lands upside down.
            // Check before terrain resolution can lift it out of shallow water.
            for (int i = 0; i < 8; ++i) {
                float half_width = i < 4 ? 1.45f : 1.35f;
                float axle = i < 4 ? (i % 4 < 2 ? 2.9f : -2.9f)
                                  : (i % 4 < 2 ? 1.05f : -1.85f);
                Vec3 point = body_position() + right * (i % 2 ? half_width : -half_width) +
                             forward * axle + up * (i < 4 ? -0.35f : 2.18f);
                Vec3 normal;
                if (point.y <= water_level &&
                    terrain.surface(point.x, point.z, normal) < water_level) {
                    destroyed = true;
                    water_impact = { point.x, water_level, point.z };
                    momentum_ = angular_momentum_ = {};
                    speed = 0;
                    accumulator_ = 0;
                    return;
                }
            }
            for (int iteration = 0; iteration < 4; ++iteration) {
                // Wheel bump stops and chassis/cabin corners also work when rolled over.
                for (int i = 0; i < 4; ++i) {
                    Vec3 mount = wheel_mount(i), normal;
                    clearance(terrain, mount, 0, normal);
                    float stop = dot(up, normal) > 0.1f ? bump_ : droop_;
                    collide(terrain, mount - up * stop, wheel_radius_);
                    float side = i % 2 ? 1.45f : -1.45f;
                    float axle = i < 2 ? 2.9f : -2.9f;
                    collide(terrain,
                        body_position() + right * side + forward * axle - up * 0.35f,
                        0.12f);
                    collide(terrain,
                        body_position() + right * (side * 0.9f) + forward * (i < 2 ? 1.0f : -1.8f) +
                            up * 2.1f,
                        0.12f);
                }
            }
            // The finite terrain boundary stops only outward motion.
            if (position.x < -994 || position.x > 994) {
                if (position.x * momentum_.x > 0)
                    momentum_.x = 0;
                position.x = std::clamp(position.x, -994.0f, 994.0f);
            }
            if (position.z < -994 || position.z > 994) {
                if (position.z * momentum_.z > 0)
                    momentum_.z = 0;
                position.z = std::clamp(position.z, -994.0f, 994.0f);
            }
            speed = dot(momentum_ * (1.0f / mass_), forward);
            wheel_angle = std::remainder(wheel_angle + speed * dt / wheel_radius_, 2 * pi);
        }
        update_wheels(terrain);
    }
};
} // namespace earth_sim
