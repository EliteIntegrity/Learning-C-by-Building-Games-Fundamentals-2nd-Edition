// Which Side - the Chapter 4 demo (the 2D cross product) for
// Learning C++ by Building Games: Fundamentals
//
// Drag A and B to swing the line from A to B. Each point is colored by the
// sign of the cross product (B - A) x (P - A): green when it's right of the
// line, red when it's left, and grey within 3 pixels of it. The mouse is a
// probe, P. The readout gives its cross product, its distance from the
// line, and the area and winding of the triangle A, B, P.

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
constexpr SDL_Color GREEN = { 47, 158, 68, 255 };
constexpr SDL_Color RED = { 224, 49, 49, 255 };

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

// A cross product's color: green right of the line, red left of it, and
// grey within the band that counts as on it.
SDL_Color sideColor(float cross, float band)
{
    if (cross > band) return GREEN;
    if (cross < -band) return RED;
    return GREY;
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
        SDL_CreateWindow("Which Side", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The line runs from A to B, and either end can be dragged.
    Vec2 A = { 300.0f, 430.0f };
    Vec2 B = { 720.0f, 300.0f };
    int grabbed = -1;   // -1 for neither, 0 for A, 1 for B

    std::vector<Vec2> points = {
        { 250, 200 }, { 420, 250 }, { 560, 180 }, { 700, 470 }, { 820, 250 },
        { 500, 480 }, { 350, 560 }, { 640, 560 }, { 480, 340 }, { 760, 380 },
        { 300, 300 }, { 600, 620 }
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
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                     event.button.button == SDL_BUTTON_LEFT)
            {
                // A press within 16 pixels of an end grabs it.
                Vec2 at = { event.button.x, event.button.y };
                if (distance(at, A) < 16.0f) grabbed = 0;
                else if (distance(at, B) < 16.0f) grabbed = 1;
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
            {
                grabbed = -1;
            }
        }

        // The mouse's position, read once a frame.
        float mx, my;
        SDL_GetMouseState(&mx, &my);
        Vec2 mouse = { mx, my };
        if (grabbed == 0) A = mouse;
        if (grabbed == 1) B = mouse;

        // A point is on the line when it's within 3 pixels of it. The cross
        // product is the line's length times the distance, so the cross
        // product at 3 pixels is the line's length times 3.
        float lineLength = distance(A, B);
        float onLine = lineLength * 3.0f;

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);
        drawGrid(renderer);

        // Each point, colored by which side of the line it's on.
        for (const Vec2& p : points)
        {
            SDL_Color c = sideColor((B - A).cross(p - A), onLine);
            SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
            fillCircle(renderer, p, 7);
        }

        // The mouse is a probe, P: the triangle A, B, P, in its side's color.
        float probe = (B - A).cross(mouse - A);
        SDL_Color pc = sideColor(probe, onLine);
        SDL_SetRenderDrawColor(renderer, pc.r, pc.g, pc.b, pc.a);
        SDL_RenderLine(renderer, A.x, A.y, mouse.x, mouse.y);
        SDL_RenderLine(renderer, B.x, B.y, mouse.x, mouse.y);
        fillCircle(renderer, mouse, 3);
        drawText(renderer, font, "P", mouse.x + 10, mouse.y - 8, pc);

        // The line from A to B, and its ends, drawn on top. The arrow stops
        // short of B, so that B's dot doesn't hide its head.
        drawArrow(renderer, A, B - (B - A).normalized() * 9.0f, BLUE);
        SDL_SetRenderDrawColor(renderer, BLUE.r, BLUE.g, BLUE.b, BLUE.a);
        fillCircle(renderer, A, 7);
        fillCircle(renderer, B, 7);
        drawText(renderer, font, "A", A.x - 20, A.y - 8, BLUE);
        drawText(renderer, font, "B", B.x + 10, B.y - 8, BLUE);

        // The probe's side, its distance from the line (the cross product
        // over the line's length), and its triangle: half the cross product
        // is the area, and the sign is the winding, clockwise on the
        // screen when it's positive.
        float away = (lineLength > 0.0f) ? std::abs(probe) / lineLength : 0.0f;
        std::string side = (probe > onLine) ? "right of the line"
                         : (probe < -onLine) ? "left of the line"
                         : "on the line";
        std::string winding = (probe > onLine) ? "clockwise"
                            : (probe < -onLine) ? "counterclockwise"
                            : "flat";
        std::string probeText = "the mouse, P: cross " +
                                std::to_string(std::lround(probe)) +
                                ", so P is " + side + ", " +
                                std::to_string(std::lround(away)) +
                                " px from it";
        long area = std::lround(std::abs(probe) / 2);
        std::string triangle = "triangle A, B, P: area " +
                               std::to_string(area) + " square px, winding " +
                               winding;
        drawText(renderer, font,
                 "the line from A to B: for a point P, "
                 "cross = (B - A) x (P - A)", 12, 10, TEXT);
        drawText(renderer, font, probeText, 12, 30, pc);
        drawText(renderer, font, triangle, 12, 50, TEXT);
        drawText(renderer, font, "[drag] move A or B   [Esc] quit",
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
