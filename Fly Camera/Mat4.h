#pragma once
#include <cmath>
#include "Vec3.h"

// Vec4: a point in four coordinates, with its w kept (Chapter 30), as a
// projection leaves it. A new one is (0, 0, 0, 1).
struct Vec4
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

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

    // An orthographic projection, for a camera at the origin looking down
    // -z. It squeezes a box into the cube from -1 to 1: halfHeight above and
    // below the middle of the view, aspect times that to each side, and from
    // nearDist to farDist in front. Its bottom row stays 0, 0, 0, 1.
    static Mat4 orthographic(float halfHeight, float aspect, float nearDist,
                             float farDist)
    {
        Mat4 r;
        r.m[0][0] = 1.0f / (aspect * halfHeight);
        r.m[1][1] = 1.0f / halfHeight;
        r.m[2][2] = -2.0f / (farDist - nearDist);
        r.m[2][3] = -(farDist + nearDist) / (farDist - nearDist);
        return r;
    }

    // A perspective projection, for a camera at the origin looking down -z,
    // with a field of view, fovY, from the bottom of the view to its top, in
    // radians. The view's half-height at a distance of 1 is tan(fovY / 2),
    // and f is 1 over that. The bottom row puts the depth in front, -z, into
    // w, so that the divide by w makes distant things smaller.
    static Mat4 perspective(float fovY, float aspect, float nearDist,
                            float farDist)
    {
        float f = 1.0f / std::tan(fovY * 0.5f);
        Mat4 r;
        r.m[0][0] = f / aspect;
        r.m[1][1] = f;
        r.m[2][2] = (farDist + nearDist) / (nearDist - farDist);
        r.m[2][3] = 2.0f * farDist * nearDist / (nearDist - farDist);
        r.m[3][2] = -1.0f;
        r.m[3][3] = 0.0f;
        return r;
    }

    // A view matrix, for a camera at eye looking toward target (Chapter 31).
    // The up vector is a hint for which way is up, such as the world's
    // (0, 1, 0). The camera's own axes are right, s, a true up, u, and back,
    // -f. The view is the camera's placement undone: a move by -eye, then
    // the turn transposed, so its rows are the axes.
    static Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up)
    {
        Vec3 f = (target - eye).normalized();
        Vec3 s = f.cross(up).normalized();
        Vec3 u = s.cross(f);
        Mat4 r;
        r.m[0][0] = s.x;
        r.m[0][1] = s.y;
        r.m[0][2] = s.z;
        r.m[1][0] = u.x;
        r.m[1][1] = u.y;
        r.m[1][2] = u.z;
        r.m[2][0] = -f.x;
        r.m[2][1] = -f.y;
        r.m[2][2] = -f.z;
        r.m[0][3] = -s.dot(eye);
        r.m[1][3] = -u.dot(eye);
        r.m[2][3] = f.dot(eye);
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

    // A point, transformed, with its fourth coordinate kept: all four rows
    // dotted with (x, y, z, 1). For a projection, the bottom row isn't
    // 0, 0, 0, 1, so w isn't 1.
    Vec4 apply4(Vec3 p) const
    {
        return { m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3],
                 m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3],
                 m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3],
                 m[3][0] * p.x + m[3][1] * p.y + m[3][2] * p.z + m[3][3] };
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
