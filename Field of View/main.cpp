// Field of View - the Chapter 4 demo (the dot product) for
// Learning C++ by Building Games: Fundamentals
//
// A guard at the center aims wherever the mouse points. A target is in
// view when the dot product of the direction to it with the guard's facing
// is at least the cosine of half the cone's angle. Up and Down widen and
// narrow the cone. The target nearest the aim is split into two parts, one
// along the aim and one across it: its projection, and what's left over.

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <cmath>
#include <string>
#include <vector>
#include "Vec2.h"

constexpr int WINDOW_WIDTH  = 1000;
constexpr int WINDOW_HEIGHT = 700;
constexpr float PI = 3.14159265f;

constexpr SDL_Color TEXT = { 235, 238, 242, 255 };
constexpr SDL_Color GREY = { 90, 100, 112, 255 };
constexpr SDL_Color BLUE = { 47, 111, 235, 255 };
constexpr SDL_Color GREEN = { 47, 158, 68, 255 };
constexpr SDL_Color RED = { 224, 49, 49, 255 };
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

// A number as text with two decimals, such as 0.77.
std::string fmt2(float v)
{
    char buf[32];
    SDL_snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
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
        SDL_CreateWindow("Field of View", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The guard stands at the center, and its facing, a unit vector,
    // turns to the mouse. The half-angle runs from the aim to one edge.
    const Vec2 guard = { WINDOW_WIDTH / 2.0f, WINDOW_HEIGHT / 2.0f };
    Vec2 facing = { 1.0f, 0.0f };
    float halfAngleDeg = 40.0f;

    std::vector<Vec2> targets = {
        { 780, 340 }, { 700, 540 }, { 560, 230 }, { 300, 320 },
        { 380, 560 }, { 640, 400 }, { 470, 150 }, { 250, 520 }
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
                else if (event.key.key == SDLK_UP) halfAngleDeg += 5.0f;
                else if (event.key.key == SDLK_DOWN) halfAngleDeg -= 5.0f;
                halfAngleDeg = SDL_clamp(halfAngleDeg, 10.0f, 90.0f);
            }
        }

        // The mouse's position, read once a frame.
        float mx, my;
        SDL_GetMouseState(&mx, &my);

        // Aim the guard: its facing is the direction to the mouse. On the
        // guard itself there's no direction, so the facing stays as it was.
        Vec2 toMouse = Vec2{ mx, my } - guard;
        if (toMouse.lengthSquared() > 1.0f) facing = toMouse.normalized();

        // The dot product at the cone's edge: the cosine of the half-angle.
        float halfAngle = halfAngleDeg * PI / 180.0f;
        float threshold = std::cos(halfAngle);

        // The target nearest the aim: the one whose direction has the
        // largest dot product with the facing.
        float bestDot = -2.0f;
        Vec2 best = targets[0];
        for (const Vec2& t : targets)
        {
            float d = (t - guard).normalized().dot(facing);
            if (d > bestDot)
            {
                bestDot = d;
                best = t;
            }
        }

        // Its projection onto the aim: how far ahead of the guard it is.
        // What's left over is across the aim, to the right when it's
        // positive, as the quarter turn is clockwise on the screen.
        Vec2 toBest = best - guard;
        float ahead = toBest.dot(facing);
        float side = toBest.dot(facing.perp());
        Vec2 foot = guard + facing * ahead;

        // The angle to it, from its dot product. acos takes -1 to 1, and
        // rounding can push a dot product just outside that.
        float clamped = SDL_clamp(bestDot, -1.0f, 1.0f);
        int bestAngle = (int)(std::acos(clamped) * 180.0f / PI + 0.5f);

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);
        drawGrid(renderer);

        // The cone's edges. A unit vector at an angle a from the facing is
        // the facing times cos(a), plus a quarter turn of it times sin(a).
        Vec2 across = facing.perp() * std::sin(halfAngle);
        Vec2 edge1 = guard + (facing * threshold + across) * 360.0f;
        Vec2 edge2 = guard + (facing * threshold - across) * 360.0f;
        SDL_SetRenderDrawColor(renderer, GREY.r, GREY.g, GREY.b, GREY.a);
        SDL_RenderLine(renderer, guard.x, guard.y, edge1.x, edge1.y);
        SDL_RenderLine(renderer, guard.x, guard.y, edge2.x, edge2.y);

        // The projection, under the facing arrow: along the aim to the
        // foot, then across to the target.
        SDL_SetRenderDrawColor(renderer, AMBER.r, AMBER.g, AMBER.b, AMBER.a);
        SDL_RenderLine(renderer, guard.x, guard.y, foot.x, foot.y);
        SDL_RenderLine(renderer, foot.x, foot.y, best.x, best.y);
        fillCircle(renderer, foot, 3);
        drawArrow(renderer, guard, guard + facing * 190.0f, BLUE);

        // Each target is in view when its direction's dot product with the
        // facing reaches the threshold.
        int inView = 0;
        for (const Vec2& t : targets)
        {
            bool visible = (t - guard).normalized().dot(facing) >= threshold;
            if (visible) inView++;
            SDL_Color c = visible ? GREEN : RED;
            SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
            fillCircle(renderer, t, 8);
        }

        std::string cone = "field of view " +
                           std::to_string((int)(halfAngleDeg * 2)) +
                           " deg: in view when dot >= cos " +
                           std::to_string((int)halfAngleDeg) + " deg = " +
                           fmt2(threshold);
        std::string count = "targets in view: " + std::to_string(inView) +
                            " of " + std::to_string((int)targets.size());
        std::string nearest = "nearest the aim: dot " + fmt2(bestDot) +
                              ", angle " + std::to_string(bestAngle) + " deg";
        std::string parts = "its projection: " +
                            std::to_string(std::lround(ahead)) +
                            " px ahead, and " +
                            std::to_string(std::lround(std::abs(side))) +
                            " px " + (side >= 0.0f ? "right" : "left") +
                            " of the aim";
        drawText(renderer, font, cone, 12, 10, TEXT);
        drawText(renderer, font, count, 12, 30, inView > 0 ? GREEN : TEXT);
        drawText(renderer, font, nearest, 12, 50, TEXT);
        drawText(renderer, font, parts, 12, 70, AMBER);
        drawText(renderer, font,
                 "[mouse] aim   [Up/Down] widen or narrow   [Esc] quit",
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
