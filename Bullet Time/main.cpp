// Bullet Time - the Chapter 17 demo (rays and swept collision) for
// Learning C++ by Building Games: Fundamentals
//
// A fast ball bounces around an arena with a thin wall across its middle,
// moving in fixed steps of a sixtieth of a second. Tested naively, only at
// the end of each step, it often jumps clean past the wall: it tunnels.
// Swept, its path is cast against the wall grown by the ball's radius, so
// every hit is caught at the moment of first contact. Space switches
// between the two, Up and Down change the speed, and P pauses. The trail
// marks where the ball was at the end of each step.

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
constexpr SDL_Color GREEN = { 47, 158, 68, 255 };
constexpr SDL_Color RED = { 224, 49, 49, 255 };
constexpr SDL_Color FAINT = { 70, 78, 90, 255 };
constexpr SDL_Color SILVER = { 150, 158, 170, 255 };

// The simulation's fixed step, a sixtieth of a second, and the ball's
// radius.
constexpr float STEP = 1.0f / 60.0f;
constexpr float RADIUS = 7.0f;

// An axis-aligned box: its smallest and largest corners. Neither of min's
// coordinates is ever more than max's.
struct AABB
{
    Vec2 min;
    Vec2 max;
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

// A box as SDL's rectangle: its top-left corner, its width and its height.
SDL_FRect rectOf(const AABB& box)
{
    return { box.min.x, box.min.y, box.max.x - box.min.x,
             box.max.y - box.min.y };
}

// Where a ray, from O along the unit vector d, first meets a circle: the
// nearer root of |O + t d - C|^2 = r^2. A ray that starts inside the
// circle, or on it, meets it at once, at 0. It returns the distance along
// the ray, or -1 for a miss.
float rayCircle(Vec2 O, Vec2 d, Vec2 C, float r)
{
    Vec2 m = O - C;
    float b = m.dot(d);
    float c = m.lengthSquared() - r * r;
    if (c <= 0.0f) return 0.0f;
    if (b > 0.0f) return -1.0f;
    float disc = b * b - c;
    if (disc < 0.0f) return -1.0f;
    return -b - std::sqrt(disc);
}

// Where a ray first meets a segment grown by a radius, a capsule: two sides
// parallel to the segment, r out on each side, and a circle around each
// end. It's the nearest hit on any of the four that the ray is heading
// into, not out of, and it fills in the normal there: straight out from a
// side, or out from an end's center. It returns -1 for a miss.
float rayCapsule(Vec2 O, Vec2 d, Vec2 A, Vec2 B, float r, Vec2& normal)
{
    Vec2 n = (B - A).perp().normalized();
    float best = -1.0f;

    // The two sides: the segment moved r out along its normal, each way.
    for (float side : { 1.0f, -1.0f })
    {
        Vec2 out = n * side;
        float t, u;
        if (d.dot(out) < 0.0f &&
            raySegment(O, d, A + out * r, B + out * r, t, u) &&
            (best < 0.0f || t < best))
        {
            best = t;
            normal = out;
        }
    }

    // The two ends: circles of radius r around A and around B.
    for (Vec2 end : { A, B })
    {
        float t = rayCircle(O, d, end, r);
        if (t < 0.0f) continue;
        Vec2 out = (O + d * t - end).normalized();
        if (d.dot(out) < 0.0f && (best < 0.0f || t < best))
        {
            best = t;
            normal = out;
        }
    }
    return best;
}

// One naive step: move the whole way, then test where the ball is now. If
// it overlaps the wall, it bounces back toward the side it started the
// step on: the distance it went past the point of contact is folded back
// out, as a bounce would have done, and its velocity is reflected.
// Returns whether it bounced.
bool naiveStep(Vec2& pos, Vec2& vel, Vec2 A, Vec2 B, Vec2 normal)
{
    Vec2 from = pos;
    pos += vel * STEP;
    Vec2 closest = closestOnSegment(pos, A, B);
    if ((pos - closest).lengthSquared() > RADIUS * RADIUS) return false;
    Vec2 back = ((from - A).dot(normal) >= 0.0f) ? normal : -normal;
    if (vel.dot(back) >= 0.0f) return false;
    float depth = RADIUS - (pos - closest).dot(back);
    pos += back * (2.0f * depth);
    vel = reflect(vel, back);
    return true;
}

// One swept step: cast the ball's center along its move against the wall
// grown by the radius. A hit is the moment of first contact, so the ball
// moves there and bounces, and spends the rest of its move in its new
// direction, which may meet the wall again. Returns whether it bounced.
bool sweptStep(Vec2& pos, Vec2& vel, Vec2 A, Vec2 B)
{
    bool bounced = false;
    float remaining = vel.length() * STEP;
    for (int i = 0; i < 4 && remaining > 0.0f; ++i)
    {
        Vec2 dir = vel.normalized();
        Vec2 normal;
        float t = rayCapsule(pos, dir, A, B, RADIUS, normal);
        if (t < 0.0f || t > remaining)
        {
            pos += dir * remaining;
            return bounced;
        }
        pos += dir * t;
        vel = reflect(vel, normal);
        remaining -= t;
        bounced = true;
    }
    return bounced;
}

// Keep the ball inside the arena, bouncing it off the sides. Any distance
// it went past a side is folded back, so that it travels its whole step.
// The sides can't be tunneled through, since this tests which side of
// each the ball is on, however far past it the ball has gone.
void keepInArena(Vec2& pos, Vec2& vel, const AABB& arena)
{
    float left = arena.min.x + RADIUS;
    float right = arena.max.x - RADIUS;
    float top = arena.min.y + RADIUS;
    float bottom = arena.max.y - RADIUS;
    if (pos.x < left)
    {
        pos.x = 2.0f * left - pos.x;
        vel.x = std::fabs(vel.x);
    }
    if (pos.x > right)
    {
        pos.x = 2.0f * right - pos.x;
        vel.x = -std::fabs(vel.x);
    }
    if (pos.y < top)
    {
        pos.y = 2.0f * top - pos.y;
        vel.y = std::fabs(vel.y);
    }
    if (pos.y > bottom)
    {
        pos.y = 2.0f * bottom - pos.y;
        vel.y = -std::fabs(vel.y);
    }
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
        SDL_CreateWindow("Bullet Time", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The arena, the thin wall across its middle, and the wall's unit
    // normal, which faces left.
    const AABB arena = { { 60, 90 }, { 940, 640 } };
    const Vec2 wallA = { 500, 240 };
    const Vec2 wallB = { 500, 500 };
    const Vec2 wallNormal = (wallB - wallA).perp().normalized();

    // The ball: where it is, its speed, and its velocity.
    Vec2 pos = { 150, 300 };
    float speed = 2000.0f;
    Vec2 vel = Vec2::fromAngle(0.16f) * speed;

    // The test in use, the pause, the time not yet stepped, the trail of
    // the ball's places, and the count of its approaches to the wall.
    bool swept = false;
    bool paused = false;
    float accumulator = 0.0f;
    std::vector<Vec2> trail;
    int caught = 0;
    int missed = 0;

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
                else if (event.key.key == SDLK_P) paused = !paused;
                else if (event.key.key == SDLK_SPACE ||
                         event.key.key == SDLK_UP ||
                         event.key.key == SDLK_DOWN)
                {
                    // A new test or a new speed starts the counts afresh.
                    // The speed stays from 300 to 5,000 pixels a second.
                    if (event.key.key == SDLK_SPACE) swept = !swept;
                    if (event.key.key == SDLK_UP) speed *= 1.15f;
                    if (event.key.key == SDLK_DOWN) speed *= 0.87f;
                    speed = std::clamp(speed, 300.0f, 5000.0f);
                    vel = vel.normalized() * speed;
                    caught = missed = 0;
                }
            }
        }

