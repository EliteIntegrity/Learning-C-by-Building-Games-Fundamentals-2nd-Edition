// SAT Visualizer - the Chapter 16 demo (the separating axis test) for
// Learning C++ by Building Games: Fundamentals
//
// A box to drag, and to turn with Left and Right, against a fixed shape: a
// pentagon, or with C, a circle. The separating axis test runs every frame.
// When the shapes are apart, the demo draws the line through the gap that
// the test found. When they overlap, it draws the shortest push that would
// move the box out: the minimum translation vector.

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "Vec2.h"

constexpr int WINDOW_WIDTH  = 1000;
constexpr int WINDOW_HEIGHT = 700;

constexpr SDL_Color TEXT = { 235, 238, 242, 255 };
constexpr SDL_Color GREY = { 90, 100, 112, 255 };
constexpr SDL_Color BLUE = { 47, 111, 235, 255 };
constexpr SDL_Color AMBER = { 232, 89, 12, 255 };
constexpr SDL_Color GREEN = { 47, 158, 68, 255 };
constexpr SDL_Color SILVER = { 150, 158, 170, 255 };

// How fast the held keys turn the box, in radians a second.
constexpr float TURN_SPEED = 1.8f;

// A circle: its center and its radius.
struct Circle
{
    Vec2 center;
    float radius;
};

// A polygon: its corners, in order around its edge, each once.
struct Polygon
{
    std::vector<Vec2> points;
};

// A shape's shadow on an axis: the least and the most of its points' dot
// products with the axis.
struct Interval
{
    float min;
    float max;
};

// What the separating axis test found. When the shapes overlap, axis is
// the way the box must move to get out, and depth is how far. When they're
// apart, axis is the one with the gap, and depth is the gap's width.
struct SatResult
{
    bool overlapping;
    Vec2 axis;
    float depth;
};

// Draw a line of text at (x, y). SDL_ttf turns the string into a surface,
// the surface is uploaded as a texture, and the texture is drawn.
void drawText(SDL_Renderer* renderer, TTF_Font* font,
              const std::string& text, float x, float y, SDL_Color color)
{
    if (!font) return;   // No font loaded: skip the label rather than crash.

    SDL_Surface* surface = TTF_RenderText_Blended(font, text.c_str(), 0, color);
    if (!surface) return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FRect dst = { x, y, (float)surface->w, (float)surface->h };
    SDL_RenderTexture(renderer, texture, nullptr, &dst);

    SDL_DestroySurface(surface);
    SDL_DestroyTexture(texture);
}

// A faint grid, with a line every 50 pixels, to read positions against.
void drawGrid(SDL_Renderer* renderer)
{
    SDL_SetRenderDrawColor(renderer, 38, 42, 50, 255);
    for (int x = 0; x < WINDOW_WIDTH; x += 50)
        SDL_RenderLine(renderer, (float)x, 0, (float)x, WINDOW_HEIGHT);
    for (int y = 0; y < WINDOW_HEIGHT; y += 50)
        SDL_RenderLine(renderer, 0, (float)y, WINDOW_WIDTH, (float)y);
}

// Draw an arrow from one point to another: a line, and a head of two short
// strokes back from the tip. The head needs the arrow's direction, and a
// quarter turn of it, which Chapters 3 and 4 explain.
void drawArrow(SDL_Renderer* renderer, Vec2 from, Vec2 to, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderLine(renderer, from.x, from.y, to.x, to.y);

    Vec2 d = to - from;
    float len = std::sqrt(d.x * d.x + d.y * d.y);
    if (len < 1.0f) return;
    Vec2 dir = d / len;
    Vec2 side = { -dir.y, dir.x };
    Vec2 wing1 = to - dir * 13.0f + side * 6.0f;
    Vec2 wing2 = to - dir * 13.0f - side * 6.0f;
    SDL_RenderLine(renderer, to.x, to.y, wing1.x, wing1.y);
    SDL_RenderLine(renderer, to.x, to.y, wing2.x, wing2.y);
}

