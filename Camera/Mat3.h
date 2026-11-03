#pragma once
#include <cmath>
#include "Vec2.h"

// Mat3: a 3 by 3 matrix, for transforms in 2D (Chapter 26). A point is the
// column (x, y, 1), and a matrix transforms it from the left: M times p.
// The bottom row stays 0, 0, 1, so that a move rides along with a turn and
// a scale. The numbers are kept by rows: m[row][column].
struct Mat3
{
    // A new Mat3 is the identity, which changes nothing: ones down the
    // diagonal, and zeros everywhere else.
    float m[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };

    // A move by (tx, ty), in the third column.
    static Mat3 translation(float tx, float ty)
    {
        Mat3 r;
        r.m[0][2] = tx;
        r.m[1][2] = ty;
        return r;
    }

    // A scale by sx across and sy down, on the diagonal.
    static Mat3 scale(float sx, float sy)
    {
        Mat3 r;
        r.m[0][0] = sx;
        r.m[1][1] = sy;
        return r;
    }

    // A turn by an angle, in radians, clockwise on the screen. Its first
    // two columns are where the turn sends the x and y axes (Chapter 6).
    static Mat3 rotation(float radians)
    {
        float c = std::cos(radians);
        float s = std::sin(radians);
        Mat3 r;
        r.m[0][0] = c;
        r.m[0][1] = -s;
        r.m[1][0] = s;
        r.m[1][1] = c;
        return r;
    }

    // A point, transformed: each row dotted with (x, y, 1), so the move in
    // the third column is added.
    Vec2 apply(const Vec2& p) const
    {
        return { m[0][0] * p.x + m[0][1] * p.y + m[0][2],
                 m[1][0] * p.x + m[1][1] * p.y + m[1][2] };
    }

    // A direction, transformed: each row dotted with (x, y, 0), so it's
    // turned and scaled, but never moved.
    Vec2 applyDir(const Vec2& v) const
    {
        return { m[0][0] * v.x + m[0][1] * v.y,
                 m[1][0] * v.x + m[1][1] * v.y };
    }

    // The product: one matrix that does b's transform, and then this
    // one's. Entry (i, j) is row i of this matrix dotted with column j of b.
    Mat3 operator*(const Mat3& b) const
    {
        Mat3 r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
            {
                float sum = 0.0f;
                for (int k = 0; k < 3; ++k) sum += m[i][k] * b.m[k][j];
                r.m[i][j] = sum;
            }
        return r;
    }
};

// The determinant of a matrix's 2 by 2 block: how many times the matrix
// scales areas, negative when it also flips them over.
inline float determinant(const Mat3& a)
{
    return a.m[0][0] * a.m[1][1] - a.m[0][1] * a.m[1][0];
}

// The inverse, which undoes a matrix whose bottom row is 0, 0, 1. Its
// 2 by 2 block is a's block, inverted: the diagonal swapped, the other two
// negated, and all four divided by the determinant. Its move is a's move,
// taken through that block and reversed. A determinant of 0 flattens the
// plane, which can't be undone, so it gives the identity instead.
inline Mat3 inverse(const Mat3& a)
{
    Mat3 r;
    float det = determinant(a);
    if (det == 0.0f) return r;
    r.m[0][0] = a.m[1][1] / det;
    r.m[0][1] = -a.m[0][1] / det;
    r.m[1][0] = -a.m[1][0] / det;
    r.m[1][1] = a.m[0][0] / det;
    r.m[0][2] = -(r.m[0][0] * a.m[0][2] + r.m[0][1] * a.m[1][2]);
    r.m[1][2] = -(r.m[1][0] * a.m[0][2] + r.m[1][1] * a.m[1][2]);
    return r;
}
