#pragma once
#include <cmath>

// Vec3: a vector in three dimensions (Chapter 28). It's Vec2 with a third
// component, z, and the same arithmetic, length and dot product, each with
// a z term added. Its cross product is new: a vector at right angles to
// the two it came from. Its functions take a Vec3 by value, a copy of three
// floats, which costs no more than a reference.
struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vec3() = default;
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    // Arithmetic: Vec2's, with z added to each.
    Vec3 operator+(Vec3 r) const { return { x + r.x, y + r.y, z + r.z }; }
    Vec3 operator-(Vec3 r) const { return { x - r.x, y - r.y, z - r.z }; }
    Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
    Vec3 operator/(float s) const { return { x / s, y / s, z / s }; }
    Vec3 operator-() const { return { -x, -y, -z }; }
    Vec3& operator+=(Vec3 r) { x += r.x; y += r.y; z += r.z; return *this; }
    Vec3& operator-=(Vec3 r) { x -= r.x; y -= r.y; z -= r.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    bool operator==(Vec3 r) const { return x == r.x && y == r.y && z == r.z; }

    // Length: Pythagoras, with a third square under the root.
    float lengthSquared() const { return x * x + y * y + z * z; }
    float length() const { return std::sqrt(lengthSquared()); }

    // The same direction with a length of 1, or zero for the zero vector.
    Vec3 normalized() const
    {
        float len = length();
        return (len > 0.0f) ? Vec3{ x / len, y / len, z / len } : Vec3{};
    }

    // The dot product: Chapter 4's, with a z term.
    float dot(Vec3 r) const { return x * r.x + y * r.y + z * r.z; }

    // The cross product: a vector at right angles to this one and r, as
    // long as the area of the parallelogram the two span. Each component is
    // a 2D cross product of the other two axes.
    Vec3 cross(Vec3 r) const
    {
        return { y * r.z - z * r.y,
                 z * r.x - x * r.z,
                 x * r.y - y * r.x };
    }
};

// A number on the left, so that 2.0f * v works as well as v * 2.0f.
inline Vec3 operator*(float s, Vec3 v) { return v * s; }

// The straight-line distance between two points.
inline float distance(Vec3 a, Vec3 b) { return (b - a).length(); }
