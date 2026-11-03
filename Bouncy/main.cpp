// Bouncy - the Chapter 10 demo (gravity, friction and bouncing) for
// Learning C++ by Building Games: Fundamentals
//
// Balls fall under gravity, and bounce off the window's sides and a ground
// that tilts. Each bounce splits a ball's velocity in two: the part across
// the surface reverses and loses speed, and the part along it is slowed by
// friction while the ball touches. Click to drop a ball. Left and Right
// tilt the ground, Up and Down change the bounciness, F changes the
// friction, and D switches between thin air and thick.

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

// Gravity: a fall's speed grows by this many pixels a second, every
// second, whatever is falling.
constexpr float GRAVITY = 900.0f;

// A ball that meets a surface slower than this, in pixels a second, stops
// bouncing and rests.
constexpr float REST_SPEED = 60.0f;

// A ball, and the five colors balls take in turn.
struct Ball
{
    Vec2 pos;
    Vec2 vel;
    float radius;
    SDL_Color color;
};

constexpr SDL_Color BALL_COLORS[] = { BLUE, GREEN, RED, AMBER, VIOLET };

// A flat surface: a point on it, and its normal, a unit vector that points
// out of it, to the side the balls are on.
struct Surface
{
    Vec2 point;
    Vec2 normal;
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

// A random float from lo to hi. The generator is made once, and seeded
// from the system, so that every run is different.
std::mt19937 rng{ std::random_device{}() };

float randRange(float lo, float hi)
{
    std::uniform_real_distribution<float> spread(lo, hi);
    return spread(rng);
}

// A new ball at a point, with a random size, a small random speed across
// the window, and the next color of the five.
Ball makeBall(Vec2 at, size_t count)
{
    Vec2 vel = { randRange(-120.0f, 120.0f), 0.0f };
    return { at, vel, randRange(9.0f, 17.0f), BALL_COLORS[count % 5] };
}

// How far a ball's center is from a surface, along its normal: positive on
// the open side, and negative once the center has sunk through.
float distanceTo(const Ball& b, const Surface& s)
{
    return (b.pos - s.point).dot(s.normal);
}

// A ball touches a surface when it's within half a pixel of it, or when
// this frame's move would carry it in. Then it's set down on the surface,
// and its velocity splits into the part across the surface and the part
// along it. The part across bounces back, scaled by restitution, unless
// it's too slow to bother with, when it stops and the ball rests. Friction
// slows the part along, by up to friction times how hard gravity presses
// the ball into the surface, but never past zero.
void touch(Ball& b, const Surface& s, float restitution, float friction,
           float dt)
{
    float across = b.vel.dot(s.normal);
    float gap = distanceTo(b, s) - b.radius;
    if (across > 0.0f || gap + across * dt > 0.5f) return;   // not touching

    b.pos -= s.normal * gap;
    Vec2 along = b.vel - s.normal * across;
    across = (across < -REST_SPEED) ? -across * restitution : 0.0f;

    float press = std::max(0.0f, -GRAVITY * s.normal.y);
    float slow = friction * press * dt;
    float speed = along.length();
    along = (speed > slow) ? along * ((speed - slow) / speed) : Vec2{};

    b.vel = along + s.normal * across;
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
        SDL_CreateWindow("Bouncy", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // Eight balls to start, at random places near the top.
    std::vector<Ball> balls;
    for (int i = 0; i < 8; ++i)
    {
        Vec2 at = { randRange(150.0f, 850.0f), randRange(50.0f, 200.0f) };
        balls.push_back(makeBall(at, balls.size()));
    }

    // The ground: a point on it, and its tilt, in radians, from level.
    const Vec2 groundPoint = { WINDOW_WIDTH / 2.0f, 440.0f };
    const float maxTilt = 25.0f * PI / 180.0f;
    float tilt = 0.0f;

    // The settings the keys change: how bouncy the balls are, how much
    // friction the surfaces have, and whether the air is thin or thick.
    float restitution = 0.7f;
    const float frictions[] = { 0.0f, 0.3f, 0.6f };
    int frictionChoice = 1;
    bool thickAir = false;

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
                else if (event.key.key == SDLK_UP) restitution += 0.05f;
                else if (event.key.key == SDLK_DOWN) restitution -= 0.05f;
                else if (event.key.key == SDLK_F)
                    frictionChoice = (frictionChoice + 1) % 3;
                else if (event.key.key == SDLK_D) thickAir = !thickAir;
                restitution = std::clamp(restitution, 0.0f, 1.0f);
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                     event.button.button == SDL_BUTTON_LEFT)
            {
                Vec2 at = { event.button.x, event.button.y };
                balls.push_back(makeBall(at, balls.size()));
            }
        }

