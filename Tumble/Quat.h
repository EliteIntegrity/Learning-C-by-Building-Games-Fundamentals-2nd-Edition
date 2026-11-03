#pragma once
#include <cmath>
#include "Vec3.h"
#include "Mat4.h"

// Quat: a quaternion, four numbers that hold a whole turn (Chapter 35). For
// a turn by an angle about an axis, w is the cosine of half the angle, and
// (x, y, z) is the axis times the sine of half the angle, so that all four,
// as a vector, are 1 long.
struct Quat
{
    float w = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Quat() = default;
    Quat(float w, float x, float y, float z) : w(w), x(x), y(y), z(z) {}

    // A turn by an angle, in radians, about an axis.
    static Quat fromAxisAngle(Vec3 axis, float angle)
    {
        Vec3 a = axis.normalized();
        float s = std::sin(angle * 0.5f);
        return { std::cos(angle * 0.5f), a.x * s, a.y * s, a.z * s };
    }

    // The product: one quaternion that turns by q, and then by this one.
    // With v for (x, y, z), it's (w1 w2 - v1 . v2, w1 v2 + w2 v1 + v1 x v2).
    Quat operator*(const Quat& q) const
    {
        return { w * q.w - x * q.x - y * q.y - z * q.z,
                 w * q.x + x * q.w + y * q.z - z * q.y,
                 w * q.y - x * q.z + y * q.w + z * q.x,
                 w * q.z + x * q.y - y * q.x + z * q.w };
    }

    // The arithmetic of four numbers, which slerp blends with: a quaternion
    // times a number, the sum of two, and the negative of one.
    Quat operator*(float s) const { return { w * s, x * s, y * s, z * s }; }
    Quat operator+(const Quat& q) const
    {
        return { w + q.w, x + q.x, y + q.y, z + q.z };
    }
    Quat operator-() const { return { -w, -x, -y, -z }; }

    // The dot product of all four numbers. For two quaternions 1 long, it's
    // the cosine of the angle between them, as points in four dimensions.
    float dot(const Quat& q) const
    {
        return w * q.w + x * q.x + y * q.y + z * q.z;
    }

    // The same turn undone: the axis reversed, and the angle kept.
    Quat conjugate() const { return { w, -x, -y, -z }; }

    // Scaled back to a length of 1, which arithmetic in floats drifts from.
    Quat normalized() const
    {
        float n = std::sqrt(dot(*this));
        return n > 0.0f ? Quat{ w / n, x / n, y / n, z / n } : Quat{};
    }

    // The turn as a matrix, for the pipeline: the matrix that does what
    // q v q*, the quaternion, the point and the conjugate multiplied,
    // does to a point v.
    Mat4 toMat4() const
    {
        Mat4 m;
        m.m[0][0] = 1 - 2 * (y * y + z * z);
        m.m[0][1] = 2 * (x * y - w * z);
        m.m[0][2] = 2 * (x * z + w * y);
        m.m[1][0] = 2 * (x * y + w * z);
        m.m[1][1] = 1 - 2 * (x * x + z * z);
        m.m[1][2] = 2 * (y * z - w * x);
        m.m[2][0] = 2 * (x * z - w * y);
        m.m[2][1] = 2 * (y * z + w * x);
        m.m[2][2] = 1 - 2 * (x * x + y * y);
        return m;
    }
};

// Spherical linear interpolation: the orientation a fraction t of the way
// from a to b, along the shortest arc between them, at an even pace. If
// the dot product is negative, -b is the same turn as b, and nearer, so
// the blend goes to it instead. If the two are nearly the same, a straight
// blend, normalized, is close enough, and avoids dividing by almost 0.
inline Quat slerp(Quat a, Quat b, float t)
{
    float d = a.dot(b);
    if (d < 0.0f)
    {
        b = -b;
        d = -d;
    }
    if (d > 0.9995f) return (a * (1.0f - t) + b * t).normalized();
    float theta = std::acos(d);
    float s0 = std::sin((1.0f - t) * theta) / std::sin(theta);
    float s1 = std::sin(t * theta) / std::sin(theta);
    return a * s0 + b * s1;
}

// The angle of the turn from one orientation to another, in radians: the
// angle of r, the turn that takes a to b. Its w is the cosine of half that
// angle, and the length of its (x, y, z) the sine, so atan2 gives the half
// angle, even when it's tiny, where acos of a float near 1 isn't precise.
// Since r and -r are the same turn, w's size is what counts.
inline float turnBetween(const Quat& a, const Quat& b)
{
    Quat r = b * a.conjugate();
    float s = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
    return 2.0f * std::atan2(s, std::fabs(r.w));
}
