// Magnet - the Chapter 13 demo (closest points) for
// Learning C++ by Building Games: Fundamentals
//
// Four shapes sit still: a segment, a circle, a box and a triangle, each
// held in one of Chapter 11's structs. Wherever the mouse is, the demo
// finds the closest point on each shape, and draws a line to it, a dot on
// it, and its distance. The readout gives the segment's fraction, before
// and after the clamp. S switches the circle, the box and the triangle
// between boundaries, where a point inside goes to the nearest edge, and
// solids, where a point inside is its own closest point.

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

// A segment: its two ends.
struct Segment
{
    Vec2 a;
    Vec2 b;
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

// A box's four corners, in order around it, for drawing.
std::vector<Vec2> corners(const AABB& box)
{
    return { box.min, { box.max.x, box.min.y }, box.max,
             { box.min.x, box.max.y } };
}

// How far along the segment from A to B the foot of the perpendicular
// from P lands, as a fraction: 0 at A and 1 at B, and less than 0 or more
// than 1 when the foot is beyond an end. A segment with no length has no
// direction to measure along, so it gives 0.
float fractionAlong(Vec2 P, Vec2 A, Vec2 B)
{
    Vec2 ab = B - A;
    float lengthSq = ab.lengthSquared();
    if (lengthSq < 0.0001f) return 0.0f;
    return (P - A).dot(ab) / lengthSq;
}

// The closest point to P on the segment from A to B: the foot of the
// perpendicular, with its fraction clamped to the segment's 0 to 1.
Vec2 closestOnSegment(Vec2 P, Vec2 A, Vec2 B)
{
    float t = std::clamp(fractionAlong(P, A, B), 0.0f, 1.0f);
    return lerp(A, B, t);
}

// The closest point to P on a circle's rim: the rim point in P's direction
// from the center. From the center itself, every rim point is as close as
// any other, so the one straight to the right is chosen.
Vec2 closestOnCircle(Vec2 P, const Circle& circle)
{
    Vec2 out = P - circle.center;
    float len = out.length();
    if (len < 0.0001f) return circle.center + Vec2{ circle.radius, 0.0f };
    return circle.center + out * (circle.radius / len);
}

// The closest point to P in a box, counting its inside: P with each
// coordinate clamped into the box's range. A point inside the box is its
// own closest point.
Vec2 closestInBox(Vec2 P, const AABB& box)
{
    return { std::clamp(P.x, box.min.x, box.max.x),
             std::clamp(P.y, box.min.y, box.max.y) };
}

// The closest point to P on a polygon's edges: the nearest of the closest
// points on each edge. Squared distances are compared, since the nearest
// point has the smallest of those too, and they need no square root.
Vec2 closestOnPolygon(Vec2 P, const Polygon& poly)
{
    size_t n = poly.points.size();
    Vec2 best = poly.points[0];
    float bestDistSq = 1e18f;
    for (size_t i = 0; i < n; ++i)
    {
        Vec2 a = poly.points[i];
        Vec2 b = poly.points[(i + 1) % n];
        Vec2 c = closestOnSegment(P, a, b);
        float distSq = (c - P).lengthSquared();
        if (distSq < bestDistSq)
        {
            bestDistSq = distSq;
            best = c;
        }
    }
    return best;
}

// The closest point to P in a circle, counting its inside: P itself when
// it's inside, and the rim point otherwise.
Vec2 closestInCircle(Vec2 P, const Circle& circle)
{
    float r = circle.radius;
    if ((P - circle.center).lengthSquared() <= r * r) return P;
    return closestOnCircle(P, circle);
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

// The closest point to P in a convex polygon, counting its inside.
Vec2 closestInPolygon(Vec2 P, const Polygon& poly)
{
    return insideConvex(P, poly) ? P : closestOnPolygon(P, poly);
}

// The closest point to P on a box's edges, not counting its inside: the
// box as a polygon, its four corners in order around it.
Vec2 closestOnBox(Vec2 P, const AABB& box)
{
    return closestOnPolygon(P, { corners(box) });
}

// A closest point, shown: a line to it from the mouse, a dot on it, and
// its distance, in whole pixels.
void showClosest(SDL_Renderer* renderer, TTF_Font* font, Vec2 mouse,
                 Vec2 closest)
{
    setColor(renderer, SILVER);
    SDL_RenderLine(renderer, mouse.x, mouse.y, closest.x, closest.y);
    setColor(renderer, AMBER);
    fillCircle(renderer, closest, 5.0f);

    // The distance, centered just past the dot, on the far side from the
    // mouse, so it's clear of the line. When the mouse is on the dot,
    // there's no far side, so it goes above. TTF_GetStringSize measures
    // the text, in pixels, so that it can be centered.
    float dist = distance(mouse, closest);
    Vec2 away = (dist > 0.5f) ? (closest - mouse) / dist : Vec2{ 0.0f, -1.0f };
    std::string label = std::to_string(std::lround(dist));
    int w = 0, h = 0;
    if (font) TTF_GetStringSize(font, label.c_str(), 0, &w, &h);
    Vec2 at = closest + away * 22.0f;
    drawText(renderer, font, label, at.x - w / 2.0f, at.y - h / 2.0f, AMBER);
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
        SDL_CreateWindow("Magnet", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The shapes, in Chapter 11's structs. They stay where they are, and
    // the mouse does the moving.
    const Segment segment = { { 150, 160 }, { 400, 120 } };
    const Circle circle = { { 720, 200 }, 95.0f };
    const AABB box = { { 150, 420 }, { 340, 580 } };
    const Polygon triangle = { { { 560, 430 }, { 780, 410 }, { 690, 610 } } };

    // Whether the circle, the box and the triangle count their insides.
    bool solid = false;

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
                else if (event.key.key == SDLK_S) solid = !solid;
            }
        }

        // The mouse's position, read once a frame.
        float mx, my;
        SDL_GetMouseState(&mx, &my);

        // The closest point to the mouse on each shape, or in it, when the
        // shapes are solid. A segment has no inside, so it has one version.
        Vec2 mouse = { mx, my };
        Vec2 onSegment = closestOnSegment(mouse, segment.a, segment.b);
        Vec2 onCircle = solid ? closestInCircle(mouse, circle)
                              : closestOnCircle(mouse, circle);
        Vec2 onBox = solid ? closestInBox(mouse, box)
                           : closestOnBox(mouse, box);
        Vec2 onTriangle = solid ? closestInPolygon(mouse, triangle)
                                : closestOnPolygon(mouse, triangle);
        float t = fractionAlong(mouse, segment.a, segment.b);

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);
        drawGrid(renderer);

