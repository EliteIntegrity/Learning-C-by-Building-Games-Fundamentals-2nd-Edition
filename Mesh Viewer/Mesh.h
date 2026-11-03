#pragma once
#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "Vec3.h"

// Mesh: a model as a list of corners and a list of faces (Chapter 32). Each
// face is a loop of indices into the corners, counterclockwise as seen from
// outside: three for a triangle, four for a square, or more.
struct Mesh
{
    std::string name;
    std::vector<Vec3> verts;
    std::vector<std::vector<int>> faces;
};

// The average of a mesh's corners, which is inside it when it's convex.
inline Vec3 meshCenter(const Mesh& m)
{
    Vec3 sum;
    for (Vec3 v : m.verts) sum += v;
    return m.verts.empty() ? sum : sum / (float)m.verts.size();
}

// Move a mesh so that the average of its corners is at the origin, so that
// it turns about its middle.
inline void recenter(Mesh& m)
{
    Vec3 c = meshCenter(m);
    for (Vec3& v : m.verts) v -= c;
}

// A face's normal: the sum of the cross products of its fan of triangles,
// each from its first corner. A face whose first three corners lie on one
// line still gets its normal from the others.
inline Vec3 faceNormal(const Mesh& m, const std::vector<int>& f)
{
    Vec3 n;
    for (size_t k = 1; k + 1 < f.size(); ++k)
        n += (m.verts[f[k]] - m.verts[f[0]]).cross(m.verts[f[k + 1]] -
                                                   m.verts[f[0]]);
    return n;
}

// Reverse every face whose normal points in toward the mesh's center, so
// that every face is counterclockwise from outside. That's right for a
// convex mesh, whose center is inside every face's back.
inline void fixWinding(Mesh& m)
{
    Vec3 c = meshCenter(m);
    for (std::vector<int>& f : m.faces)
    {
        Vec3 middle;
        for (int i : f) middle += m.verts[i];
        middle = middle / (float)f.size();
        if (faceNormal(m, f).dot(middle - c) < 0.0f)
            std::reverse(f.begin(), f.end());
    }
}

// Load a Wavefront .obj file: its "v x y z" lines are corners, and its
// "f a b c" lines are faces. A face's corner can be written as a/b/c, and
// only a, the corner's number, is read. A number counts from 1, or from
// the end if it's negative, so that -1 is the last corner so far. Anything
// unreadable, or a number that names no corner, gives false.
inline bool loadObj(const std::string& path, Mesh& out)
{
    std::ifstream in(path);
    if (!in) return false;
    Mesh m;
    m.name = path.substr(path.find_last_of("/\\") + 1);
    std::string line;
    while (std::getline(in, line))
    {
        std::istringstream words(line);
        std::string tag;
        words >> tag;
        if (tag == "v")
        {
            Vec3 v;
            if (!(words >> v.x >> v.y >> v.z)) return false;
            m.verts.push_back(v);
        }
        else if (tag == "f")
        {
            std::vector<int> face;
            std::string word;
            while (words >> word)
            {
                std::istringstream number(word);
                int index = 0;
                if (!(number >> index) || index == 0) return false;
                if (index > 0) face.push_back(index - 1);
                else face.push_back((int)m.verts.size() + index);
            }
            if (face.size() >= 3) m.faces.push_back(face);
        }
    }
    for (const std::vector<int>& f : m.faces)
        for (int i : f)
            if (i < 0 || i >= (int)m.verts.size()) return false;
    if (m.faces.empty()) return false;
    recenter(m);
    fixWinding(m);
    out = m;
    return true;
}

// A cube, 2 units on a side, with Transforming a Cube's corners, and its six
// square faces: the front, back, right, left, top and bottom.
inline Mesh builtinCube()
{
    Mesh m;
    m.name = "cube";
    m.verts = { { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
                { -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 } };
    m.faces = { { 4, 5, 6, 7 }, { 1, 0, 3, 2 }, { 5, 1, 2, 6 },
                { 0, 4, 7, 3 }, { 7, 6, 2, 3 }, { 0, 1, 5, 4 } };
    return m;
}

// An octahedron: a corner 1 along each axis, both ways, and eight
// triangles, the four around the top corner and then the four around the
// bottom one.
inline Mesh builtinOctahedron()
{
    Mesh m;
    m.name = "octahedron";
    m.verts = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 },
                { 0, 0, 1 }, { 0, 0, -1 } };
    m.faces = { { 2, 4, 0 }, { 2, 1, 4 }, { 2, 5, 1 }, { 2, 0, 5 },
                { 3, 0, 4 }, { 3, 4, 1 }, { 3, 1, 5 }, { 3, 5, 0 } };
    return m;
}

// A tetrahedron: four of the cube's corners, no two on one of its edges,
// and four triangles.
inline Mesh builtinTetrahedron()
{
    Mesh m;
    m.name = "tetrahedron";
    m.verts = { { 1, 1, 1 }, { 1, -1, -1 }, { -1, 1, -1 }, { -1, -1, 1 } };
    m.faces = { { 0, 1, 2 }, { 0, 3, 1 }, { 0, 2, 3 }, { 1, 3, 2 } };
    return m;
}
