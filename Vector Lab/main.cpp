// Vector Lab - the Chapter 2 demo for
// Learning C++ by Building Games: Fundamentals
//
// Drag with the left mouse button to set vector a, and with the right
// button to set vector b. Watch a + b build as a parallelogram. Space
// switches between a + b and a - b.

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

// A vector as text, such as "a = (180, -110)".
std::string vecLabel(const std::string& name, Vec2 v)
{
    return name + " = (" + std::to_string((int)v.x) + ", " +
           std::to_string((int)v.y) + ")";
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
        SDL_CreateWindow("Vector Lab", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The vectors start at the origin, O, in the middle of the window.
    const Vec2 origin = { WINDOW_WIDTH / 2.0f, WINDOW_HEIGHT / 2.0f };
    Vec2 a = { 180.0f, -110.0f };
    Vec2 b = { 90.0f, 130.0f };
    bool showSum = true;

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
                else if (event.key.key == SDLK_SPACE) showSum = !showSum;
            }
        }

        // The mouse's position, read once a frame.
        float mx, my;
        SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mx, &my);

        // A held left button sets a, and the right button sets b: each is
        // the vector from O to the mouse, the mouse's position minus O's.
        Vec2 mouse = { mx, my };
        if (buttons & SDL_BUTTON_LMASK) a = mouse - origin;
        if (buttons & SDL_BUTTON_RMASK) b = mouse - origin;
        Vec2 result = showSum ? a + b : a - b;

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);
        drawGrid(renderer);

        // The axes through O.
        SDL_SetRenderDrawColor(renderer, 70, 78, 90, 255);
        SDL_RenderLine(renderer, 0, origin.y, WINDOW_WIDTH, origin.y);
        SDL_RenderLine(renderer, origin.x, 0, origin.x, WINDOW_HEIGHT);

        if (showSum)
        {
            // b placed at a's head, and a at b's head: the parallelogram's
            // dashed sides, which meet at a + b.
            drawDashed(renderer, origin + a, origin + a + b, AMBER);
            drawDashed(renderer, origin + b, origin + b + a, BLUE);
        }
        else
        {
            // a - b runs from b's head to a's head.
            drawDashed(renderer, origin + b, origin + a, GREEN);
        }

        drawArrow(renderer, origin, origin + result, GREEN);
        drawArrow(renderer, origin, origin + a, BLUE);
        drawArrow(renderer, origin, origin + b, AMBER);

        // Each arrow's name, beside its head.
        Vec2 nameA = origin + a + Vec2{ 6, -8 };
        Vec2 nameB = origin + b + Vec2{ 6, -8 };
        Vec2 nameR = origin + result + Vec2{ 6, -8 };
        drawText(renderer, font, "a", nameA.x, nameA.y, BLUE);
        drawText(renderer, font, "b", nameB.x, nameB.y, AMBER);
        drawText(renderer, font, showSum ? "a+b" : "a-b", nameR.x, nameR.y,
                 GREEN);

        drawText(renderer, font, vecLabel("a", a), 12, 10, TEXT);
        drawText(renderer, font, vecLabel("b", b), 12, 30, TEXT);
        std::string name = showSum ? "a + b" : "a - b";
        drawText(renderer, font, vecLabel(name, result), 12, 50, GREEN);
        drawText(renderer, font,
                 "[left drag] a   [right drag] b   [Space] a + b or a - b"
                 "   [Esc] quit", 12, WINDOW_HEIGHT - 26, GREY);

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