// A dashed line from one point to another: 6-pixel dashes, 12 apart.
void drawDashed(SDL_Renderer* renderer, Vec2 from, Vec2 to, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    Vec2 d = to - from;
    float len = std::sqrt(d.x * d.x + d.y * d.y);
    if (len < 1.0f) return;
    Vec2 dir = d / len;
    for (float t = 0.0f; t < len; t += 12.0f)
    {
        Vec2 start = from + dir * t;
        Vec2 end = from + dir * SDL_min(t + 6.0f, len);
        SDL_RenderLine(renderer, start.x, start.y, end.x, end.y);
    }
}

// A circle's outline: 72 short lines around its edge, each from one point
// on the circle to the next, a 72nd of a turn on.
void drawCircleOutline(SDL_Renderer* renderer, Vec2 center, float radius,
                       SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    Vec2 prev = center + Vec2{ radius, 0.0f };
    for (int i = 1; i <= 72; ++i)
    {
        float angle = (float)i / 72.0f * 2.0f * PI;
        Vec2 p = center + Vec2::fromAngle(angle, radius);
        SDL_RenderLine(renderer, prev.x, prev.y, p.x, p.y);
        prev = p;
    }
}

// A closed outline through a list of points: a line from each point to
// the next, and from the last back to the first.
void drawPolygon(SDL_Renderer* renderer, const std::vector<Vec2>& points,
                 SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (size_t i = 0; i < points.size(); ++i)
    {
        const Vec2& a = points[i];
        const Vec2& b = points[(i + 1) % points.size()];
        SDL_RenderLine(renderer, a.x, a.y, b.x, b.y);
    }
}

