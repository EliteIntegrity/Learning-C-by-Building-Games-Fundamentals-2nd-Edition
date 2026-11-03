// Impulse - the Chapter 20 demo (a small physics engine) for
// Learning C++ by Building Games: Fundamentals
//
// Seventy balls with mass fall into a box and settle into a pile. Gravity and
// a spring to the mouse are forces, added up each step, and collisions are
// answered with impulses shared by mass. Drag a ball to throw it. Space
// switches between fixed steps and one variable step a frame, L makes one
// frame take a fifth of a second, R switches between 120 and 30 steps a
// second, I turns interpolation on and off, and Enter drops a new pile.

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
constexpr SDL_Color AMBER = { 232, 89, 12, 255 };
constexpr SDL_Color GREEN = { 47, 158, 68, 255 };
constexpr SDL_Color RED = { 224, 49, 49, 255 };
constexpr SDL_Color VIOLET = { 150, 102, 230, 255 };
constexpr SDL_Color SILVER = { 150, 158, 170, 255 };
constexpr SDL_Color BALL_COLORS[] = { BLUE, GREEN, RED, AMBER, VIOLET };

// Gravity, as in Chapter 10, in pixels a second, every second, and how
// bouncy a collision is.
constexpr float GRAVITY = 900.0f;
constexpr float RESTITUTION = 0.35f;

// The passes over every contact that each step makes.
constexpr int PASSES = 4;

// The mouse's spring: its pull for each pixel of stretch, and its damping
// for each pixel a second of speed.
constexpr float SPRING = 40000.0f;
constexpr float DAMPING = 3000.0f;

// How many balls make the pile.
constexpr int BALL_COUNT = 70;

// An axis-aligned box: its smallest and largest corners. Neither of min's
// coordinates is ever more than max's.
struct AABB
{
    Vec2 min;
    Vec2 max;
};

// A flat surface: a point on it, and its normal, a unit vector that points
// out of it, to the side the balls are on.
struct Surface
{
    Vec2 point;
    Vec2 normal;
};

// The box the balls are in.
const AABB BOX = { { 60, 90 }, { 940, 640 } };

