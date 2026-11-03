// Noise Fields - the Chapter 21 demo (randomness and noise) for
// Learning C++ by Building Games: Fundamentals
//
// Fractal value noise, read as directions, makes a flow field, and
// thousands of particles drift along it, each drawn as a short streak. N
// shows the noise itself in greys, O changes its octaves, and W swaps it
// for white noise. Space picks a new seed at random, and Left and Right
// step the seed down and up. A click sprays new particles from the mouse,
// and S switches the spray between an even scatter and the naive one.

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>
#include "Vec2.h"

constexpr int WINDOW_WIDTH  = 1000;
constexpr int WINDOW_HEIGHT = 700;

constexpr SDL_Color TEXT = { 235, 238, 242, 255 };
constexpr SDL_Color GREY = { 90, 100, 112, 255 };
constexpr SDL_Color AMBER = { 232, 89, 12, 255 };
constexpr SDL_Color STREAK = { 150, 200, 240, 255 };
constexpr SDL_Color PANEL = { 24, 26, 30, 220 };

// The noise's scale: its units for each pixel. At 0.0035, a square of its
// grid, one unit across, is about 286 pixels wide.
constexpr float SCALE = 0.0035f;

// The particles: how many, their speed in pixels a second, and the length
// of each one's streak.
constexpr int PARTICLE_COUNT = 2500;
constexpr float SPEED = 65.0f;
constexpr float TAIL = 8.0f;

// A click's spray: how many particles, and the radius of its circle.
constexpr int SPRAY_COUNT = 300;
constexpr float SPRAY_RADIUS = 60.0f;

// The noise drawn in greys: how many shades, and the size of each block.
constexpr int SHADES = 64;
constexpr float BLOCK = 5.0f;