        // The fixed step: the frame's time goes into the accumulator, and
        // the ball takes as many whole steps of STEP as it holds.
        if (!paused) accumulator += dt;
        while (!paused && accumulator >= STEP)
        {
            accumulator -= STEP;
            Vec2 from = pos;
            bool bounced =
                swept ? sweptStep(pos, vel, wallA, wallB)
                      : naiveStep(pos, vel, wallA, wallB, wallNormal);

            // An approach the test missed: the center's path went straight
            // through the wall, with no bounce.
            Vec2 move = pos - from;
            float t, u;
            if (bounced) ++caught;
            else if (move.lengthSquared() > 0.0f &&
                     raySegment(from, move.normalized(), wallA, wallB, t, u) &&
                     t <= move.length())
                ++missed;

            keepInArena(pos, vel, arena);

            // The trail: the ball's place at the end of each step.
            trail.push_back(pos);
            if (trail.size() > 30) trail.erase(trail.begin());
        }

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);
        drawGrid(renderer);

        // The arena, and the thin wall, two pixels wide.
        setColor(renderer, SILVER);
        SDL_FRect box = rectOf(arena);
        SDL_RenderRect(renderer, &box);
        SDL_RenderLine(renderer, wallA.x, wallA.y, wallB.x, wallB.y);
        SDL_RenderLine(renderer, wallA.x + 1, wallA.y, wallB.x + 1, wallB.y);

        // When swept, the wall grown by the radius, faintly: the outline
        // the ball's center can't cross.
        if (swept)
        {
            Vec2 side = wallNormal * RADIUS;
            setColor(renderer, FAINT);
            SDL_RenderLine(renderer, wallA.x + side.x, wallA.y + side.y,
                           wallB.x + side.x, wallB.y + side.y);
            SDL_RenderLine(renderer, wallA.x - side.x, wallA.y - side.y,
                           wallB.x - side.x, wallB.y - side.y);
            drawCircleOutline(renderer, wallA, RADIUS, FAINT);
            drawCircleOutline(renderer, wallB, RADIUS, FAINT);
        }

        // The trail: a line through the ball's places, with a dot at each.
        setColor(renderer, GREY);
        for (size_t i = 1; i < trail.size(); ++i)
        {
            SDL_RenderLine(renderer, trail[i - 1].x, trail[i - 1].y,
                           trail[i].x, trail[i].y);
        }
        for (const Vec2& p : trail) fillCircle(renderer, p, 2.0f);

        // The ball: green when swept, and red when naive.
        setColor(renderer, swept ? GREEN : RED);
        fillCircle(renderer, pos, RADIUS);

        // The readout: the test, how many approaches it caught, the
        // speed, and how far a step moves the ball.
        int approaches = caught + missed;
        int share = (approaches > 0) ? 100 * caught / approaches : 100;
        drawText(renderer, font,
                 std::string(swept ? "swept" : "naive") + " test: caught " +
                 std::to_string(caught) + " of " + std::to_string(approaches) +
                 " approaches (" + std::to_string(share) + "%)",
                 12, 10, swept ? GREEN : RED);
        drawText(renderer, font,
                 "speed " + std::to_string(std::lround(speed)) + " px/s: " +
                 std::to_string(std::lround(speed * STEP)) + " px a step" +
                 (paused ? "   (paused)" : ""),
                 12, 30, SILVER);
        drawText(renderer, font,
                 "[Space] naive or swept   [Up] [Down] speed   [P] pause   "
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