// A body: a ball with mass. The force on it is added up each step, and its
// inverse mass is stored, since that's what the response divides by. Its
// place before the last step is kept, for drawing between steps.
struct Body
{
    Vec2 pos;
    Vec2 vel;
    Vec2 force;
    float radius;
    float mass;
    float invMass;
    Vec2 prevPos;
    SDL_Color color;
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

// A box as SDL's rectangle: its top-left corner, its width and its height.
SDL_FRect rectOf(const AABB& box)
{
    return { box.min.x, box.min.y, box.max.x - box.min.x,
             box.max.y - box.min.y };
}

// A ball of a radius, whose mass is its radius squared, as if every ball
// were a disc cut from the same sheet, so that mass grows with area.
Body makeBody(Vec2 pos, float radius, SDL_Color color)
{
    float mass = radius * radius;
    return { pos, { 0, 0 }, { 0, 0 }, radius, mass, 1.0f / mass, pos, color };
}

// A new pile, dropped from the top of the box: balls of random sizes, at
// random places that don't overlap.
std::vector<Body> dropBalls()
{
    std::vector<Body> bodies;
    while ((int)bodies.size() < BALL_COUNT)
    {
        float r = randRange(12.0f, 28.0f);
        Vec2 p = { randRange(BOX.min.x + r, BOX.max.x - r),
                   randRange(BOX.min.y + r, 380.0f) };
        bool clear = true;
        for (const Body& b : bodies)
            if (distance(p, b.pos) < r + b.radius + 2.0f) clear = false;
        if (clear)
        {
            SDL_Color color = BALL_COLORS[bodies.size() % 5];
            bodies.push_back(makeBody(p, r, color));
        }
    }
    return bodies;
}

// The forces for one step, added up: gravity on every ball, its mass times
// g, and on the ball being held, a spring toward the mouse. The spring
// pulls in step with how far it's stretched, and its damping pushes back
// against the ball's speed, so that it settles rather than swings forever.
void addForces(std::vector<Body>& bodies, int held, Vec2 mouse)
{
    for (Body& b : bodies) b.force += Vec2{ 0.0f, GRAVITY } * b.mass;
    if (held >= 0)
    {
        Body& b = bodies[held];
        b.force += (mouse - b.pos) * SPRING - b.vel * DAMPING;
    }
}

// One step of semi-implicit Euler, as in Chapter 7: the force changes the
// velocity, by the force over the mass, and then the new velocity moves the
// body. The force is used up, ready for the next step's.
void integrate(std::vector<Body>& bodies, float h)
{
    for (Body& b : bodies)
    {
        b.vel += b.force * (b.invMass * h);
        b.prevPos = b.pos;
        b.pos += b.vel * h;
        b.force = { 0, 0 };
    }
}

// The response to one contact between a and b, where n is the unit normal
// from a toward b, and depth is their overlap. The push moves each out in
// step with its inverse mass, so the heavier moves less. The impulse, only
// if they're closing, changes their velocities by the same amount of
// momentum, in opposite directions.
void respond(Body& a, Body& b, Vec2 n, float depth)
{
    float invSum = a.invMass + b.invMass;
    if (invSum <= 0.0f) return;
    float push = depth / invSum;
    a.pos -= n * (push * a.invMass);
    b.pos += n * (push * b.invMass);

    float closing = (b.vel - a.vel).dot(n);
    if (closing >= 0.0f) return;
    float j = -(1.0f + RESTITUTION) * closing / invSum;
    a.vel -= n * (j * a.invMass);
    b.vel += n * (j * b.invMass);
}

// Every contact, in passes: each pair of balls, then each ball against
// each wall. A wall is a body with no velocity and an inverse mass of 0,
// infinitely heavy, so the same response bounces a ball off it.
void resolveContacts(std::vector<Body>& bodies, const Surface walls[4])
{
    Body wall = {};
    for (int pass = 0; pass < PASSES; ++pass)
    {
        for (size_t i = 0; i < bodies.size(); ++i)
            for (size_t j = i + 1; j < bodies.size(); ++j)
            {
                Body& a = bodies[i];
                Body& b = bodies[j];
                Vec2 d = b.pos - a.pos;
                float dist = d.length();
                float reach = a.radius + b.radius;
                if (dist <= reach && dist > 0.0001f)
                    respond(a, b, d / dist, reach - dist);
            }
        for (Body& b : bodies)
            for (int w = 0; w < 4; ++w)
            {
                Vec2 off = b.pos - walls[w].point;
                float depth = b.radius - off.dot(walls[w].normal);
                if (depth >= 0.0f) respond(b, wall, -walls[w].normal, depth);
            }
    }
}

// The deepest overlap left between any two balls, or a ball and a wall.
float deepestOverlap(const std::vector<Body>& bodies, const Surface walls[4])
{
    float deepest = 0.0f;
    for (size_t i = 0; i < bodies.size(); ++i)
    {
        for (size_t j = i + 1; j < bodies.size(); ++j)
        {
            float reach = bodies[i].radius + bodies[j].radius;
            float gap = distance(bodies[i].pos, bodies[j].pos);
            deepest = std::max(deepest, reach - gap);
        }
        for (int w = 0; w < 4; ++w)
        {
            Vec2 off = bodies[i].pos - walls[w].point;
            float depth = bodies[i].radius - off.dot(walls[w].normal);
            deepest = std::max(deepest, depth);
        }
    }
    return deepest;
}

// One whole step of the physics, h seconds long.
void step(std::vector<Body>& bodies, const Surface walls[4], int held,
          Vec2 mouse, float h)
{
    addForces(bodies, held, mouse);
    integrate(bodies, h);
    resolveContacts(bodies, walls);
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
        SDL_CreateWindow("Impulse", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The box's four walls, each a surface whose normal points in. Then the
    // pile, the ball being held, or -1, and the settings: fixed steps or
    // one a frame, how long a fixed step is, and whether to interpolate.
    const Surface walls[4] = { { BOX.min, { 1, 0 } }, { BOX.min, { 0, 1 } },
                               { BOX.max, { -1, 0 } }, { BOX.max, { 0, -1 } } };
    std::vector<Body> bodies = dropBalls();
    int held = -1;
    bool fixedStep = true;
    float stepTime = 1.0f / 120.0f;
    bool interpolate = true;

    // The time not yet stepped, a lag spike waiting for the next frame, and
    // the frames and steps in the last second, for the readout.
    float accumulator = 0.0f;
    bool spike = false;
    float secondSum = 0.0f;
    int frames = 0;
    int steps = 0;
    int shownFrames = 0;
    int shownSteps = 0;

    // Delta time: the seconds since the last frame, from SDL's counter.
    Uint64 previous = SDL_GetPerformanceCounter();
    const double frequency = (double)SDL_GetPerformanceFrequency();

    bool running = true;
    while (running)
    {
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)((now - previous) / frequency);
        previous = now;
        float frameSeconds = dt;   // Before the clamp, for the readout.
        if (dt > 0.05f) dt = 0.05f;   // A long stall counts as 1/20 s.

        // Events: quitting, and the keys and clicks that happen once.
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT) running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN)
            {
                if (event.key.key == SDLK_ESCAPE) running = false;
                else if (event.key.key == SDLK_L) spike = true;
                else if (event.key.key == SDLK_I) interpolate = !interpolate;
                else if (event.key.key == SDLK_SPACE ||
                         event.key.key == SDLK_R)
                {
                    // A new kind of step starts with no time saved up.
                    if (event.key.key == SDLK_SPACE) fixedStep = !fixedStep;
                    else stepTime = (stepTime < 0.01f) ? 1.0f / 30.0f
                                                       : 1.0f / 120.0f;
                    accumulator = 0.0f;
                }
                else if (event.key.key == SDLK_RETURN)
                {
                    bodies = dropBalls();
                    held = -1;
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                     event.button.button == SDL_BUTTON_LEFT)
            {
                // The ball under the mouse, the last drawn if two overlap.
                Vec2 press = { event.button.x, event.button.y };
                for (int i = 0; i < (int)bodies.size(); ++i)
                    if (distance(press, bodies[i].pos) <= bodies[i].radius)
                        held = i;
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) held = -1;
        }

