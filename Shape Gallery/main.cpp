// Shape Gallery - the Chapter 11 demo (representing shapes) for
// Learning C++ by Building Games: Fundamentals
//
// Five shapes, each held in a small struct, and each with handles to drag:
// a circle, an axis-aligned box, a segment, a ray and a five-sided polygon.
// The numbers beside each shape come from its struct, so they're exactly
// the data it holds. The polygon says whether it's convex, and rings each
// corner in red where it turns back. B shows each shape's bounding box.

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
constexpr SDL_Color RED = { 224, 49, 49, 255 };
constexpr SDL_Color FAINT = { 70, 78, 90, 255 };
constexpr SDL_Color SILVER = { 150, 158, 170, 255 };

// A segment: its two ends.
struct Segment
{
    Vec2 a;
    Vec2 b;
};

// A ray: where it starts, and the way it goes, as a unit vector.
struct Ray
{
    Vec2 origin;
    Vec2 dir;
};

// A circle: its center and its radius.
struct Circle
{
    Vec2 center;
    float radius;
};

// An axis-aligned box: its smallest and largest corners. Neither of min's
// coordinates is ever more than max's.
struct AABB
{
    Vec2 min;
    Vec2 max;
};

// A polygon: its corners, in order around its edge, each once.
struct Polygon
{
    std::vector<Vec2> points;
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

// A point as text, in whole pixels, such as (210, 210).
std::string fmtPoint(Vec2 p)
{
    return "(" + std::to_string(std::lround(p.x)) + ", " +
           std::to_string(std::lround(p.y)) + ")";
}

// A box from two opposite corners, in either order: the smaller of each
// coordinate goes in min, and the larger in max.
AABB boxFromCorners(Vec2 p, Vec2 q)
{
    return { { std::min(p.x, q.x), std::min(p.y, q.y) },
             { std::max(p.x, q.x), std::max(p.y, q.y) } };
}

// The bounding box of some points: the smallest box around them all.
AABB boundsOf(const std::vector<Vec2>& points)
{
    AABB box = { points[0], points[0] };
    for (const Vec2& p : points)
    {
        box.min = { std::min(box.min.x, p.x), std::min(box.min.y, p.y) };
        box.max = { std::max(box.max.x, p.x), std::max(box.max.y, p.y) };
    }
    return box;
}

// A circle's bounding box: its center, less and plus its radius each way.
AABB boundsOf(const Circle& c)
{
    Vec2 r = { c.radius, c.radius };
    return { c.center - r, c.center + r };
}

// A box's four corners, in order around it, for drawing.
std::vector<Vec2> corners(const AABB& box)
{
    return { box.min, { box.max.x, box.min.y }, box.max,
             { box.min.x, box.max.y } };
}

// The turn at a polygon's corner i: the cross product of the edge coming
// in and the edge going out. On the screen, it's positive where the edge
// turns right, and negative where it turns left.
float turnAt(const Polygon& poly, size_t i)
{
    size_t n = poly.points.size();
    Vec2 before = poly.points[(i + n - 1) % n];
    Vec2 here = poly.points[i];
    Vec2 after = poly.points[(i + 1) % n];
    return (here - before).cross(after - here);
}

// A polygon is convex when every corner turns the same way.
bool isConvex(const Polygon& poly)
{
    bool turnsRight = false;
    bool turnsLeft = false;
    for (size_t i = 0; i < poly.points.size(); ++i)
    {
        float turn = turnAt(poly, i);
        if (turn > 0.0f) turnsRight = true;
        if (turn < 0.0f) turnsLeft = true;
    }
    return !(turnsRight && turnsLeft);
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
        SDL_CreateWindow("Shape Gallery", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The shapes' handles. A segment's two ends are handles themselves,
    // and so are a polygon's corners. The circle, the box and the ray are
    // worked out from their handles each frame.
    Vec2 circleCenter = { 210, 210 }, circleRim = { 288, 210 };
    Vec2 cornerA = { 430, 150 }, cornerB = { 580, 260 };
    Segment segment = { { 720, 150 }, { 860, 270 } };
    Vec2 rayOrigin = { 120, 480 }, rayToward = { 260, 520 };
    Polygon polygon = { { { 560, 450 }, { 650, 410 }, { 730, 460 },
                          { 700, 560 }, { 590, 560 } } };

    // Pointers to every handle, so that one loop can grab any of them.
    std::vector<Vec2*> handles = { &circleCenter, &circleRim, &cornerA,
                                   &cornerB, &segment.a, &segment.b,
                                   &rayOrigin, &rayToward };
    for (Vec2& p : polygon.points) handles.push_back(&p);
    Vec2* grabbed = nullptr;
    bool showBounds = false;

    bool running = true;
    while (running)
    {
        // Events: quitting, and the keys and clicks that happen once.
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT) running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN)
            {
                if (event.key.key == SDLK_ESCAPE) running = false;
                else if (event.key.key == SDLK_B) showBounds = !showBounds;
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                     event.button.button == SDL_BUTTON_LEFT)
            {
                Vec2 press = { event.button.x, event.button.y };
                for (Vec2* h : handles)
                {
                    if (distance(press, *h) < 15.0f)
                    {
                        grabbed = h;
                        break;
                    }
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) grabbed = nullptr;
        }

        // The mouse's position, read once a frame.
        float mx, my;
        SDL_GetMouseState(&mx, &my);

        // A grabbed handle follows the mouse, written through its pointer.
        if (grabbed) *grabbed = { mx, my };

        // The shapes that are worked out from their handles.
        Circle circle = { circleCenter, distance(circleCenter, circleRim) };
        AABB box = boxFromCorners(cornerA, cornerB);
        Ray ray = { rayOrigin, (rayToward - rayOrigin).normalized() };

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);
        drawGrid(renderer);

