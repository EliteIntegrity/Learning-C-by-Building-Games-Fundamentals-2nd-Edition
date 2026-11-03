#pragma once
#include <cmath>

// Vec2: a two-dimensional vector, an (x, y) pair. It can stand for a
// position, a direction, a velocity or an offset: anything in the plane.
// The book grows it chapter by chapter, and each part names its chapter.
struct Vec2
{
    float x = 0.0f;
    float y = 0.0f;

    Vec2() = default;
    Vec2(float x, float y) : x(x), y(y) {}

    // Arithmetic (Chapter 2): add, subtract, scale and negate.
    Vec2 operator+(const Vec2& r) const { return { x + r.x, y + r.y }; }
    Vec2 operator-(const Vec2& r) const { return { x - r.x, y - r.y }; }
    Vec2 operator*(float s)       const { return { x * s,   y * s   }; }
    Vec2 operator/(float s)       const { return { x / s,   y / s   }; }
    Vec2 operator-()              const { return { -x, -y }; }

    // The compound forms, so we can write pos += velocity * dt.
    Vec2& operator+=(const Vec2& r) { x += r.x; y += r.y; return *this; }
    Vec2& operator-=(const Vec2& r) { x -= r.x; y -= r.y; return *this; }
    Vec2& operator*=(float s)       { x *= s;   y *= s;   return *this; }

    // Exact equality: the same two numbers. Floats that come from
    // arithmetic need a tolerance instead (Chapter 5).
    bool operator==(const Vec2& r) const { return x == r.x && y == r.y; }

    // Length (Chapter 3). lengthSquared skips the square root, for when
    // we only need to compare lengths.
    float lengthSquared() const { return x * x + y * y; }
    float length()        const { return std::sqrt(lengthSquared()); }

    // The same direction with a length of 1. The zero vector has no
    // direction, so it stays zero, rather than being divided by zero.
    Vec2 normalized() const
    {
        float len = length();
        return (len > 0.0f) ? Vec2{ x / len, y / len } : Vec2{ 0.0f, 0.0f };
    }

    // The dot product (Chapter 4): positive when the two point the same
    // way, zero when they're perpendicular, negative when they're opposed.
    float dot(const Vec2& r) const { return x * r.x + y * r.y; }

    // A quarter turn of this vector, which is clockwise on the screen.
    Vec2 perp() const { return { -y, x }; }

    // The 2D cross product (Chapter 4), which is perp().dot(r): positive
    // when r points to the right of this vector, as the screen shows them.
    float cross(const Vec2& r) const { return x * r.y - y * r.x; }

    // Angles (Chapter 5). A vector at an angle, in radians, with the
    // length given: 1 unless asked otherwise.
    static Vec2 fromAngle(float radians, float length = 1.0f)
    {
        return { std::cos(radians) * length, std::sin(radians) * length };
    }

    // The angle this vector points at, in radians, from -PI to PI,
    // measured from the x axis. y comes first in atan2.
    float angle() const { return std::atan2(y, x); }

    // Rotation (Chapter 6). This vector turned by an angle, in radians,
    // keeping its length: clockwise on the screen when the angle's positive.
    Vec2 rotated(float radians) const
    {
        float c = std::cos(radians);
        float s = std::sin(radians);
        return { x * c - y * s, x * s + y * c };
    }
};

// A number on the left, so that 2.0f * v works as well as v * 2.0f.
inline Vec2 operator*(float s, const Vec2& v) { return { v.x * s, v.y * s }; }

// The straight-line distance between two points (Chapter 3).
inline float distance(const Vec2& a, const Vec2& b) { return (b - a).length(); }

// Pi, for turning degrees into radians (Chapter 5).
constexpr float PI = 3.14159265f;

// An angle wrapped into the range from -PI to PI: the same direction,
// with any whole turns taken away. The wrapped difference of two angles
// is the shortest turn from one to the other.
inline float wrapAngle(float radians)
{
    return std::remainder(radians, 2.0f * PI);
}

// Whether two floats are within a small tolerance of each other, which is
// how floats that come from arithmetic should be compared.
inline bool nearlyEqual(float a, float b, float epsilon = 0.0001f)
{
    return std::fabs(a - b) < epsilon;
}

// A point turned about a pivot (Chapter 6): its offset from the pivot,
// rotated, and put back.
inline Vec2 rotateAbout(const Vec2& point, const Vec2& pivot, float radians)
{
    return pivot + (point - pivot).rotated(radians);
}

// Interpolation (Chapter 8): the value a fraction t of the way from a to
// b. It's a when t is 0, b when t is 1, and halfway between when t is 0.5.
inline float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

inline Vec2 lerp(const Vec2& a, const Vec2& b, float t)
{
    return a + (b - a) * t;
}

// Lerp run backward: the fraction of the way from a to b that v is. A
// range with no width, where a equals b, has no fractions, so it gives 0.
inline float inverseLerp(float a, float b, float v)
{
    return (a != b) ? (v - a) / (b - a) : 0.0f;
}

// A value moved from one range onto another: its fraction of the way
// from a to b, taken the same fraction of the way from c to d.
inline float remap(float v, float a, float b, float c, float d)
{
    return lerp(c, d, inverseLerp(a, b, v));
}

// An angle a fraction t of the way from a to b, in radians, turning the
// shortest way round: by the wrapped difference of the two (Chapter 5).
inline float lerpAngle(float a, float b, float t)
{
    return a + wrapAngle(b - a) * t;
}
