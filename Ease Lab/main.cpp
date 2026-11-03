// Ease Lab - the Chapter 8 demo (interpolation and easing) for
// Learning C++ by Building Games: Fundamentals
//
// One fraction, t, runs from 0 to 1 and back, and drives five markers along
// their tracks, each through a different easing curve. The graph on the
// right plots the five curves, with a line at the current t. Below the
// tracks, the same t blends two colors, and turns two needles from 300
// degrees to 60: one the long way round, and one the short way. Hold the
// left mouse button to set t from the mouse, and press Space to pause.

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
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
constexpr SDL_Color RED = { 224, 49, 49, 255 };
constexpr SDL_Color VIOLET = { 150, 102, 230, 255 };
constexpr SDL_Color FAINT = { 70, 78, 90, 255 };

// Where things go: the tracks on the left, and the graph on the right.
constexpr float TRACK_LEFT = 160.0f;
constexpr float TRACK_RIGHT = 480.0f;
constexpr float GRAPH_LEFT = 580.0f;
constexpr float GRAPH_RIGHT = 940.0f;
constexpr float GRAPH_TOP = 100.0f;
constexpr float GRAPH_BOTTOM = 460.0f;

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

// Set the color the renderer draws with, from an SDL_Color.
void setColor(SDL_Renderer* renderer, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

// The easing curves. Each takes a t from 0 to 1, and gives back an eased
// t, also from 0 to 1: the same two ends, by a different path.
float easeLinear(float t) { return t; }

// Slow to start, since t squared is tiny while t is small.
float easeIn(float t) { return t * t; }

// Slow to finish: ease-in with the time run backward, turned over.
float easeOut(float t) { return 1.0f - easeIn(1.0f - t); }

// Slow at both ends: the cubic 3t^2 - 2t^3, with t^2 taken out.
float smoothstep(float t) { return t * t * (3.0f - 2.0f * t); }

// Ease in for the first half, and out for the second. Each half is the
// cubic ease-in, u * u * u, squeezed into half the time and half the
// height.
float easeInOut(float t)
{
    if (t < 0.5f)
    {
        float u = 2.0f * t;            // the time so far, doubled
        return u * u * u / 2.0f;
    }
    float u = 2.0f * (1.0f - t);       // the time left, doubled
    return 1.0f - u * u * u / 2.0f;
}

// An easing curve, with the name and the color the demo shows it in. Its
// fn is a pointer to the curve's function, so that a loop can run every
// curve the same way.
struct Easing
{
    const char* name;
    SDL_Color color;
    float (*fn)(float);
};

constexpr Easing EASINGS[] = {
    { "linear", TEXT, easeLinear },
    { "ease in", BLUE, easeIn },
    { "ease out", GREEN, easeOut },
    { "smoothstep", AMBER, smoothstep },
    { "ease in-out", VIOLET, easeInOut },
};

// A color a fraction t of the way from a to b: each channel lerped, then
// rounded to the nearest whole number, since a cast would round down.
SDL_Color lerpColor(SDL_Color a, SDL_Color b, float t)
{
    return { (Uint8)std::lround(lerp(a.r, b.r, t)),
             (Uint8)std::lround(lerp(a.g, b.g, t)),
             (Uint8)std::lround(lerp(a.b, b.b, t)),
             (Uint8)std::lround(lerp(a.a, b.a, t)) };
}

// A point on the graph, for a t and an eased value: t across the graph,
// and the value up it. Each is a lerp, and the second runs from the
// bottom to the top, since y grows down the screen.
Vec2 graphPoint(float t, float value)
{
    return { lerp(GRAPH_LEFT, GRAPH_RIGHT, t),
             lerp(GRAPH_BOTTOM, GRAPH_TOP, value) };
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
        SDL_CreateWindow("Ease Lab", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The clock. The phase runs from 0 to 2 and starts again, and t
    // follows it from 0 to 1 and back, so the markers go there and back.
    float phase = 0.0f;
    bool playing = true;

    // The dial for the angle row, and the two angles its needles turn
    // between, in radians.
    const Vec2 dial = { 320.0f, 580.0f };
    const float dialRadius = 70.0f;
    const float startAngle = 300.0f * PI / 180.0f;
    const float endAngle = 60.0f * PI / 180.0f;

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
        SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mx, &my);

        // The clock moves on while it plays. A held left button sets the
        // phase from the mouse instead: the mouse's fraction of the way
        // across the graph, clamped, since the mouse can go past its ends.
        if (buttons & SDL_BUTTON_LMASK)
        {
            float u = inverseLerp(GRAPH_LEFT, GRAPH_RIGHT, mx);
            phase = std::clamp(u, 0.0f, 1.0f);
        }
        else if (playing)
        {
            phase += 0.5f * dt;
            if (phase >= 2.0f) phase -= 2.0f;
        }
        float t = (phase < 1.0f) ? phase : 2.0f - phase;

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);

        // The tracks. Every marker gets the same t, through its own curve,
        // and the eased value says how far along its track it is.
        float y = 100.0f;
        for (const Easing& e : EASINGS)
        {
            Vec2 start = { TRACK_LEFT, y };
            Vec2 end = { TRACK_RIGHT, y };
            float eased = e.fn(t);
            setColor(renderer, FAINT);
            SDL_RenderLine(renderer, start.x, start.y, end.x, end.y);
            setColor(renderer, e.color);
            fillCircle(renderer, lerp(start, end, eased), 9.0f);
            drawText(renderer, font, e.name, 12, y - 10, e.color);
            drawText(renderer, font, fmt2(eased), 500, y - 10, GREY);
            y += 70.0f;
        }

        // The color row: the same t blends green into red, channel by
        // channel.
        setColor(renderer, lerpColor(GREEN, RED, t));
        SDL_FRect bar = { TRACK_LEFT, 438.0f, TRACK_RIGHT - TRACK_LEFT, 24.0f };
        SDL_RenderFillRect(renderer, &bar);
        drawText(renderer, font, "color", 12, 440, TEXT);

        // The angle row. Lerping the angles' numbers turns the amber needle
        // back through 180 degrees, the long way. Lerping by the shortest
        // turn takes the green needle on through 0, the short way.
        float longWay = lerp(startAngle, endAngle, t);
        float shortWay = lerpAngle(startAngle, endAngle, t);
        Vec2 longTip = dial + Vec2::fromAngle(longWay, dialRadius);
        Vec2 shortTip = dial + Vec2::fromAngle(shortWay, dialRadius);
        drawCircleOutline(renderer, dial, dialRadius, FAINT);
        drawArrow(renderer, dial, longTip, AMBER);
        drawArrow(renderer, dial, shortTip, GREEN);
        drawText(renderer, font, "angle", 12, 570, TEXT);
        drawText(renderer, font, "lerp: the long way", 420, 552, AMBER);
        drawText(renderer, font, "lerpAngle: the short way", 420, 580, GREEN);

        // The graph: a frame, then each curve as 60 short lines, from one
        // point on it to the next.
        setColor(renderer, FAINT);
        SDL_FRect frame = { GRAPH_LEFT, GRAPH_TOP, GRAPH_RIGHT - GRAPH_LEFT,
                            GRAPH_BOTTOM - GRAPH_TOP };
        SDL_RenderRect(renderer, &frame);
        for (const Easing& e : EASINGS)
        {
            setColor(renderer, e.color);
            Vec2 prev = graphPoint(0.0f, e.fn(0.0f));
            for (int i = 1; i <= 60; ++i)
            {
                float u = i / 60.0f;
                Vec2 p = graphPoint(u, e.fn(u));
                SDL_RenderLine(renderer, prev.x, prev.y, p.x, p.y);
                prev = p;
            }
        }

        // The current t: a line up the graph, and a dot where it crosses
        // each curve, at that curve's eased value.
        Vec2 low = graphPoint(t, 0.0f);
        Vec2 high = graphPoint(t, 1.0f);
        setColor(renderer, GREY);
        SDL_RenderLine(renderer, low.x, low.y, high.x, high.y);
        for (const Easing& e : EASINGS)
        {
            setColor(renderer, e.color);
            fillCircle(renderer, graphPoint(t, e.fn(t)), 5.0f);
        }
        drawText(renderer, font, "eased", GRAPH_LEFT, GRAPH_TOP - 26, GREY);
        drawText(renderer, font, "t", GRAPH_RIGHT + 8, GRAPH_BOTTOM - 10, GREY);

        std::string readout = "t = " + fmt2(t);
        if (!playing) readout += "   paused";
        drawText(renderer, font, readout, 12, 10, TEXT);
        drawText(renderer, font,
                 "[Hold left button] set t   [Space] pause   [Esc] quit",
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
