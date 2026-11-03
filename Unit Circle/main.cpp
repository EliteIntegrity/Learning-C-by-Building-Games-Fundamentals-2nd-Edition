// Unit Circle - the Chapter 5 demo (angles and trigonometry) for
// Learning C++ by Building Games: Fundamentals
//
// A point sweeps around a circle. Its offset across from the center is the
// cosine of its angle, and its offset down is the sine. A grey needle points
// from the center toward the mouse, at the angle atan2 gives, and an arc
// shows the shortest turn from the point's angle to the needle's. Space
// pauses the sweep.

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
        SDL_CreateWindow("Unit Circle", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The circle, and the point that sweeps around it: its angle, t, in
    // radians, and how fast that grows.
    const Vec2 C = { WINDOW_WIDTH / 2.0f, WINDOW_HEIGHT / 2.0f };
    const float R = 220.0f;
    const float sweepSpeed = 0.6f;   // radians a second
    float t = 0.0f;
    bool playing = true;

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
                else if (event.key.key == SDLK_SPACE) playing = !playing;
            }
        }

        // The mouse's position, read once a frame.
        float mx, my;
        SDL_GetMouseState(&mx, &my);

        // Sweep the point. Wrapping keeps t between -PI and PI, so it never
        // grows large enough to lose its precision.
        if (playing) t = wrapAngle(t + sweepSpeed * dt);

        // The swept point: dir.x is cos(t), and dir.y is sin(t). The foot
        // is level with the center, straight below or above the point.
        Vec2 dir = Vec2::fromAngle(t);
        Vec2 P = C + dir * R;
        Vec2 foot = { P.x, C.y };

        // The needle's angle, from the center toward the mouse, by atan2.
        float needle = (Vec2{ mx, my } - C).angle();

        // The shortest turn from the point's angle to the needle's: their
        // difference, wrapped. Within a degree, the two line up.
        float turn = wrapAngle(needle - t);
        bool lined = nearlyEqual(turn, 0.0f, PI / 180.0f);

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);
        drawGrid(renderer);

        // The axes through the center, and the circle.
        SDL_SetRenderDrawColor(renderer, 70, 78, 90, 255);
        SDL_RenderLine(renderer, C.x - R - 30, C.y, C.x + R + 30, C.y);
        SDL_RenderLine(renderer, C.x, C.y - R - 30, C.x, C.y + R + 30);
        drawCircleOutline(renderer, C, R, { 120, 128, 140, 255 });

        // The shortest turn, as an arc inside the circle, from the point's
        // angle to the needle's, in 40 short steps.
        SDL_SetRenderDrawColor(renderer, TEXT.r, TEXT.g, TEXT.b, TEXT.a);
        Vec2 from = C + Vec2::fromAngle(t, R * 0.35f);
        for (int i = 1; i <= 40; ++i)
        {
            float a = t + turn * (float)i / 40.0f;
            Vec2 to = C + Vec2::fromAngle(a, R * 0.35f);
            SDL_RenderLine(renderer, from.x, from.y, to.x, to.y);
            from = to;
        }

        // The cosine leg, across, and the sine leg, down or up. With the
        // radius, they make a right-angled triangle inside the circle.
        SDL_SetRenderDrawColor(renderer, AMBER.r, AMBER.g, AMBER.b, AMBER.a);
        SDL_RenderLine(renderer, C.x, C.y, foot.x, foot.y);
        SDL_SetRenderDrawColor(renderer, GREEN.r, GREEN.g, GREEN.b, GREEN.a);
        SDL_RenderLine(renderer, foot.x, foot.y, P.x, P.y);
        drawArrow(renderer, C, P, BLUE);
        SDL_SetRenderDrawColor(renderer, BLUE.r, BLUE.g, BLUE.b, BLUE.a);
        fillCircle(renderer, P, 6);

        // The needle, green when it lines up with the point.
        SDL_Color nc = lined ? GREEN : GREY;
        Vec2 tip = C + Vec2::fromAngle(needle, R);
        SDL_SetRenderDrawColor(renderer, nc.r, nc.g, nc.b, nc.a);
        SDL_RenderLine(renderer, C.x, C.y, tip.x, tip.y);

        Vec2 cosAt = { (C.x + foot.x) / 2 - 12, C.y + 6 };
        Vec2 sinAt = { foot.x + 6, (foot.y + P.y) / 2 - 8 };
        drawText(renderer, font, "cos", cosAt.x, cosAt.y, AMBER);
        drawText(renderer, font, "sin", sinAt.x, sinAt.y, GREEN);

        // The readout, in degrees for people. tan is sin over cos, which
        // has no value where cos is 0.
        long degrees = std::lround(t * 180.0f / PI);
        std::string tanText = (std::fabs(dir.x) < 0.01f) ? "none"
                                                          : fmt2(dir.y / dir.x);
        long needleDegrees = std::lround(needle * 180.0f / PI);
        long turnDegrees = std::lround(turn * 180.0f / PI);
        std::string way = lined ? "lined up"
                        : (turn > 0.0f) ? "clockwise" : "counterclockwise";
        drawText(renderer, font,
                 "angle t: " + std::to_string(degrees) + " deg = " + fmt2(t) +
                 " rad", 12, 10, TEXT);
        drawText(renderer, font,
                 "cos t = " + fmt2(dir.x) + "   sin t = " + fmt2(dir.y) +
                 "   tan t = " + tanText, 12, 30, TEXT);
        drawText(renderer, font,
                 "needle, by atan2: " + std::to_string(needleDegrees) + " deg",
                 12, 50, nc);
        drawText(renderer, font,
                 "shortest turn to the needle: " +
                 std::to_string(turnDegrees) + " deg, " + way, 12, 70, TEXT);
        drawText(renderer, font,
                 "[Space] pause or play   [mouse] aim the needle   [Esc] quit",
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
