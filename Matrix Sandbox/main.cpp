// Matrix Sandbox - the Chapter 26 demo (matrices for 2D) for
// Learning C++ by Building Games: Fundamentals
//
// One matrix, M = T * R * S, takes a square from its own space onto the
// window. Drag the square to move it, hold Left and Right to turn it, Up
// and Down to scale it, and W and S to stretch its height, and press F to
// flip it. The readout shows M and its inverse as they change, with M's
// first two columns in the colors of the axes they draw. A right-click
// drops a probe, which the inverse takes into the square's own space.

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <cmath>
#include <string>
#include "Vec2.h"
#include "Mat3.h"

constexpr int WINDOW_WIDTH  = 1000;
constexpr int WINDOW_HEIGHT = 700;

constexpr SDL_Color TEXT = { 235, 238, 242, 255 };
constexpr SDL_Color GREY = { 90, 100, 112, 255 };
constexpr SDL_Color RED = { 235, 80, 80, 255 };
constexpr SDL_Color GREEN = { 60, 190, 100, 255 };
constexpr SDL_Color OUTLINE = { 120, 160, 230, 255 };

// How fast the held keys turn the square, in radians a second, and scale
// it, in pixels a unit, a second; the smallest and largest scales; and the
// readout's left edge.
constexpr float TURN_SPEED = 1.6f;
constexpr float SCALE_SPEED = 45.0f;
constexpr float MIN_SCALE = 12.0f;
constexpr float MAX_SCALE = 220.0f;
constexpr float PANEL_X = 610.0f;

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

// A number as text, rounded to the decimals asked for, and right-aligned
// in 9 characters, so that a matrix's columns line up. Rounding first
// keeps a tiny negative number from showing as -0.00.
std::string padded(float v, int decimals)
{
    float shift = std::pow(10.0f, (float)decimals);
    v = std::round(v * shift) / shift + 0.0f;
    char buf[32];
    SDL_snprintf(buf, sizeof(buf), "%9.*f", decimals, v);
    return buf;
}

