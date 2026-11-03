// Critters - the Chapter 9 demo (steering behaviors) for
// Learning C++ by Building Games: Fundamentals
//
// Fifteen agents steer around the mouse, three to each behavior. Seekers
// chase it at full speed, and arrivers slow down to stop on it. Pursuers
// aim at where it's going. Wanderers drift about, and grazers wander too,
// but flee when the mouse comes close. Each leaves a trail of dots. R
// switches how quickly the agents respond: in 0.15 seconds, or in one.

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>
#include "Vec2.h"

constexpr int WINDOW_WIDTH  = 1000;
constexpr int WINDOW_HEIGHT = 700;

constexpr SDL_Color TEXT = { 235, 238, 242, 255 };
constexpr SDL_Color GREY = { 90, 100, 112, 255 };
constexpr SDL_Color BLUE = { 47, 111, 235, 255 };
constexpr SDL_Color GREEN = { 47, 158, 68, 255 };
constexpr SDL_Color RED = { 224, 49, 49, 255 };
constexpr SDL_Color VIOLET = { 150, 102, 230, 255 };
constexpr SDL_Color FAINT = { 70, 78, 90, 255 };

// How the agents move: their top speed, and the most that their steering
// can change their velocity in a second.
constexpr float MAX_SPEED = 220.0f;      // pixels a second
constexpr float MAX_FORCE = 380.0f;      // pixels a second, every second

// Arrivers slow down inside SLOW_RADIUS of the mouse, and grazers flee
// inside PANIC_RADIUS.
constexpr float SLOW_RADIUS = 150.0f;
constexpr float PANIC_RADIUS = 220.0f;

// The five behaviors, and each one's name and color, in the same order.
enum Behavior { SEEK, ARRIVE, PURSUE, WANDER, GRAZE };

struct Kind
{
    const char* name;
    SDL_Color color;
};

constexpr Kind KINDS[] = {
    { "seek", BLUE },
    { "arrive", GREEN },
    { "pursue", VIOLET },
    { "wander", TEXT },
    { "graze", RED },
};

// An agent: where it is, how fast it's going, what it does, the angle it
// wanders toward, the way it faces, and where it's been, oldest first.
struct Agent
{
    Vec2 pos;
    Vec2 vel;
    Behavior behavior;
    float wanderAngle;
    float heading;
    std::vector<Vec2> trail;
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

// A random float from lo to hi. The generator is made once, and seeded
// from the system, so that every run is different.
std::mt19937 rng{ std::random_device{}() };

float randRange(float lo, float hi)
{
    std::uniform_real_distribution<float> spread(lo, hi);
    return spread(rng);
}

// Seek: full speed, straight at the target.
Vec2 seek(Vec2 pos, Vec2 target)
{
    return (target - pos).normalized() * MAX_SPEED;
}

// Arrive: seek, but inside SLOW_RADIUS, a speed in step with the distance
// left, down to zero on the target.
Vec2 arrive(Vec2 pos, Vec2 target)
{
    Vec2 toTarget = target - pos;
    float d = toTarget.length();
    float speed = (d < SLOW_RADIUS) ? MAX_SPEED * d / SLOW_RADIUS : MAX_SPEED;
    return toTarget.normalized() * speed;
}

// Flee: full speed, straight away from the threat, inside PANIC_RADIUS.
// Farther off, it wants the velocity the agent already has, which asks
// for no steering at all.
Vec2 flee(const Agent& a, Vec2 threat)
{
    Vec2 away = a.pos - threat;
    if (away.length() > PANIC_RADIUS) return a.vel;
    return away.normalized() * MAX_SPEED;
}

// Pursue: seek where the target will be, if it keeps its velocity, by the
// time the agent could get there, but never more than a second ahead.
Vec2 pursue(Vec2 pos, Vec2 target, Vec2 targetVel)
{
    float ahead = std::min(distance(pos, target) / MAX_SPEED, 1.0f);
    return seek(pos, target + targetVel * ahead);
}

// Wander: drift the agent's own angle by a small random step, and head
// that way at full speed. The step grows with the square root of dt, so
// the drift over a second is the same at any frame rate.
Vec2 wander(Agent& a, float dt)
{
    a.wanderAngle += randRange(-1.0f, 1.0f) * 0.65f * std::sqrt(dt);
    return Vec2::fromAngle(a.wanderAngle) * MAX_SPEED;
}

// One agent's frame. Its behavior gives the velocity it wants, and its
// steering is the change that would get its velocity there in
// responseTime seconds. A grazer adds up two behaviors' steering, with
// fleeing weighted three times as much as wandering.
void update(Agent& a, Vec2 target, Vec2 targetVel, float responseTime, float dt)
{
    Vec2 steer;
    if (a.behavior == SEEK)
        steer = (seek(a.pos, target) - a.vel) / responseTime;
    else if (a.behavior == ARRIVE)
        steer = (arrive(a.pos, target) - a.vel) / responseTime;
    else if (a.behavior == PURSUE)
        steer = (pursue(a.pos, target, targetVel) - a.vel) / responseTime;
    else if (a.behavior == WANDER)
        steer = (wander(a, dt) - a.vel) / responseTime;
    else
        steer = (wander(a, dt) - a.vel) / responseTime +
                (flee(a, target) - a.vel) / responseTime * 3.0f;

    // The steering is capped at MAX_FORCE, and the speed at MAX_SPEED.
    // Then the agent moves by semi-implicit Euler, as Thrust did.
    a.vel += steer.truncated(MAX_FORCE) * dt;
    a.vel = a.vel.truncated(MAX_SPEED);
    a.pos += a.vel * dt;

    // Off one edge, back on at the opposite one.
    if (a.pos.x < 0) a.pos.x += WINDOW_WIDTH;
    if (a.pos.x > WINDOW_WIDTH) a.pos.x -= WINDOW_WIDTH;
    if (a.pos.y < 0) a.pos.y += WINDOW_HEIGHT;
    if (a.pos.y > WINDOW_HEIGHT) a.pos.y -= WINDOW_HEIGHT;

    // It faces the way it's moving. Stopped, it keeps the way it faced.
    if (a.vel.length() > 1.0f) a.heading = a.vel.angle();
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
        SDL_CreateWindow("Critters", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // Three agents of each behavior, still, at random places, each with a
    // random angle to face and to wander toward, and no trail yet.
    std::vector<Agent> agents;
    for (Behavior b : { SEEK, ARRIVE, PURSUE, WANDER, GRAZE })
    {
        for (int i = 0; i < 3; ++i)
        {
            Vec2 pos = { randRange(100.0f, 900.0f),
                         randRange(100.0f, 600.0f) };
            float angle = randRange(-PI, PI);
            agents.push_back({ pos, Vec2{}, b, angle, angle, {} });
        }
    }

    // An agent's shape: a triangle around its center, facing along x.
    const std::vector<Vec2> body = { { 12, 0 }, { -9, -7 }, { -9, 7 } };

    // R switches the agents between a quick response and a slow one.
    bool slow = false;

    // The mouse's velocity, for the pursuers, and where it was the frame
    // before, to measure it.
    Vec2 mouseVel;
    Vec2 lastMouse = { WINDOW_WIDTH / 2.0f, WINDOW_HEIGHT / 2.0f };

    // The time since the agents last added to their trails.
    float trailTimer = 0.0f;

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
                else if (event.key.key == SDLK_R) slow = !slow;
            }
        }

