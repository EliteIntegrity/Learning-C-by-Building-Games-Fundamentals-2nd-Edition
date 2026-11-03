// Chase - the Chapter 3 demo for
// Learning C++ by Building Games: Fundamentals
//
// Two dots chase the mouse. The blue one normalizes the direction to the
// mouse and moves at a constant speed, stopping when it arrives. The amber
// one steps a share of the gap each frame, so it slows as it closes in.

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <cmath>
#include <string>
#include "Vec2.h"

constexpr int WINDOW_WIDTH  = 1000;
constexpr int WINDOW_HEIGHT = 700;

constexpr SDL_Color TEXT = { 235, 238, 242, 255 };
constexpr SDL_Color GREY = { 90, 100, 112, 255 };
constexpr SDL_Color BLUE = { 47, 111, 235, 255 };
constexpr SDL_Color AMBER = { 232, 89, 12, 255 };

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
        SDL_CreateWindow("Chase", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    Vec2 smart = { 150.0f, 300.0f };   // moves at a constant speed
    Vec2 naive = { 150.0f, 430.0f };   // steps a share of the gap
    const float speed = 280.0f;        // pixels a second
    const float rate = 3.0f;           // a frame closes rate * dt of the gap

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

        // The mouse's position, read once a frame.
        float mx, my;
        SDL_GetMouseState(&mx, &my);
        Vec2 target = { mx, my };

        // The blue dot moves a step of speed * dt toward the target, along
        // the direction to it: toTarget / dist is toTarget.normalized(),
        // with no second square root. A step that would reach the target
        // ends on it, so the dot can't overshoot.
        Vec2 smartWas = smart;
        Vec2 toTarget = target - smart;
        float dist = toTarget.length();
        float step = speed * dt;
        if (dist <= step) smart = target;
        else smart += toTarget / dist * step;

        // The amber dot steps a share of the whole gap. The closer it gets,
        // the smaller the gap, and the slower it moves.
        Vec2 naiveWas = naive;
        Vec2 gap = target - naive;
        naive += gap * (rate * dt);

        // Each dot's speed this frame: how far it moved, over dt.
        float smartSpeed = (dt > 0.0f) ? distance(smartWas, smart) / dt : 0.0f;
        float naiveSpeed = (dt > 0.0f) ? distance(naiveWas, naive) / dt : 0.0f;

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);
        drawGrid(renderer);

        // The target: a small cross at the mouse.
        SDL_SetRenderDrawColor(renderer, 200, 205, 212, 255);
        SDL_RenderLine(renderer, mx - 8, my, mx + 8, my);
        SDL_RenderLine(renderer, mx, my - 8, mx, my + 8);

        // Each dot, with an arrow: the blue one's direction, at a fixed
        // length, and the amber one's whole gap.
        if (smart != target)
        {
            Vec2 dir = (target - smart).normalized();
            drawArrow(renderer, smart, smart + dir * 55.0f, BLUE);
        }
        SDL_SetRenderDrawColor(renderer, BLUE.r, BLUE.g, BLUE.b, BLUE.a);
        fillCircle(renderer, smart, 9);
        drawArrow(renderer, naive, target, AMBER);
        SDL_SetRenderDrawColor(renderer, AMBER.r, AMBER.g, AMBER.b, AMBER.a);
        fillCircle(renderer, naive, 9);

        std::string blue = "blue  (normalize, then step):  " +
                           std::to_string((int)distance(smart, target)) +
                           " px away, moving at " +
                           std::to_string((int)(smartSpeed + 0.5f)) + " px/s";
        std::string amber = "amber (a share of the gap):    " +
                            std::to_string((int)distance(naive, target)) +
                            " px away, moving at " +
                            std::to_string((int)(naiveSpeed + 0.5f)) + " px/s";
        drawText(renderer, font, blue, 12, 10, BLUE);
        drawText(renderer, font, amber, 12, 30, AMBER);
        drawText(renderer, font,
                 "move the mouse: blue keeps its speed, amber slows as it "
                 "nears   [Esc] quit", 12, WINDOW_HEIGHT - 26, GREY);

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
