// Rotator - the Chapter 6 demo (rotating points and vectors) for
// Learning C++ by Building Games: Fundamentals
//
// An arrow-shaped polygon rotates. At first it spins about its own center,
// beside a faint copy that doesn't turn. Space switches to orbiting a pivot
// that follows the mouse, so you can watch the whole shape swing around any
// point. Two short arrows show where the shape's own x and y axes have gone.

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
        SDL_CreateWindow("Rotator", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The arrow, as points around its own center, which is its origin. It
    // sits at home, to the right of the window's middle. spin is the angle
    // it's turned by, in radians.
    const std::vector<Vec2> shape = {
        { -45, -18 }, { 15, -18 }, { 15, -38 }, { 60, 0 },
        { 15, 38 }, { 15, 18 }, { -45, 18 }
    };
    const Vec2 home = { WINDOW_WIDTH / 2.0f + 200.0f, WINDOW_HEIGHT / 2.0f };
    const float spinSpeed = 0.8f;   // radians a second
    float spin = 0.0f;
    bool orbit = false;             // spin in place, or orbit the pivot

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
                else if (event.key.key == SDLK_SPACE) orbit = !orbit;
            }
        }

        // The mouse's position, read once a frame.
        float mx, my;
        SDL_GetMouseState(&mx, &my);

        // The total angle grows each frame, wrapped so it stays small.
        spin = wrapAngle(spin + spinSpeed * dt);

        // The pivot: the shape's own center, or the mouse when orbiting.
        Vec2 pivot = orbit ? Vec2{ mx, my } : home;

        // The shape, rebuilt from its own points each frame by the total
        // angle. Spinning, each point is rotated, then moved home. Orbiting,
        // each point is turned about the pivot, from where it sits at home.
        std::vector<Vec2> world;
        for (const Vec2& p : shape)
        {
            if (orbit) world.push_back(rotateAbout(home + p, pivot, spin));
            else world.push_back(home + p.rotated(spin));
        }
        Vec2 center = rotateAbout(home, pivot, spin);

        // Where the shape's own axes have gone: its x axis to (cos, sin),
        // and its y axis to a quarter turn of that.
        Vec2 xAxis = Vec2{ 1.0f, 0.0f }.rotated(spin);
        Vec2 yAxis = Vec2{ 0.0f, 1.0f }.rotated(spin);

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);
        drawGrid(renderer);

        if (orbit)
        {
            // The circle the shape's center travels, and the line from the
            // pivot to the center.
            drawCircleOutline(renderer, pivot, distance(home, pivot), FAINT);
            SDL_SetRenderDrawColor(renderer, GREY.r, GREY.g, GREY.b, GREY.a);
            SDL_RenderLine(renderer, pivot.x, pivot.y, center.x, center.y);
        }
        else
        {
            // A faint copy of the shape that doesn't turn, to compare with.
            std::vector<Vec2> still;
            for (const Vec2& p : shape) still.push_back(home + p);
            drawPolygon(renderer, still, FAINT);
        }

        drawPolygon(renderer, world, BLUE);
        drawArrow(renderer, center, center + xAxis * 40.0f, AMBER);
        drawArrow(renderer, center, center + yAxis * 40.0f, GREEN);
        SDL_SetRenderDrawColor(renderer, TEXT.r, TEXT.g, TEXT.b, TEXT.a);
        fillCircle(renderer, pivot, 5);

        std::string mode = orbit ? "orbiting the pivot, at the mouse"
                                 : "spinning about its own center";
        long degrees = std::lround(spin * 180.0f / PI);
        drawText(renderer, font, "mode: " + mode, 12, 10, TEXT);
        drawText(renderer, font,
                 "rotation: " + std::to_string(degrees) + " deg", 12, 30, TEXT);
        drawText(renderer, font,
                 "x axis -> (" + fmt2(xAxis.x) + ", " + fmt2(xAxis.y) + ")",
                 12, 50, AMBER);
        drawText(renderer, font,
                 "y axis -> (" + fmt2(yAxis.x) + ", " + fmt2(yAxis.y) + ")",
                 12, 70, GREEN);
        drawText(renderer, font,
                 "[Space] spin or orbit   [mouse] move the pivot   [Esc] quit",
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