        // The mouse's position, read once a frame.
        float mx, my;
        SDL_GetMouseState(&mx, &my);

        // The mouse's velocity: how far it moved since the last frame, over
        // dt, smoothed as Chapter 8 followed a target, so that one jerky
        // frame doesn't throw the pursuers off.
        Vec2 mouse = { mx, my };
        if (dt > 0.0f)
        {
            Vec2 measured = (mouse - lastMouse) / dt;
            float share = 1.0f - std::pow(0.5f, dt / 0.1f);
            mouseVel = lerp(mouseVel, measured, share);
        }
        lastMouse = mouse;

        float responseTime = slow ? 1.0f : 0.15f;
        for (Agent& a : agents) update(a, mouse, mouseVel, responseTime, dt);

        // Every 20th of a second, each agent adds where it is to its trail,
        // which keeps the last 30 places: a second and a half of its path.
        trailTimer += dt;
        if (trailTimer >= 0.05f)
        {
            trailTimer -= 0.05f;
            for (Agent& a : agents)
            {
                a.trail.push_back(a.pos);
                if (a.trail.size() > 30) a.trail.erase(a.trail.begin());
            }
        }

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);
        drawGrid(renderer);

        // The mouse: a cross, with the arrivers' slowing circle and the
        // grazers' panic circle around it, and an arrow to where it will be
        // in half a second, at the velocity the pursuers use.
        setColor(renderer, TEXT);
        SDL_RenderLine(renderer, mx - 8, my, mx + 8, my);
        SDL_RenderLine(renderer, mx, my - 8, mx, my + 8);
        drawCircleOutline(renderer, mouse, SLOW_RADIUS, FAINT);
        drawCircleOutline(renderer, mouse, PANIC_RADIUS, FAINT);
        drawArrow(renderer, mouse, mouse + mouseVel * 0.5f, VIOLET);

        // Each agent: its trail, as dots, and its body, turned to its
        // heading and moved to its position, as Chapter 6 drew its shape.
        for (const Agent& a : agents)
        {
            SDL_Color color = KINDS[a.behavior].color;
            setColor(renderer, color);
            for (const Vec2& p : a.trail) fillCircle(renderer, p, 1.5f);
            std::vector<Vec2> shape;
            for (const Vec2& p : body)
                shape.push_back(a.pos + p.rotated(a.heading));
            drawPolygon(renderer, shape, color);
        }

        // The key: each behavior's color and name.
        float y = 10.0f;
        for (const Kind& k : KINDS)
        {
            setColor(renderer, k.color);
            fillCircle(renderer, { 20.0f, y + 10.0f }, 6.0f);
            drawText(renderer, font, k.name, 34, y, k.color);
            y += 22.0f;
        }

        drawText(renderer, font, "response time: " + fmt2(responseTime) + " s",
                 12, 126, TEXT);
        drawText(renderer, font,
                 "[Move the mouse] lead them   [R] slow or quick   [Esc] quit",
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
