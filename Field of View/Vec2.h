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
};

// A number on the left, so that 2.0f * v works as well as v * 2.0f.
inline Vec2 operator*(float s, const Vec2& v) { return { v.x * s, v.y * s }; }

// The straight-line distance between two points (Chapter 3).
inline float distance(const Vec2& a, const Vec2& b) { return (b - a).length(); }
