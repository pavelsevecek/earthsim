#include "Common.hpp"

namespace earth_sim {
Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 result{};
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 4; ++k)
                result[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k];
    return result;
}

Mat4 perspective(float aspect, float distance) {
    float f = 1.0f / std::tan(pi / 8.0f);
    float near_plane = std::max(0.001f, distance * 0.0001f);
    float far_plane = std::max(6000.0f, distance + 4000.0f);
    return { f / aspect,
        0,
        0,
        0,
        0,
        f,
        0,
        0,
        0,
        0,
        (far_plane + near_plane) / (near_plane - far_plane),
        -1,
        0,
        0,
        2 * far_plane * near_plane / (near_plane - far_plane),
        0 };
}

Mat4 look_at(Vec3 eye, Vec3 forward, Vec3 right, Vec3 up) {
    return { right.x,
        up.x,
        -forward.x,
        0,
        right.y,
        up.y,
        -forward.y,
        0,
        right.z,
        up.z,
        -forward.z,
        0,
        -dot(right, eye),
        -dot(up, eye),
        dot(forward, eye),
        1 };
}
} // namespace earth_sim