        // The shapes, with the segment's ends named.
        setColor(renderer, BLUE);
        SDL_RenderLine(renderer, segment.a.x, segment.a.y, segment.b.x,
                       segment.b.y);
        drawText(renderer, font, "A", segment.a.x - 20, segment.a.y - 10, BLUE);
        drawText(renderer, font, "B", segment.b.x + 10, segment.b.y - 10, BLUE);
        drawCircleOutline(renderer, circle.center, circle.radius, BLUE);
        drawPolygon(renderer, corners(box), BLUE);
        drawPolygon(renderer, triangle.points, BLUE);

        // The closest point on each, from the mouse.
        showClosest(renderer, font, mouse, onSegment);
        showClosest(renderer, font, mouse, onCircle);
        showClosest(renderer, font, mouse, onBox);
        showClosest(renderer, font, mouse, onTriangle);

        // The mouse, as a cross, named P.
        setColor(renderer, TEXT);
        SDL_RenderLine(renderer, mx - 7, my, mx + 7, my);
        SDL_RenderLine(renderer, mx, my - 7, mx, my + 7);
        drawText(renderer, font, "P", mx + 8, my + 2, TEXT);

        // The readout: the segment's fraction, before and after the clamp,
        // and what the other shapes are.
        drawText(renderer, font,
                 "segment: t = " + fmt2(t) + ", clamped to " +
                 fmt2(std::clamp(t, 0.0f, 1.0f)), 12, 10, TEXT);
        drawText(renderer, font,
                 solid ? "solid: inside a shape, P is its own closest point"
                       : "boundaries: inside a shape, P goes to an edge",
                 12, 30, SILVER);
        drawText(renderer, font,
                 "[Move the mouse] look   [S] solid or boundary   [Esc] quit",
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