// Set the color the renderer draws with, from an SDL_Color.
void setColor(SDL_Renderer* renderer, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

// Whether P is inside a convex polygon: on the same side of every edge, by
// Chapter 12's side test, just as Chapter 11's isConvex checks the turns
// at its corners. A point on an edge counts as inside.
bool insideConvex(Vec2 P, const Polygon& poly)
{
    bool right = false;
    bool left = false;
    size_t n = poly.points.size();
    for (size_t i = 0; i < n; ++i)
    {
        Vec2 a = poly.points[i];
        Vec2 b = poly.points[(i + 1) % n];
        float side = (b - a).cross(P - a);
        if (side > 0.0f) right = true;
        if (side < 0.0f) left = true;
    }
    return !(right && left);
}

// A polygon's shadow on an axis, a unit vector: how far along the axis its
// corners reach, by the dot product, from the least to the most.
Interval projectPolygon(const Polygon& poly, Vec2 axis)
{
    float first = poly.points[0].dot(axis);
    Interval shadow = { first, first };
    for (const Vec2& p : poly.points)
    {
        float d = p.dot(axis);
        shadow.min = std::min(shadow.min, d);
        shadow.max = std::max(shadow.max, d);
    }
    return shadow;
}

// A circle's shadow on an axis: its center's place on the axis, less and
// plus the radius, since a circle reaches as far in every direction.
Interval projectCircle(const Circle& c, Vec2 axis)
{
    float center = c.center.dot(axis);
    return { center - c.radius, center + c.radius };
}

// The unit normals of a polygon's edges: the axes it gives the test.
std::vector<Vec2> edgeNormals(const Polygon& poly)
{
    std::vector<Vec2> normals;
    size_t n = poly.points.size();
    for (size_t i = 0; i < n; ++i)
    {
        Vec2 edge = poly.points[(i + 1) % n] - poly.points[i];
        normals.push_back(edge.perp().normalized());
    }
    return normals;
}

// One axis of the test, with A's and B's shadows on it. A gap means the
// shapes are apart: the result says so, and this returns true, which ends
// the test. Otherwise, A could get out along this axis by moving its
// shadow past either end of B's, and the shorter way is kept, if it's the
// shortest push so far. A zero axis, from an edge with no length, says
// nothing, so it's skipped.
bool gapOnAxis(Vec2 axis, Interval a, Interval b, SatResult& result)
{
    if (axis.lengthSquared() < 0.5f) return false;
    float gap = std::max(b.min - a.max, a.min - b.max);
    if (gap > 0.0f)
    {
        result = { false, axis, gap };
        return true;
    }
    float forward = b.max - a.min;
    float backward = a.max - b.min;
    float push = std::min(forward, backward);
    if (push < result.depth)
        result = { true, (forward < backward) ? axis : -axis, push };
    return false;
}

// The separating axis test for two convex polygons: the normals of both
// polygons' edges are the axes. One gap ends it. An overlap on every one
// means the shapes collide, and the shortest push is kept.
SatResult satPolygons(const Polygon& A, const Polygon& B)
{
    std::vector<Vec2> axes = edgeNormals(A);
    for (Vec2 axis : edgeNormals(B)) axes.push_back(axis);

    SatResult result = { true, { 0.0f, 0.0f }, 1e9f };
    for (Vec2 axis : axes)
    {
        Interval a = projectPolygon(A, axis);
        Interval b = projectPolygon(B, axis);
        if (gapOnAxis(axis, a, b, result)) return result;
    }
    return result;
}

// The corner of a polygon that's nearest to a point.
Vec2 nearestCorner(const Polygon& poly, Vec2 P)
{
    Vec2 best = poly.points[0];
    for (const Vec2& p : poly.points)
        if ((p - P).lengthSquared() < (best - P).lengthSquared()) best = p;
    return best;
}

// The separating axis test for a convex polygon and a circle. A circle has
// no edges, so it gives one axis of its own: from the polygon's corner
// nearest the circle's center, toward the center.
SatResult satPolygonCircle(const Polygon& A, const Circle& B)
{
    std::vector<Vec2> axes = edgeNormals(A);
    axes.push_back((B.center - nearestCorner(A, B.center)).normalized());

    SatResult result = { true, { 0.0f, 0.0f }, 1e9f };
    for (Vec2 axis : axes)
    {
        Interval a = projectPolygon(A, axis);
        Interval b = projectCircle(B, axis);
        if (gapOnAxis(axis, a, b, result)) return result;
    }
    return result;
}

// A shape placed in the world: its own corners, around its center, turned
// by its angle and moved to its position, as Chapter 6 placed shapes.
Polygon placed(const std::vector<Vec2>& corners, Vec2 pos, float angle)
{
    Polygon poly;
    for (const Vec2& c : corners) poly.points.push_back(pos + c.rotated(angle));
    return poly;
}

int main(int argc, char* argv[])
{
    // Start SDL and SDL_ttf. Each returns false if it fails.
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    if (!TTF_Init())
    {
        SDL_Log("TTF_Init failed: %s", SDL_GetError());
        return 1;
    }

    // The window, and a renderer that waits for the screen to refresh
    // (vsync), so the loop runs once for every frame the screen shows.
    SDL_Window* window =
        SDL_CreateWindow("SAT Visualizer", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The box, A: its corners around its center, its position and its
    // angle. The mouse drags it, and the keys turn it.
    const std::vector<Vec2> boxCorners = { { -70, -45 }, { 70, -45 },
                                           { 70, 45 }, { -70, 45 } };
    Vec2 posA = { 320, 300 };
    float angleA = 0.3f;
    bool grabbed = false;
    Vec2 lastMouse = { 0.0f, 0.0f };

    // The fixed shape, B: a regular pentagon around (620, 380), or a
    // circle there with the same radius, which C switches between.
    const Vec2 posB = { 620, 380 };
    std::vector<Vec2> pentagonCorners;
    for (int i = 0; i < 5; ++i)
    {
        float angle = -PI / 2 + (float)i * 2.0f * PI / 5;
        pentagonCorners.push_back(Vec2::fromAngle(angle, 80.0f));
    }
    const Polygon pentagon = placed(pentagonCorners, posB, 0.0f);
    const Circle circle = { posB, 80.0f };
    bool useCircle = false;

    // Delta time: the seconds since the last frame, from SDL's counter.
    Uint64 previous = SDL_GetPerformanceCounter();
    const double frequency = (double)SDL_GetPerformanceFrequency();

    bool running = true;
    while (running)
    {
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)((now - previous) / frequency);
        previous = now;
        if (dt > 0.05f) dt = 0.05f;   // A long stall counts as 1/20 s.

        // Events: quitting, and the keys and clicks that happen once.
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT) running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN)
            {
                if (event.key.key == SDLK_ESCAPE) running = false;
                else if (event.key.key == SDLK_C) useCircle = !useCircle;
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                     event.button.button == SDL_BUTTON_LEFT)
            {
                Vec2 press = { event.button.x, event.button.y };
                Polygon box = placed(boxCorners, posA, angleA);
                if (insideConvex(press, box)) grabbed = true;
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) grabbed = false;
        }

        // The mouse's position, read once a frame.
        float mx, my;
        SDL_GetMouseState(&mx, &my);

        // The held keys turn the box, at TURN_SPEED radians a second.
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_LEFT]) angleA -= TURN_SPEED * dt;
        if (keys[SDL_SCANCODE_RIGHT]) angleA += TURN_SPEED * dt;

        // A held box moves as far as the mouse has since last frame.
        Vec2 mouse = { mx, my };
        if (grabbed) posA += mouse - lastMouse;
        lastMouse = mouse;

        // The test, against whichever shape B is.
        Polygon box = placed(boxCorners, posA, angleA);
        SatResult result = useCircle ? satPolygonCircle(box, circle)
                                     : satPolygons(box, pentagon);

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);
        drawGrid(renderer);

        // The shapes: amber when they overlap, and blue and green when
        // they don't.
        SDL_Color colorA = result.overlapping ? AMBER : BLUE;
        SDL_Color colorB = result.overlapping ? AMBER : GREEN;
        drawPolygon(renderer, box.points, colorA);
        if (useCircle)
            drawCircleOutline(renderer, circle.center, circle.radius, colorB);
        else
            drawPolygon(renderer, pentagon.points, colorB);

        if (result.overlapping)
        {
            // The shortest push, as an arrow from the box's center, and
            // the box where the push would leave it, just touching.
            Vec2 tip = posA + result.axis * result.depth;
            drawArrow(renderer, posA, tip, AMBER);
            drawPolygon(renderer, placed(boxCorners, tip, angleA).points, GREY);
        }
        else
        {
            // The line through the middle of the gap, at right angles to
            // the axis that found it: the ruler that proves they're apart.
            Interval a = projectPolygon(box, result.axis);
            Interval b = useCircle ? projectCircle(circle, result.axis)
                                   : projectPolygon(pentagon, result.axis);
            float middle = (a.max < b.min) ? (a.max + b.min) / 2.0f
                                           : (b.max + a.min) / 2.0f;
            Vec2 halfway = lerp(posA, posB, 0.5f);
            Vec2 onLine =
                halfway + result.axis * (middle - halfway.dot(result.axis));
            Vec2 along = result.axis.perp();
            drawDashed(renderer, onLine - along * 700.0f,
                       onLine + along * 700.0f, GREY);
        }

        // The readout: what the test found, and the axes it had to try.
        std::string depth = std::to_string(std::lround(result.depth));
        drawText(renderer, font,
                 result.overlapping
                     ? "overlapping: push the box " + depth + " px, as shown"
                     : "apart: a gap of " + depth + " px on one axis",
                 12, 10, result.overlapping ? AMBER : TEXT);
        drawText(renderer, font,
                 useCircle ? "axes: the box's 4 edge normals, and 1 toward "
                             "the circle's center"
                           : "axes: the box's 4 edge normals, and the "
                             "pentagon's 5",
                 12, 30, SILVER);
        drawText(renderer, font,
                 "[Drag] the box   [Left] [Right] turn it   "
                 "[C] pentagon or circle   [Esc] quit",
                 12, WINDOW_HEIGHT - 26, GREY);

        SDL_RenderPresent(renderer);
    }

    // Clean up, in the reverse order of creation.
    if (font) TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