        // The mouse's position, read once a frame.
        float mx, my;
        SDL_GetMouseState(&mx, &my);

        // A lag spike makes this one frame a fifth of a second long, past
        // the skeleton's clamp, as if the game had stalled.
        float frameTime = spike ? 0.2f : dt;
        spike = false;

        // The physics: fixed steps, as many as the time saved up holds, as
        // in Bullet Time, or one step as long as the frame.
        Vec2 mouse = { mx, my };
        if (fixedStep)
        {
            accumulator += frameTime;
            while (accumulator >= stepTime)
            {
                step(bodies, walls, held, mouse, stepTime);
                accumulator -= stepTime;
                ++steps;
            }
        }
        else
        {
            step(bodies, walls, held, mouse, frameTime);
            ++steps;
        }

        // The frames and steps in each second, counted for the readout.
        ++frames;
        secondSum += frameSeconds;
        if (secondSum >= 1.0f)
        {
            shownFrames = frames;
            shownSteps = steps;
            secondSum -= 1.0f;
            frames = steps = 0;
        }

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);

        // Each ball is drawn between its last two places, the share of a
        // step the leftover time makes, when interpolating.
        bool between = fixedStep && interpolate;
        float alpha = between ? accumulator / stepTime : 1.0f;
        setColor(renderer, SILVER);
        SDL_FRect box = rectOf(BOX);
        SDL_RenderRect(renderer, &box);
        for (const Body& b : bodies)
        {
            setColor(renderer, b.color);
            fillCircle(renderer, lerp(b.prevPos, b.pos, alpha), b.radius);
        }

        // The spring, from the ball being held to the mouse.
        if (held >= 0)
        {
            Vec2 at = lerp(bodies[held].prevPos, bodies[held].pos, alpha);
            setColor(renderer, TEXT);
            SDL_RenderLine(renderer, at.x, at.y, mouse.x, mouse.y);
        }

        // The readout: the steps and the interpolation, then the last second's
        // frames and steps, and the deepest overlap.
        std::string mode = "variable steps, one a frame";
        if (fixedStep)
        {
            long rate = std::lround(1.0f / stepTime);
            mode = "fixed steps, " + std::to_string(rate) + " a second, " +
                   (interpolate ? "interpolated" : "not interpolated");
        }
        drawText(renderer, font, mode, 12, 10, fixedStep ? TEXT : AMBER);
        drawText(renderer, font,
                 "last second: " + std::to_string(shownFrames) + " frames, " +
                 std::to_string(shownSteps) + " steps   deepest overlap " +
                 fmt2(deepestOverlap(bodies, walls)) + " px",
                 12, 30, SILVER);
        drawText(renderer, font,
                 "[Drag] a ball to throw it   [Space] fixed or variable "
                 "steps   [L] lag spike", 12, WINDOW_HEIGHT - 46, GREY);
        drawText(renderer, font,
                 "[R] 120 or 30 steps a second   [I] interpolate   "
                 "[Enter] new pile   [Esc] quit", 12, WINDOW_HEIGHT - 26, GREY);

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
