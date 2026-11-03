// Overlap Playground - the Chapter 15 demo (point and overlap tests) for
// Learning C++ by Building Games: Fundamentals
//
// Two circles and two boxes to drag. Each pair that overlaps lights up in
// amber, with the name of the test that found it between the two. Any
// shape the mouse is in turns white. A pentagon with a dent is there for
// the point tests: the readout says whether the crossing count and the
// convex test each find the mouse inside it, and they don't always agree.

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

// Amber with an alpha of 70 out of 255, so that it's mostly see-through.
constexpr SDL_Color HOT_FILL = { 232, 89, 12, 70 };

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

// An overlapping pair, for the readout: the point halfway between the two
// shapes' centers, and the name of the test that found it.
struct Overlap
{
    Vec2 at;
    std::string test;
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

// The closest point to P in a box, counting its inside: P with each
// coordinate clamped into the box's range. A point inside the box is its
// own closest point.
Vec2 closestInBox(Vec2 P, const AABB& box)
{
    return { std::clamp(P.x, box.min.x, box.max.x),
             std::clamp(P.y, box.min.y, box.max.y) };
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

// Draw a line of text centered on a point. TTF_GetStringSize measures it,
// in pixels, as it did for Magnet's distances.
void drawTextCentered(SDL_Renderer* renderer, TTF_Font* font,
                      const std::string& text, Vec2 at, SDL_Color color)
{
    int w = 0, h = 0;
    if (font) TTF_GetStringSize(font, text.c_str(), 0, &w, &h);
    drawText(renderer, font, text, at.x - w / 2.0f, at.y - h / 2.0f, color);
}

// Whether a point is in a circle: no farther from the center than the
// radius, by squared distances. On the rim counts as in.
bool pointInCircle(Vec2 P, const Circle& c)
{
    return (P - c.center).lengthSquared() <= c.radius * c.radius;
}

// Whether a point is in a box: inside its range on both axes. On an edge
// counts as in.
bool pointInBox(Vec2 P, const AABB& box)
{
    return P.x >= box.min.x && P.x <= box.max.x &&
           P.y >= box.min.y && P.y <= box.max.y;
}

// Whether a point is in any polygon, by counting the edges that a ray from
// it, going right, crosses: an odd count means in. An edge counts only
// when one of its ends is below the point and the other isn't, so a ray
// through a corner meets it once, and a flat edge never counts.
bool insidePolygon(Vec2 P, const Polygon& poly)
{
    bool inside = false;
    size_t n = poly.points.size();
    for (size_t i = 0; i < n; ++i)
    {
        Vec2 a = poly.points[i];
        Vec2 b = poly.points[(i + 1) % n];
        if ((a.y > P.y) != (b.y > P.y))
        {
            float x = lerp(a.x, b.x, inverseLerp(a.y, b.y, P.y));
            if (x > P.x) inside = !inside;
        }
    }
    return inside;
}

// Whether two circles overlap: their centers no farther apart than their
// radii added together, by squared distances. Touching counts.
bool circlesOverlap(const Circle& a, const Circle& b)
{
    float reach = a.radius + b.radius;
    return (b.center - a.center).lengthSquared() <= reach * reach;
}

// Whether two boxes overlap: their ranges overlap on both axes, so each
// box's min is no more than the other's max. Touching counts.
bool boxesOverlap(const AABB& a, const AABB& b)
{
    return a.min.x <= b.max.x && b.min.x <= a.max.x &&
           a.min.y <= b.max.y && b.min.y <= a.max.y;
}

// Whether a circle and a box overlap: the closest point in the box to the
// circle's center is within the radius. Chapter 13's closestInBox finds
// that point beside a side, past a corner, or with the center inside.
bool circleBoxOverlap(const Circle& c, const AABB& box)
{
    Vec2 closest = closestInBox(c.center, box);
    return (closest - c.center).lengthSquared() <= c.radius * c.radius;
}

// A box's center, halfway between its corners.
Vec2 centerOf(const AABB& box)
{
    return lerp(box.min, box.max, 0.5f);
}

// A box as SDL's rectangle: its top-left corner, its width and its height.
SDL_FRect rectOf(const AABB& box)
{
    return { box.min.x, box.min.y, box.max.x - box.min.x,
             box.max.y - box.min.y };
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
        SDL_CreateWindow("Overlap Playground", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // Blending, so that a color's alpha lets what's under it show
    // through, for the translucent fills.
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The shapes, in Chapter 11's structs: two circles, two boxes, and a
    // pentagon with a dent in its bottom, for the point tests.
    std::vector<Circle> circles = { { { 280, 250 }, 80.0f },
                                    { { 640, 470 }, 60.0f } };
    std::vector<AABB> boxes = { { { 400, 150 }, { 570, 270 } },
                                { { 270, 380 }, { 410, 560 } } };
    Polygon pentagon = { { { 700, 130 }, { 880, 130 }, { 880, 320 },
                           { 790, 230 }, { 700, 320 } } };

    // A grab holds pointers to the grabbed shape's points, which all move
    // with the mouse, and the mouse's position the frame before.
    std::vector<Vec2*> grabbed;
    Vec2 lastMouse = { 0.0f, 0.0f };

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
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                     event.button.button == SDL_BUTTON_LEFT)
            {
                // Grab the first shape the press is in: the pentagon, then
                // the boxes, then the circles, the reverse of the order
                // they're drawn in, so that the one on top wins.
                Vec2 press = { event.button.x, event.button.y };
                if (insidePolygon(press, pentagon))
                    for (Vec2& p : pentagon.points) grabbed.push_back(&p);
                for (AABB& box : boxes)
                    if (grabbed.empty() && pointInBox(press, box))
                        grabbed = { &box.min, &box.max };
                for (Circle& c : circles)
                    if (grabbed.empty() && pointInCircle(press, c))
                        grabbed = { &c.center };
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) grabbed.clear();
        }

        // The mouse's position, read once a frame.
        float mx, my;
        SDL_GetMouseState(&mx, &my);

        // A grabbed shape moves as far as the mouse has since last frame.
        Vec2 mouse = { mx, my };
        for (Vec2* p : grabbed) *p += mouse - lastMouse;
        lastMouse = mouse;

        // Test every pair that has a test: circles with circles, boxes
        // with boxes, and circles with boxes. Both shapes of a pair that
        // overlaps are hot, and the pair is kept for the readout.
        std::vector<bool> circleHot(circles.size(), false);
        std::vector<bool> boxHot(boxes.size(), false);
        std::vector<Overlap> overlaps;
        for (size_t i = 0; i < circles.size(); ++i)
        {
            for (size_t j = i + 1; j < circles.size(); ++j)
            {
                if (!circlesOverlap(circles[i], circles[j])) continue;
                circleHot[i] = circleHot[j] = true;
                Vec2 at = lerp(circles[i].center, circles[j].center, 0.5f);
                overlaps.push_back({ at, "circles" });
            }
        }
        for (size_t i = 0; i < boxes.size(); ++i)
        {
            for (size_t j = i + 1; j < boxes.size(); ++j)
            {
                if (!boxesOverlap(boxes[i], boxes[j])) continue;
                boxHot[i] = boxHot[j] = true;
                Vec2 at = lerp(centerOf(boxes[i]), centerOf(boxes[j]), 0.5f);
                overlaps.push_back({ at, "boxes" });
            }
        }
        for (size_t i = 0; i < circles.size(); ++i)
        {
            for (size_t j = 0; j < boxes.size(); ++j)
            {
                if (!circleBoxOverlap(circles[i], boxes[j])) continue;
                circleHot[i] = boxHot[j] = true;
                Vec2 at = lerp(circles[i].center, centerOf(boxes[j]), 0.5f);
                overlaps.push_back({ at, "circle and box" });
            }
        }

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);
        drawGrid(renderer);

