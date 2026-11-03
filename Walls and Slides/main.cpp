// Walls and Slides - the Chapter 18 demo (collision response) for
// Learning C++ by Building Games: Fundamentals
//
// A blue ball chases the mouse. It can't pass through the grey walls, and
// slides along them, and it bounces off the amber bumpers. Each step,
// detection finds every contact, with a normal and a depth, and the
// response pushes the ball out and fixes its velocity. V turns the fix of
// the velocity off, to show what it's for, and P switches between one
// pass over the walls and eight. A click puts the ball at the mouse.

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
constexpr SDL_Color SILVER = { 150, 158, 170, 255 };
constexpr SDL_Color WALL = { 104, 112, 124, 255 };

// The simulation's fixed step, a sixtieth of a second, and the ball's
// radius.
constexpr float STEP = 1.0f / 60.0f;
constexpr float RADIUS = 16.0f;

// How the ball chases the mouse: its acceleration, in pixels a second,
// every second; its drag, as in Chapter 10; and its top speed, in pixels
// a second.
constexpr float ACCEL = 2000.0f;
constexpr float DRAG = 2.2f;
constexpr float MAX_SPEED = 650.0f;

// The bumpers' restitution: the ball leaves them at 90% of the speed it
// came in with, across their surface.
constexpr float BOUNCE = 0.9f;

// An axis-aligned box: its smallest and largest corners. Neither of min's
// coordinates is ever more than max's.
struct AABB
{
    Vec2 min;
    Vec2 max;
};

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
// the way the first shape must move to get out, and depth is how far.
// When they're apart, axis is the one with the gap, and depth is the
// gap's width.
struct SatResult
{
    bool overlapping;
    Vec2 axis;
    float depth;
};

