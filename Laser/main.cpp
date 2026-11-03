// Laser - the Chapter 12 demo (lines, rays and distances) for
// Learning C++ by Building Games: Fundamentals
//
// A laser fires from a fixed origin toward the mouse, and stops at the
// first wall it hits: a ray cast, made of ray-against-segment tests. The
// wall it hits lights up, drawn as an arrow from its first end to its
// second, with its line carried on past its ends, and the foot of the
// perpendicular from the origin to that line. The readout gives how far
// the beam went, where along the wall it hit, and which side of the
// wall's line the origin is on, and how far from it.

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
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
constexpr SDL_Color FAINT = { 70, 78, 90, 255 };
constexpr SDL_Color SILVER = { 150, 158, 170, 255 };

// A segment: its two ends.
struct Segment
{
    Vec2 a;
    Vec2 b;
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

// Where a ray, from O along the unit vector d, crosses the segment from A
// to B. It's true for a hit, and then t is the distance along the ray,
// and u the fraction of the way from A to B. Lines that are parallel, to
// within a tiny angle, never cross at a single point, so they miss.
bool raySegment(Vec2 O, Vec2 d, Vec2 A, Vec2 B, float& t, float& u)
{
    Vec2 s = B - A;
    float denom = d.cross(s);
    if (std::fabs(denom) < 0.00001f * s.length()) return false;
    Vec2 toA = A - O;
    t = toA.cross(s) / denom;
    u = toA.cross(d) / denom;
    return t >= 0.0f && u >= 0.0f && u <= 1.0f;
}

// How far a point is from the infinite line through A and B: the dot
// product with the line's unit normal, A to B turned a quarter turn. It's
// positive on the right of A to B, as the screen shows it, and negative on
// the left.
float signedDistance(Vec2 P, Vec2 A, Vec2 B)
{
    Vec2 normal = (B - A).perp().normalized();
    return normal.dot(P - A);
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
        SDL_CreateWindow("Laser", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The laser's origin, and the way it's aimed, as a unit vector.
    const Vec2 origin = { 140.0f, 350.0f };
    Vec2 dir = { 1.0f, 0.0f };

    // The walls. The second and the third meet at a corner.
    const std::vector<Segment> walls = {
        { { 400, 200 }, { 400, 60 } },
        { { 540, 250 }, { 760, 250 } },
        { { 760, 250 }, { 760, 420 } },
        { { 620, 500 }, { 880, 620 } },
        { { 300, 560 }, { 520, 610 } },
        { { 720, 110 }, { 900, 300 } },
    };

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
        }

        // The mouse's position, read once a frame.
        float mx, my;
        SDL_GetMouseState(&mx, &my);

        // Aim at the mouse, unless it's on the origin, where there's no
        // direction to aim in, and the last aim stays.
        Vec2 mouse = { mx, my };
        if ((mouse - origin).lengthSquared() > 1.0f)
            dir = (mouse - origin).normalized();

        // The ray cast: test every wall, and keep the nearest hit. Hits
        // closer than a hundredth of a pixel don't count, so that a beam
        // can start on a wall without hitting it.
        float nearest = 1e9f;
        float hitU = 0.0f;
        int hitWall = -1;
        for (int i = 0; i < (int)walls.size(); ++i)
        {
            float t = 0.0f, u = 0.0f;
            if (raySegment(origin, dir, walls[i].a, walls[i].b, t, u) &&
                t > 0.01f && t < nearest)
            {
                nearest = t;
                hitU = u;
                hitWall = i;
            }
        }
        Vec2 beamEnd = origin + dir * (hitWall >= 0 ? nearest : 2000.0f);

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);
        drawGrid(renderer);

        // The walls that weren't hit.
        setColor(renderer, SILVER);
        for (int i = 0; i < (int)walls.size(); ++i)
        {
            if (i == hitWall) continue;
            SDL_RenderLine(renderer, walls[i].a.x, walls[i].a.y,
                           walls[i].b.x, walls[i].b.y);
        }

        // The hit wall: its line carried on past its ends, faintly, then
        // the wall as an arrow from a to b, and the perpendicular from the
        // origin to the line, with its foot.
        float side = 0.0f;
        if (hitWall >= 0)
        {
            const Segment& w = walls[hitWall];
            Vec2 along = (w.b - w.a).normalized();
            drawDashed(renderer, w.a - along * 1500.0f, w.b + along * 1500.0f,
                       FAINT);
            drawArrow(renderer, w.a, w.b, GREEN);
            side = signedDistance(origin, w.a, w.b);
            Vec2 foot = origin - along.perp() * side;
            drawDashed(renderer, origin, foot, AMBER);
            setColor(renderer, AMBER);
            fillCircle(renderer, foot, 4.0f);
        }

        // The beam, the origin, and where the beam hit.
        setColor(renderer, BLUE);
        SDL_RenderLine(renderer, origin.x, origin.y, beamEnd.x, beamEnd.y);
        fillCircle(renderer, origin, 6.0f);
        if (hitWall >= 0)
        {
            setColor(renderer, GREEN);
            fillCircle(renderer, beamEnd, 6.0f);
        }

        // The readout: where the beam hit, and the origin's side of that
        // wall's line, and its distance from it.
        if (hitWall >= 0)
        {
            drawText(renderer, font,
                     "wall " + std::to_string(hitWall) + " hit: t = " +
                     std::to_string(std::lround(nearest)) + " px, u = " +
                     fmt2(hitU), 12, 10, TEXT);
            drawText(renderer, font,
                     "the origin is " +
                     std::to_string(std::lround(std::fabs(side))) +
                     " px from the wall's line, on its " +
                     (side > 0.0f ? "right" : "left"), 12, 30, SILVER);
        }
        else
        {
            drawText(renderer, font, "no hit: the beam leaves the window",
                     12, 10, TEXT);
        }
        drawText(renderer, font, "[Move the mouse] aim   [Esc] quit",
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
