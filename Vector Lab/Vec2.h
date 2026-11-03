#pragma once

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
};

// A number on the left, so that 2.0f * v works as well as v * 2.0f.
inline Vec2 operator*(float s, const Vec2& v) { return { v.x * s, v.y * s }; }