        // Left and Right tilt the ground while they're held, by 0.35
        // radians, or 20 degrees, a second, up to 25 degrees either way.
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_LEFT]) tilt -= 0.35f * dt;
        if (keys[SDL_SCANCODE_RIGHT]) tilt += 0.35f * dt;
        tilt = std::clamp(tilt, -maxTilt, maxTilt);

        // The four surfaces: the ground, whose normal turns with its tilt,
        // and the window's left, right and top edges.
        Vec2 groundNormal = Vec2::fromAngle(-PI / 2.0f + tilt);
        const Surface surfaces[] = {
            { groundPoint, groundNormal },
            { { 0.0f, 0.0f }, { 1.0f, 0.0f } },
            { { (float)WINDOW_WIDTH, 0.0f }, { -1.0f, 0.0f } },
            { { 0.0f, 0.0f }, { 0.0f, 1.0f } },
        };

        float friction = frictions[frictionChoice];
        float drag = thickAir ? 3.0f : 0.15f;

        // Each ball: gravity and the air change its velocity, and so does
        // every surface it touches. Then the velocity moves it.
        for (Ball& b : balls)
        {
            b.vel.y += GRAVITY * dt;
            b.vel *= std::exp(-drag * dt);
            for (const Surface& s : surfaces)
                touch(b, s, restitution, friction, dt);
            b.pos += b.vel * dt;
        }

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);
        drawGrid(renderer);

        // The ground: a long line along the surface, which is the normal
        // turned a quarter turn, and the normal itself, as an arrow.
        Vec2 along = groundNormal.perp();
        Vec2 left = groundPoint - along * 700.0f;
        Vec2 right = groundPoint + along * 700.0f;
        setColor(renderer, SILVER);
        SDL_RenderLine(renderer, left.x, left.y, right.x, right.y);
        drawArrow(renderer, groundPoint, groundPoint + groundNormal * 60.0f,
                  AMBER);

        // The balls, and the fastest one's speed, for the readout.
        float fastest = 0.0f;
        for (const Ball& b : balls)
        {
            setColor(renderer, b.color);
            fillCircle(renderer, b.pos, b.radius);
            fastest = std::max(fastest, b.vel.length());
        }

        long degrees = std::lround(tilt * 180.0f / PI);
        drawText(renderer, font,
                 "balls: " + std::to_string(balls.size()) +
                 "   bounciness: " + fmt2(restitution) +
                 "   friction: " + fmt2(friction) +
                 "   tilt: " + std::to_string(degrees) + " deg",
                 12, 10, TEXT);
        long terminal = std::lround(GRAVITY / drag);
        std::string air = thickAir ? "thick air" : "thin air";
        drawText(renderer, font,
                 air + ": terminal speed " + std::to_string(terminal) +
                 " px/s   fastest ball: " +
                 std::to_string(std::lround(fastest)) + " px/s",
                 12, 30, GREY);
        drawText(renderer, font,
                 "[Click] drop   [Left/Right] tilt   [Up/Down] bounciness"
                 "   [F] friction   [D] air   [Esc] quit",
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