        // The circles: a translucent amber fill and an amber outline when
        // they overlap something, and a blue outline when they don't. A
        // circle the mouse is in has a white outline.
        for (size_t i = 0; i < circles.size(); ++i)
        {
            const Circle& c = circles[i];
            if (circleHot[i])
            {
                setColor(renderer, HOT_FILL);
                fillCircle(renderer, c.center, c.radius);
            }
            SDL_Color edge = circleHot[i] ? AMBER : BLUE;
            if (pointInCircle(mouse, c)) edge = TEXT;
            drawCircleOutline(renderer, c.center, c.radius, edge);
        }

        // The boxes, the same way, with SDL's own rectangle calls.
        for (size_t i = 0; i < boxes.size(); ++i)
        {
            SDL_FRect rect = rectOf(boxes[i]);
            if (boxHot[i])
            {
                setColor(renderer, HOT_FILL);
                SDL_RenderFillRect(renderer, &rect);
            }
            SDL_Color edge = boxHot[i] ? AMBER : BLUE;
            if (pointInBox(mouse, boxes[i])) edge = TEXT;
            setColor(renderer, edge);
            SDL_RenderRect(renderer, &rect);
        }

        // The pentagon: white when the crossing count finds the mouse in
        // it, and silver when it doesn't.
        bool inByCount = insidePolygon(mouse, pentagon);
        bool inByConvex = insideConvex(mouse, pentagon);
        drawPolygon(renderer, pentagon.points, inByCount ? TEXT : SILVER);

        // Each overlapping pair's test, named halfway between the shapes.
        for (const Overlap& o : overlaps)
            drawTextCentered(renderer, font, o.test, o.at, TEXT);

        // The mouse, as a cross, named P.
        setColor(renderer, TEXT);
        SDL_RenderLine(renderer, mx - 7, my, mx + 7, my);
        SDL_RenderLine(renderer, mx, my - 7, mx, my + 7);
        drawText(renderer, font, "P", mx + 8, my + 2, TEXT);

        // The readout: how many pairs overlap, and what each point test
        // says about the mouse and the pentagon.
        drawText(renderer, font,
                 "overlapping pairs: " + std::to_string(overlaps.size()),
                 12, 10, TEXT);
        drawText(renderer, font,
                 std::string("pentagon: the crossing count says ") +
                 (inByCount ? "in" : "out") + ", the convex test says " +
                 (inByConvex ? "in" : "out"), 12, 30, SILVER);
        drawText(renderer, font, "[Drag] a shape   [Esc] quit",
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