// What detection finds, for a response to use: whether a ball touches a
// shape, the unit normal pointing out of the shape toward the ball, and
// how far the ball has sunk in along it.
struct Contact
{
    bool touching;
    Vec2 normal;
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

// Fill a circle. SDL draws lines but not discs, so we draw one horizontal
// line for each row of pixels, as wide as Pythagoras says the circle is
// at that row.
void fillCircle(SDL_Renderer* renderer, Vec2 center, float radius)
{
    for (int dy = -(int)radius; dy <= (int)radius; ++dy)
    {
        float dx = std::sqrt(radius * radius - (float)(dy * dy));
        SDL_RenderLine(renderer, center.x - dx, center.y + dy,
                       center.x + dx, center.y + dy);
    }
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

// A number as text with two decimals, such as 0.77.
std::string fmt2(float v)
{
    char buf[32];
    SDL_snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

// Set the color the renderer draws with, from an SDL_Color.
void setColor(SDL_Renderer* renderer, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

// A box's four corners, in order around it, for drawing.
std::vector<Vec2> corners(const AABB& box)
{
    return { box.min, { box.max.x, box.min.y }, box.max,
             { box.min.x, box.max.y } };
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

// Fill a convex polygon, a row of pixels at a time, as fillCircle fills a
// circle. Each row runs between the two places where it crosses the
// polygon's edges, found as Chapter 15's crossing test found them.
void fillConvex(SDL_Renderer* renderer, const Polygon& poly)
{
    float top = poly.points[0].y;
    float bottom = top;
    for (const Vec2& p : poly.points)
    {
        top = std::min(top, p.y);
        bottom = std::max(bottom, p.y);
    }
    size_t n = poly.points.size();
    for (float y = std::ceil(top); y <= bottom; y += 1.0f)
    {
        float left = 1e9f;
        float right = -1e9f;
        for (size_t i = 0; i < n; ++i)
        {
            Vec2 a = poly.points[i];
            Vec2 b = poly.points[(i + 1) % n];
            if ((a.y > y) != (b.y > y))
            {
                float x = lerp(a.x, b.x, inverseLerp(a.y, b.y, y));
                left = std::min(left, x);
                right = std::max(right, x);
            }
        }
        if (left <= right) SDL_RenderLine(renderer, left, y, right, y);
    }
}

// Detection for a ball against a convex wall, by Chapter 16's test, with
// the wall as the first shape. The test's axis is the way the wall would
// move out of the ball, so the ball's way out of the wall is the opposite.
Contact contactWall(const Circle& ball, const Polygon& wall)
{
    SatResult result = satPolygonCircle(wall, ball);
    if (!result.overlapping) return { false, { 0.0f, 0.0f }, 0.0f };
    return { true, -result.axis, result.depth };
}

// Detection for a ball against a round bumper. They touch when their
// centers are no farther apart than their radii added together. The
// normal runs from the bumper's center to the ball's, and the depth is how
// much nearer than that they are. Centers in the same place give no
// direction, so the ball goes straight right, as Chapter 13 chose.
Contact contactBumper(const Circle& ball, const Circle& bumper)
{
    Vec2 out = ball.center - bumper.center;
    float dist = out.length();
    float reach = ball.radius + bumper.radius;
    if (dist > reach) return { false, { 0.0f, 0.0f }, 0.0f };
    Vec2 normal = (dist > 0.0001f) ? out / dist : Vec2{ 1.0f, 0.0f };
    return { true, normal, reach - dist };
}

// The response to a contact. The ball moves out along the normal by the
// depth, so that it just touches. Then the part of its velocity going
// into the shape is taken away, times 1 plus the restitution: 0 leaves the
// ball sliding along the surface, and 1 sends it back out as fast as it
// went in. Walls and Slides can skip the velocity, to show what it's for.
void respond(Vec2& pos, Vec2& vel, const Contact& c, float restitution,
             bool fixVelocity)
{
    pos += c.normal * c.depth;
    if (!fixVelocity) return;
    float across = vel.dot(c.normal);
    if (across < 0.0f) vel -= c.normal * ((1.0f + restitution) * across);
}

// How deep the ball still is in anything, after the response: the deepest
// of its contacts, or 0 if it touches nothing.
float deepestOverlap(Vec2 pos, const std::vector<Polygon>& walls,
                     const std::vector<Circle>& bumpers)
{
    float deepest = 0.0f;
    for (const Polygon& wall : walls)
    {
        Contact c = contactWall({ pos, RADIUS }, wall);
        deepest = std::max(deepest, c.depth);
    }
    for (const Circle& bumper : bumpers)
    {
        Contact c = contactBumper({ pos, RADIUS }, bumper);
        deepest = std::max(deepest, c.depth);
    }
    return deepest;
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
        SDL_CreateWindow("Walls and Slides", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The walls, each a convex polygon: a long wall made of two boxes, a
    // wall down from its left end, a narrow wedge of two thin boxes, each
    // turned 10 degrees, and four boxes just outside the window's edges.
    const std::vector<Vec2> arm = { { -15, -140 }, { 15, -140 },
                                    { 15, 140 }, { -15, 140 } };
    const std::vector<Polygon> walls = {
        { corners({ { 220, 120 }, { 440, 160 } }) },
        { corners({ { 440, 120 }, { 660, 160 } }) },
        { corners({ { 220, 160 }, { 260, 440 } }) },
        placed(arm, { 776, 462 }, -0.1745f),
        placed(arm, { 824, 462 }, 0.1745f),
        { corners({ { -100, -100 }, { 1100, 0 } }) },
        { corners({ { -100, 700 }, { 1100, 800 } }) },
        { corners({ { -100, 0 }, { 0, 700 } }) },
        { corners({ { 1000, 0 }, { 1100, 700 } }) },
    };

    // The bumpers, which the ball bounces off.
    const std::vector<Circle> bumpers = { { { 560, 330 }, 40.0f },
                                          { { 400, 540 }, 34.0f },
                                          { { 640, 560 }, 30.0f } };

    // The ball. Then the settings: whether the response fixes the
    // velocity, and how many passes it makes over the walls. And the time
    // not yet stepped.
    Vec2 pos = { 120, 350 };
    Vec2 vel = { 0, 0 };
    bool fixVelocity = true;
    int passes = 8;
    float accumulator = 0.0f;

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
                else if (event.key.key == SDLK_V) fixVelocity = !fixVelocity;
                else if (event.key.key == SDLK_P)
                    passes = (passes == 1) ? 8 : 1;
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                     event.button.button == SDL_BUTTON_LEFT)
            {
                // A click puts the ball where the mouse is, at rest.
                pos = { event.button.x, event.button.y };
                vel = { 0.0f, 0.0f };
            }
        }

        // The mouse's position, read once a frame.
        float mx, my;
        SDL_GetMouseState(&mx, &my);

        // The fixed step, as in Bullet Time: the frame's time goes into the
        // accumulator, and the ball takes as many whole steps as it holds.
        Vec2 mouse = { mx, my };
        accumulator += dt;
        while (accumulator >= STEP)
        {
            accumulator -= STEP;

            // The ball's move: toward the mouse, with drag and a top speed.
            Vec2 toMouse = mouse - pos;
            if (toMouse.length() > 4.0f)
                vel += toMouse.normalized() * (ACCEL * STEP);
            vel *= std::exp(-DRAG * STEP);
            vel = vel.truncated(MAX_SPEED);
            pos += vel * STEP;

            // Detection, then response, for every wall and every bumper,
            // in as many passes as the setting asks for.
            for (int pass = 0; pass < passes; ++pass)
            {
                for (const Polygon& wall : walls)
                {
                    Contact c = contactWall({ pos, RADIUS }, wall);
                    if (c.touching) respond(pos, vel, c, 0.0f, fixVelocity);
                }
                for (const Circle& bumper : bumpers)
                {
                    Contact c = contactBumper({ pos, RADIUS }, bumper);
                    if (c.touching) respond(pos, vel, c, BOUNCE, fixVelocity);
                }
            }
        }

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);
        drawGrid(renderer);

        // The walls, and the bumpers.
        setColor(renderer, WALL);
        for (const Polygon& wall : walls) fillConvex(renderer, wall);
        setColor(renderer, AMBER);
        for (const Circle& bumper : bumpers)
            fillCircle(renderer, bumper.center, bumper.radius);

        // The ball, with an arrow for its velocity: how far it would go in
        // 0.15 seconds.
        setColor(renderer, BLUE);
        fillCircle(renderer, pos, RADIUS);
        if (vel.length() > 20.0f)
            drawArrow(renderer, pos, pos + vel * 0.15f, TEXT);

        // The readout: the ball's speed and the deepest overlap the passes
        // left, then the settings.
        float deepest = deepestOverlap(pos, walls, bumpers);
        drawText(renderer, font,
                 "speed " + std::to_string(std::lround(vel.length())) +
                 " px/s   deepest overlap left " + fmt2(deepest) + " px",
                 12, 10, TEXT);
        drawText(renderer, font,
                 std::string("velocity fix ") + (fixVelocity ? "on" : "off") +
                 "   passes " + std::to_string(passes),
                 12, 30, SILVER);
        drawText(renderer, font,
                 "[Click] place the ball   [V] velocity fix   [P] passes   "
                 "[Esc] quit", 12, WINDOW_HEIGHT - 26, GREY);

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