// Whether a point in the square's own space is inside the square, which
// runs from -1 to 1 each way.
bool inSquare(Vec2 local)
{
    return std::fabs(local.x) <= 1.0f && std::fabs(local.y) <= 1.0f;
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
        SDL_CreateWindow("Matrix Sandbox", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The square's corners, in its own space, and where R puts it back.
    const Vec2 CORNER[4] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    const Vec2 HOME = { 380.0f, 360.0f };

    // The matrix's parts: the move, the turn in radians, the scale across
    // and down, in pixels for each of the square's units, and the flip.
    // Then the drag, and the probe.
    Vec2 pos = HOME;
    float rot = 0.0f;
    float sx = 70.0f;
    float sy = 70.0f;
    bool flipped = false;
    bool dragging = false;
    Vec2 grab;
    Vec2 probe;
    bool hasProbe = false;

    // The matrix and its inverse, built each frame and kept to the next,
    // so that a click is tested against the square the window shows.
    Mat3 M;
    Mat3 Minv;

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
                else if (event.key.key == SDLK_F) flipped = !flipped;
                else if (event.key.key == SDLK_R)
                {
                    pos = HOME;
                    rot = 0.0f;
                    sx = 70.0f;
                    sy = 70.0f;
                    flipped = false;
                    hasProbe = false;
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            {
                // A left press inside the square grabs it, tested in the
                // square's own space, through the inverse. A right press
                // drops the probe.
                Vec2 press = { event.button.x, event.button.y };
                if (event.button.button == SDL_BUTTON_LEFT &&
                    inSquare(Minv.apply(press)))
                {
                    dragging = true;
                    grab = pos - press;
                }
                else if (event.button.button == SDL_BUTTON_RIGHT)
                {
                    probe = press;
                    hasProbe = true;
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) dragging = false;
        }

        // The mouse's position, read once a frame.
        float mx, my;
        SDL_GetMouseState(&mx, &my);

        // A dragged square follows the mouse, with the point grabbed under it.
        if (dragging) pos = Vec2{ mx, my } + grab;

        // Keys held down turn the square, scale it, and stretch its height.
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_LEFT]) rot = wrapAngle(rot - TURN_SPEED * dt);
        if (keys[SDL_SCANCODE_RIGHT]) rot = wrapAngle(rot + TURN_SPEED * dt);
        float grow = 0.0f;
        if (keys[SDL_SCANCODE_UP]) grow += SCALE_SPEED * dt;
        if (keys[SDL_SCANCODE_DOWN]) grow -= SCALE_SPEED * dt;
        float stretch = 0.0f;
        if (keys[SDL_SCANCODE_W]) stretch += SCALE_SPEED * dt;
        if (keys[SDL_SCANCODE_S]) stretch -= SCALE_SPEED * dt;
        sx = SDL_clamp(sx + grow, MIN_SCALE, MAX_SCALE);
        sy = SDL_clamp(sy + grow + stretch, MIN_SCALE, MAX_SCALE);

        // The matrix, from its parts: scale, then turn, then move, read from
        // the right. A flip is a scale of -1 across. Then its inverse.
        M = Mat3::translation(pos.x, pos.y) * Mat3::rotation(rot) *
            Mat3::scale(flipped ? -sx : sx, sy);
        Minv = inverse(M);

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);
        drawGrid(renderer);

        // The square: its corners, taken onto the window by M, filled as two
        // triangles with no texture, then outlined.
        SDL_Vertex verts[4];
        for (int i = 0; i < 4; ++i)
        {
            Vec2 p = M.apply(CORNER[i]);
            verts[i].position = { p.x, p.y };
            verts[i].color = { 0.13f, 0.24f, 0.42f, 1.0f };
            verts[i].tex_coord = { 0.0f, 0.0f };
        }
        const int indices[6] = { 0, 1, 2, 0, 2, 3 };
        SDL_RenderGeometry(renderer, nullptr, verts, 4, indices, 6);
        setColor(renderer, OUTLINE);
        for (int i = 0; i < 4; ++i)
        {
            Vec2 a = M.apply(CORNER[i]);
            Vec2 b = M.apply(CORNER[(i + 1) % 4]);
            SDL_RenderLine(renderer, a.x, a.y, b.x, b.y);
        }

        // The square's own axes, from its center to where M sends the points
        // (1, 0) and (0, 1), and a marker at (0.8, 0.8), which shows a flip.
        Vec2 center = M.apply({ 0.0f, 0.0f });
        drawArrow(renderer, center, M.apply({ 1.0f, 0.0f }), RED);
        drawArrow(renderer, center, M.apply({ 0.0f, 1.0f }), GREEN);
        Vec2 mark = M.apply({ 0.8f, 0.8f });
        setColor(renderer, TEXT);
        SDL_FRect markRect = { mark.x - 4, mark.y - 4, 8, 8 };
        SDL_RenderFillRect(renderer, &markRect);

        // The probe, a fixed point in the window, taken into the square's
        // space by the inverse: green when it's inside the square there.
        Vec2 local = Minv.apply(probe);
        if (hasProbe)
        {
            setColor(renderer, inSquare(local) ? GREEN : RED);
            SDL_FRect dot = { probe.x - 5, probe.y - 5, 10, 10 };
            SDL_RenderFillRect(renderer, &dot);
        }

        // The readout. First M, with its first column in red and its second
        // in green, the colors of the axes they draw, and the move in the
        // third. Then the inverse, to four decimals, since its numbers are
        // small. Then the determinant, the parts, and the probe.
        drawText(renderer, font, "M = T * R * S, square to window",
                 PANEL_X, 40, TEXT);
        drawText(renderer, font, "   x axis", PANEL_X, 62, RED);
        drawText(renderer, font, "   y axis", PANEL_X + 90, 62, GREEN);
        drawText(renderer, font, "     move", PANEL_X + 180, 62, TEXT);
        for (int i = 0; i < 3; ++i)
        {
            float y = 84.0f + i * 22.0f;
            drawText(renderer, font, padded(M.m[i][0], 2), PANEL_X, y, RED);
            drawText(renderer, font, padded(M.m[i][1], 2), PANEL_X + 90, y,
                     GREEN);
            drawText(renderer, font, padded(M.m[i][2], 2), PANEL_X + 180, y,
                     TEXT);
        }
        drawText(renderer, font, "the inverse, window to square",
                 PANEL_X, 162, TEXT);
        for (int i = 0; i < 3; ++i)
        {
            float y = 186.0f + i * 22.0f;
            for (int j = 0; j < 3; ++j)
                drawText(renderer, font, padded(Minv.m[i][j], 4),
                         PANEL_X + j * 90.0f, y, TEXT);
        }
        float det = determinant(M);
        drawText(renderer, font,
                 "determinant " + fmt2(det) + (det < 0.0f ? ", flipped" : ""),
                 PANEL_X, 270, TEXT);
        drawText(renderer, font,
                 "turn " + std::to_string((int)std::lround(rot * 180.0f / PI)) +
                 " degrees", PANEL_X, 292, TEXT);
        drawText(renderer, font,
                 "scale " + fmt2(flipped ? -sx : sx) + " across, " +
                 fmt2(sy) + " down",
                 PANEL_X, 314, TEXT);
        drawText(renderer, font, "the probe, in the square's space:",
                 PANEL_X, 350, TEXT);
        std::string where = "right-click to drop one";
        if (hasProbe)
            where = "(" + fmt2(local.x) + ", " + fmt2(local.y) + "), " +
                    (inSquare(local) ? "inside" : "outside");
        drawText(renderer, font, where, PANEL_X, 372,
                 hasProbe ? (inSquare(local) ? GREEN : RED) : GREY);
        drawText(renderer, font,
                 "[Drag] move   [Left] [Right] turn   [Up] [Down] scale   "
                 "[W] [S] height", 12, WINDOW_HEIGHT - 46, GREY);
        drawText(renderer, font,
                 "[F] flip   [Right-click] probe   [R] reset   [Esc] quit",
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