// A particle: where it is, the way it's heading, its age and how long it
// lives, both in seconds, and whether it came from a spray.
struct Particle
{
    Vec2 pos;
    Vec2 dir;
    float age;
    float life;
    bool sprayed;
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

// Set the color the renderer draws with, from an SDL_Color.
void setColor(SDL_Renderer* renderer, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

// Slow at both ends: the cubic 3t^2 - 2t^3, with t^2 taken out.
float smoothstep(float t) { return t * t * (3.0f - 2.0f * t); }

// A random float from lo to hi. The generator is made once, and seeded
// from the system, so that every run is different.
std::mt19937 rng{ std::random_device{}() };

float randRange(float lo, float hi)
{
    std::uniform_real_distribution<float> spread(lo, hi);
    return spread(rng);
}

// A value from 0 up to 1, made from a corner (xi, yi) and a seed. It's the
// same every time for the same three, but with no pattern from one corner
// to the next. Multiplying by large odd numbers, and folding the high bits
// down onto the low ones, mixes every bit of the inputs into the result.
// The low 24 bits are kept, since a float holds every 24-bit whole number
// exactly.
float hash01(int xi, int yi, uint32_t seed)
{
    uint32_t n = (uint32_t)xi * 374761393u + (uint32_t)yi * 668265263u +
                 seed * 2246822519u;
    n = (n ^ (n >> 13)) * 1274126177u;
    n ^= n >> 16;
    return (float)(n & 0x00ffffffu) / 16777216.0f;
}

// Value noise: a random value at each corner of a grid of squares, one unit
// across, and a smooth blend between the four corners around (x, y). The
// blend goes along the top and bottom edges first, then between those two.
float valueNoise(float x, float y, uint32_t seed)
{
    float fx = std::floor(x);
    float fy = std::floor(y);
    int xi = (int)fx;
    int yi = (int)fy;
    float u = smoothstep(x - fx);
    float v = smoothstep(y - fy);
    float top = lerp(hash01(xi, yi, seed), hash01(xi + 1, yi, seed), u);
    float bottom = lerp(hash01(xi, yi + 1, seed),
                        hash01(xi + 1, yi + 1, seed), u);
    return lerp(top, bottom, v);
}

// Fractal noise: octaves of value noise, each at twice the frequency and
// half the amplitude of the one before, and each with a seed of its own.
// Dividing by the amplitudes' total keeps the result from 0 up to 1.
float fractalNoise(float x, float y, int octaves, uint32_t seed)
{
    float sum = 0.0f;
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    for (int i = 0; i < octaves; ++i)
    {
        sum += valueNoise(x * frequency, y * frequency, seed + i) * amplitude;
        total += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return sum / total;
}

// The field's value at a point, from 0 up to 1: fractal noise, or, for
// white noise, the hash of the pixel the point is in, so that every pixel
// has a value of its own.
float fieldAt(Vec2 p, int octaves, bool white, uint32_t seed)
{
    if (white) return hash01((int)p.x, (int)p.y, seed);
    return fractalNoise(p.x * SCALE, p.y * SCALE, octaves, seed);
}

// The way the field points at a point: its value as an angle, where 0 to 1
// makes two whole turns.
Vec2 flowAt(Vec2 p, int octaves, bool white, uint32_t seed)
{
    return Vec2::fromAngle(fieldAt(p, octaves, white, seed) * 4.0f * PI);
}

// A random point in a circle. An even scatter takes the square root of a
// uniform number for the radius, since the area within a radius grows as
// its square. The naive one takes the number as it is, which crowds the
// points toward the middle.
Vec2 randomInCircle(Vec2 center, float radius, bool even)
{
    float angle = randRange(0.0f, 2.0f * PI);
    float u = randRange(0.0f, 1.0f);
    float r = even ? radius * std::sqrt(u) : radius * u;
    return center + Vec2::fromAngle(angle, r);
}

// A particle starts again at a random place in the window, with a life of
// 2 to 6 seconds.
void respawn(Particle& p)
{
    p.pos = { randRange(0.0f, (float)WINDOW_WIDTH),
              randRange(0.0f, (float)WINDOW_HEIGHT) };
    p.dir = { 1.0f, 0.0f };
    p.age = 0.0f;
    p.life = randRange(2.0f, 6.0f);
    p.sprayed = false;
}

// The noise in greys, as blocks BLOCK pixels square, each the field's value
// at its middle. The blocks are sorted by shade, so that each shade can be
// drawn in one call.
void shadeField(std::vector<std::vector<SDL_FRect>>& shades, int octaves,
                bool white, uint32_t seed)
{
    for (std::vector<SDL_FRect>& shade : shades) shade.clear();
    for (float y = 0.0f; y < WINDOW_HEIGHT; y += BLOCK)
        for (float x = 0.0f; x < WINDOW_WIDTH; x += BLOCK)
        {
            Vec2 middle = { x + BLOCK / 2.0f, y + BLOCK / 2.0f };
            float n = fieldAt(middle, octaves, white, seed);
            int s = std::min((int)(n * SHADES), SHADES - 1);
            shades[s].push_back({ x, y, BLOCK, BLOCK });
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
        SDL_CreateWindow("Noise Fields", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The particles, scattered over the window. Then the noise: its seed,
    // fixed so that the field is the same at every launch, its octaves, and
    // whether it's white noise. Then whether the noise is shown, whether
    // the spray is even, the next particle a spray takes, and the noise's
    // blocks, sorted by shade, with a flag for when they're out of date.
    // The panels behind the readout are see-through, so the renderer blends.
    std::vector<Particle> particles(PARTICLE_COUNT);
    for (Particle& p : particles) respawn(p);
    uint32_t seed = 1234;
    int octaves = 4;
    bool white = false;
    bool showNoise = false;
    bool evenSpray = true;
    int nextSpray = 0;
    std::vector<std::vector<SDL_FRect>> shades(SHADES);
    bool reshade = true;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

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
                else if (event.key.key == SDLK_SPACE) seed = (uint32_t)rng();
                else if (event.key.key == SDLK_RIGHT) ++seed;
                else if (event.key.key == SDLK_LEFT) --seed;
                else if (event.key.key == SDLK_O) octaves = octaves % 4 + 1;
                else if (event.key.key == SDLK_W) white = !white;
                else if (event.key.key == SDLK_N) showNoise = !showNoise;
                else if (event.key.key == SDLK_S) evenSpray = !evenSpray;
                reshade = true;   // A key may have changed the field.
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                     event.button.button == SDL_BUTTON_LEFT)
            {
                // A spray: the next particles in the list start again in a
                // circle around the mouse, in amber.
                Vec2 at = { event.button.x, event.button.y };
                for (int i = 0; i < SPRAY_COUNT; ++i)
                {
                    Particle& p = particles[nextSpray];
                    respawn(p);
                    p.pos = randomInCircle(at, SPRAY_RADIUS, evenSpray);
                    p.sprayed = true;
                    nextSpray = (nextSpray + 1) % PARTICLE_COUNT;
                }
            }
        }

        // Each particle heads the way the field points where it is, and
        // starts again somewhere new when it's too old or leaves the window.
        for (Particle& p : particles)
        {
            p.dir = flowAt(p.pos, octaves, white, seed);
            p.pos += p.dir * (SPEED * dt);
            p.age += dt;
            bool outside = p.pos.x < 0.0f || p.pos.x > WINDOW_WIDTH ||
                           p.pos.y < 0.0f || p.pos.y > WINDOW_HEIGHT;
            if (p.age > p.life || outside) respawn(p);
        }

        // The noise's greys are worked out again only when they're shown,
        // and something has changed.
        if (showNoise && reshade)
        {
            shadeField(shades, octaves, white, seed);
            reshade = false;
        }

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);

        // The noise, if it's shown: each shade's blocks in one call.
        if (showNoise)
        {
            for (int s = 0; s < SHADES; ++s)
            {
                if (shades[s].empty()) continue;
                Uint8 g = (Uint8)(20 + 2 * s);
                SDL_SetRenderDrawColor(renderer, g, g, g, 255);
                SDL_RenderFillRects(renderer, shades[s].data(),
                                    (int)shades[s].size());
            }
        }

        // Each particle is a streak, from TAIL pixels behind it to where
        // it is.
        for (const Particle& p : particles)
        {
            setColor(renderer, p.sprayed ? AMBER : STREAK);
            Vec2 tail = p.pos - p.dir * TAIL;
            SDL_RenderLine(renderer, tail.x, tail.y, p.pos.x, p.pos.y);
        }

        // The readout and the controls, on panels the field shows faintly
        // through. The readout gives the seed, the noise and the spray.
        setColor(renderer, PANEL);
        SDL_FRect top = { 0, 0, WINDOW_WIDTH, 34 };
        SDL_FRect bottom = { 0, WINDOW_HEIGHT - 52, WINDOW_WIDTH, 52 };
        SDL_RenderFillRect(renderer, &top);
        SDL_RenderFillRect(renderer, &bottom);
        std::string noise = "white noise";
        if (!white)
            noise = std::to_string(octaves) +
                    (octaves == 1 ? " octave" : " octaves") + " of value noise";
        drawText(renderer, font, "seed " + std::to_string(seed) + "   " +
                 noise + "   " + (evenSpray ? "even" : "naive") + " spray",
                 12, 10, TEXT);
        drawText(renderer, font,
                 "[Space] new seed   [Left] [Right] seed down or up   "
                 "[O] octaves   [W] white noise", 12, WINDOW_HEIGHT - 46, GREY);
        drawText(renderer, font,
                 "[N] show the noise   [Click] spray   [S] even or naive "
                 "spray   [Esc] quit", 12, WINDOW_HEIGHT - 26, GREY);

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
