// Thrust - the Chapter 7 demo (delta time, velocity and acceleration) for
// Learning C++ by Building Games: Fundamentals
//
// A ship in the style of Asteroids. Left and Right, or A and D, turn it, and
// Up, or W, thrusts it forward. Thrust changes the velocity, the velocity
// changes the position, and momentum carries the ship on when you let go.
// Every change is scaled by delta time, so the ship moves the same at any
// frame rate. It wraps around the window's edges.

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
        SDL_CreateWindow("Thrust", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The ship: where it is, how fast it's going, and which way it faces,
    // in radians. It starts in the middle, still, pointing up.
    Vec2 pos = { WINDOW_WIDTH / 2.0f, WINDOW_HEIGHT / 2.0f };
    Vec2 vel = { 0.0f, 0.0f };
    float heading = -PI / 2.0f;

    const float turnRate = 3.2f;       // radians a second
    const float thrust = 320.0f;       // pixels a second, every second
    const float drag = 0.4f;           // a share of the speed lost a second

    // The ship's outline, and its flame, as points around its own center,
    // facing along its x axis.
    const std::vector<Vec2> hull = { { 30, 0 }, { -21, -18 }, { -21, 18 } };
    const std::vector<Vec2> flame = { { -21, -9 }, { -42, 0 }, { -21, 9 } };

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
            }
        }

        // Keys held down: SDL's array of which keys are down this frame.
        // Turning changes the heading, at a steady rate.
        const bool* keys = SDL_GetKeyboardState(nullptr);
        bool left = keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A];
        bool right = keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D];
        bool thrusting = keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W];
        if (left) heading = wrapAngle(heading - turnRate * dt);
        if (right) heading = wrapAngle(heading + turnRate * dt);

        // The physics, by semi-implicit Euler: the acceleration changes the
        // velocity first, then the new velocity changes the position. Drag
        // takes a share of the velocity away each second.
        Vec2 acc = thrusting ? Vec2::fromAngle(heading) * thrust : Vec2{};
        vel += acc * dt;
        vel -= vel * (drag * dt);
        pos += vel * dt;

        // Off one edge, back on at the opposite one.
        if (pos.x < 0) pos.x += WINDOW_WIDTH;
        if (pos.x > WINDOW_WIDTH) pos.x -= WINDOW_WIDTH;
        if (pos.y < 0) pos.y += WINDOW_HEIGHT;
        if (pos.y > WINDOW_HEIGHT) pos.y -= WINDOW_HEIGHT;

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);
        drawGrid(renderer);

        // The velocity, drawn as an arrow to where the ship will be in
        // 0.35 seconds, if nothing changes.
        drawArrow(renderer, pos, pos + vel * 0.35f, GREEN);

        // The ship, and its flame while it thrusts: each local point turned
        // to the heading, then moved to the ship's position.
        std::vector<Vec2> ship;
        for (const Vec2& p : hull) ship.push_back(pos + p.rotated(heading));
        drawPolygon(renderer, ship, BLUE);
        if (thrusting)
        {
            std::vector<Vec2> fire;
            for (const Vec2& p : flame)
                fire.push_back(pos + p.rotated(heading));
            drawPolygon(renderer, fire, AMBER);
        }

        long speed = std::lround(vel.length());
        long degrees = std::lround(heading * 180.0f / PI);
        drawText(renderer, font,
                 "speed: " + std::to_string(speed) + " px/s   heading: " +
                 std::to_string(degrees) + " deg", 12, 10, TEXT);
        drawText(renderer, font,
                 "top speed: thrust / drag = " +
                 std::to_string(std::lround(thrust / drag)) + " px/s",
                 12, 30, GREY);
        drawText(renderer, font,
                 "[Left/Right or A/D] turn   [Up or W] thrust   [Esc] quit",
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