        // The bounding boxes, faint, under the shapes. The box is its own,
        // and a ray, which runs forever, has none.
        if (showBounds)
        {
            drawPolygon(renderer, corners(boundsOf(circle)), FAINT);
            drawPolygon(renderer, corners(boundsOf({ segment.a, segment.b })),
                        FAINT);
            drawPolygon(renderer, corners(boundsOf(polygon.points)), FAINT);
        }

        // Each shape, with its name and then its struct's data, a line for
        // each member. The circle comes first.
        drawCircleOutline(renderer, circle.center, circle.radius, BLUE);
        Vec2 at = circle.center + Vec2{ -60.0f, circle.radius + 10.0f };
        drawText(renderer, font, "Circle", at.x, at.y, TEXT);
        drawText(renderer, font, "center " + fmtPoint(circle.center),
                 at.x, at.y + 20, SILVER);
        drawText(renderer, font,
                 "radius " + std::to_string(std::lround(circle.radius)),
                 at.x, at.y + 40, SILVER);

        // The box.
        drawPolygon(renderer, corners(box), BLUE);
        at = { box.min.x, box.max.y + 10.0f };
        drawText(renderer, font, "AABB", at.x, at.y, TEXT);
        drawText(renderer, font, "min " + fmtPoint(box.min), at.x, at.y + 20,
                 SILVER);
        drawText(renderer, font, "max " + fmtPoint(box.max), at.x, at.y + 40,
                 SILVER);

        // The segment.
        setColor(renderer, BLUE);
        SDL_RenderLine(renderer, segment.a.x, segment.a.y, segment.b.x,
                       segment.b.y);
        at = { std::min(segment.a.x, segment.b.x),
               std::max(segment.a.y, segment.b.y) + 10.0f };
        drawText(renderer, font, "Segment", at.x, at.y, TEXT);
        drawText(renderer, font, "a " + fmtPoint(segment.a), at.x, at.y + 20,
                 SILVER);
        drawText(renderer, font, "b " + fmtPoint(segment.b), at.x, at.y + 40,
                 SILVER);

        // The ray, drawn 300 pixels long, though it never ends.
        drawArrow(renderer, ray.origin, ray.origin + ray.dir * 300.0f, BLUE);
        at = ray.origin + Vec2{ 0.0f, -66.0f };
        drawText(renderer, font, "Ray", at.x, at.y, TEXT);
        drawText(renderer, font, "origin " + fmtPoint(ray.origin),
                 at.x, at.y + 20, SILVER);
        drawText(renderer, font,
                 "dir (" + fmt2(ray.dir.x) + ", " + fmt2(ray.dir.y) + ")",
                 at.x, at.y + 40, SILVER);

        // The polygon, with each corner ringed: green where it turns the
        // way most corners do, and red where it turns back, as a dent does.
        bool convex = isConvex(polygon);
        drawPolygon(renderer, polygon.points, BLUE);
        size_t n = polygon.points.size();
        size_t rights = 0;
        for (size_t i = 0; i < n; ++i)
            if (turnAt(polygon, i) > 0.0f) ++rights;
        bool mostlyRight = rights * 2 > n;
        for (size_t i = 0; i < n; ++i)
        {
            bool right = turnAt(polygon, i) > 0.0f;
            SDL_Color ring = (right == mostlyRight) ? GREEN : RED;
            drawCircleOutline(renderer, polygon.points[i], 11.0f, ring);
        }
        at = { boundsOf(polygon.points).min.x,
               boundsOf(polygon.points).max.y + 14.0f };
        drawText(renderer, font, "Polygon", at.x, at.y, TEXT);
        drawText(renderer, font, std::to_string(n) + " points", at.x,
                 at.y + 20, SILVER);
        drawText(renderer, font, convex ? "convex" : "concave", at.x,
                 at.y + 40, convex ? GREEN : RED);

        // Every handle, so you can see what's draggable.
        for (Vec2* h : handles)
        {
            setColor(renderer, (h == grabbed) ? TEXT : AMBER);
            fillCircle(renderer, *h, 5.0f);
        }

        drawText(renderer, font,
                 "[Drag] an amber handle   [B] bounding boxes   [Esc] quit",
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
