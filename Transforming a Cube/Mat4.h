#pragma once
#include <cmath>
#include "Vec3.h"

// Mat4: a 4 by 4 matrix, for transforms in 3D (Chapter 29). It's Mat3 a
// size larger. A point is the column (x, y, z, 1), and a matrix transforms
// it from the left. The bottom row is 0, 0, 0, 1 for every transform but a
// projection (Chapter 30). The numbers are kept by rows: m[row][column].
struct Mat4
{
    // A new Mat4 is the identity, which changes nothing.
    float m[4][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 },
                      { 0, 0, 0, 1 } };

    // A move by (tx, ty, tz), in the fourth column.
    static Mat4 translation(float tx, float ty, float tz)
    {
        Mat4 r;
        r.m[0][3] = tx;
        r.m[1][3] = ty;
        r.m[2][3] = tz;
        return r;
    }

    // A scale by sx, sy and sz, on the diagonal.
    static Mat4 scale(float sx, float sy, float sz)
    {
        Mat4 r;
        r.m[0][0] = sx;
        r.m[1][1] = sy;
        r.m[2][2] = sz;
        return r;
    }

    // Turns about the three axes, by an angle in radians, each one
    // counterclockwise as seen from its axis's positive end. Each leaves its
    // own axis alone, and turns the next two in the cycle x, y, z, x as
    // Chapter 6 turned x and y: y and z for x, z and x for y, x and y for z.
    static Mat4 rotationX(float a)
    {
        float c = std::cos(a);
        float s = std::sin(a);
        Mat4 r;
        r.m[1][1] = c;
        r.m[1][2] = -s;
        r.m[2][1] = s;
        r.m[2][2] = c;
        return r;
    }

    static Mat4 rotationY(float a)
    {
        float c = std::cos(a);
        float s = std::sin(a);
        Mat4 r;
        r.m[2][2] = c;
        r.m[2][0] = -s;
        r.m[0][2] = s;
        r.m[0][0] = c;
        return r;
    }

    static Mat4 rotationZ(float a)
    {
        float c = std::cos(a);
        float s = std::sin(a);
        Mat4 r;
        r.m[0][0] = c;
        r.m[0][1] = -s;
        r.m[1][0] = s;
        r.m[1][1] = c;
        return r;
    }

    // A point, transformed: each of the top three rows dotted with
    // (x, y, z, 1), so the move in the fourth column is added.
    Vec3 apply(Vec3 p) const
    {
        return { m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3],
                 m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3],
                 m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3] };
    }

    // A direction, transformed: each row dotted with (x, y, z, 0), so it's
    // turned and scaled, but never moved.
    Vec3 applyDir(Vec3 v) const
    {
        return { m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
                 m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                 m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z };
    }

    // The product: one matrix that does b's transform, and then this
    // one's, as Mat3's product, with four of everything.
    Mat4 operator*(const Mat4& b) const
    {
        Mat4 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
            {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) sum += m[i][k] * b.m[k][j];
                r.m[i][j] = sum;
            }
        return r;
    }
};

// Column j of a matrix's 3 by 3 block: where the matrix sends axis j. For
// a turn, it's the turned object's own axis.
inline Vec3 column(const Mat4& a, int j)
{
    return { a.m[0][j], a.m[1][j], a.m[2][j] };
}
