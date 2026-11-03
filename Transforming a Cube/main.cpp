// Transforming a Cube - the Chapter 29 demo (3D transforms and 4 by 4
// matrices) for Learning C++ by Building Games: Fundamentals
//
// A wireframe cube placed by one model matrix, M = T * R * S, with its own
// axes drawn from the columns of its turn, R = Rz * Ry * Rx. Drag to turn
// it about x and y, and Z and X turn it about z. Up and Down scale it,
// WASD moves it in x and y, and Q and E move it in z, which the view drops.
// O swaps the order to R * T * S, so that it orbits, and G sets the turn
// about y to 90 degrees, where x and z turn it about the same axis.

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <cmath>
#include <string>
#include "Vec2.h"
#include "Vec3.h"
#include "Mat4.h"

constexpr int WINDOW_WIDTH  = 1000;
constexpr int WINDOW_HEIGHT = 700;

constexpr SDL_Color TEXT = { 235, 238, 242, 255 };
constexpr SDL_Color GREY = { 90, 100, 112, 255 };
constexpr SDL_Color RED = { 235, 80, 80, 255 };
constexpr SDL_Color GREEN = { 60, 190, 100, 255 };
constexpr SDL_Color BLUE = { 90, 150, 240, 255 };
constexpr SDL_Color AMBER = { 240, 200, 60, 255 };

// The view: pixels for each unit of the world, and where the world's
// origin is in the window. Then how fast things change, a second: the spin,
// the turn about z, the move and the scale; how far a drag turns, a pixel;
// and the scale's limits.
constexpr float SCALE = 110.0f;
constexpr float CX = 380.0f;
constexpr float CY = 360.0f;
constexpr float SPIN_X = 0.25f;
constexpr float SPIN_Y = 0.6f;
constexpr float TURN_SPEED = 1.2f;
constexpr float MOVE_SPEED = 2.0f;
constexpr float SCALE_SPEED = 0.8f;
constexpr float DRAG_TURN = 0.01f;
constexpr float MIN_SCALE = 0.3f;
constexpr float MAX_SCALE = 2.2f;

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

// A point in the world, on the window: an orthographic projection, which
// drops z. The world's y points up and the window's down, so y is negated.
Vec2 project(Vec3 p)
{
    return { CX + p.x * SCALE, CY - p.y * SCALE };
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
        SDL_CreateWindow("Transforming a Cube", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The cube in its own space: eight corners at every mix of -1 and 1,
    // and its twelve edges, as pairs of corners: the four of its back face,
    // the four of its front, and the four that join them.
    const Vec3 CORNER[8] = { { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 },
                             { -1, 1, -1 }, { -1, -1, 1 }, { 1, -1, 1 },
                             { 1, 1, 1 }, { -1, 1, 1 } };
    const int EDGE[12][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
                              { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
                              { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };

    // The cube's place, its three turns in radians, and its scale; whether
    // it spins, whether its matrix is built in the orbit's order, and
    // whether it's being dragged.
    Vec3 pos = { 0.0f, 0.0f, 0.0f };
    float rx = 0.5f;
    float ry = 0.6f;
    float rz = 0.0f;
    float s = 1.0f;
    bool spin = true;
    bool orbit = false;
    bool dragging = false;

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

        // Events: quitting, and the keys and clicks that happen once. A drag
        // turns the cube about y as the mouse moves across, and about x as it
        // moves down.
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT) running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN)
            {
                if (event.key.key == SDLK_ESCAPE) running = false;
                else if (event.key.key == SDLK_SPACE) spin = !spin;
                else if (event.key.key == SDLK_O) orbit = !orbit;
                else if (event.key.key == SDLK_G)
                {
                    rx = 0.0f;
                    ry = PI / 2.0f;
                    rz = 0.0f;
                    spin = false;
                }
                else if (event.key.key == SDLK_R)
                {
                    pos = { 0.0f, 0.0f, 0.0f };
                    rx = 0.5f;
                    ry = 0.6f;
                    rz = 0.0f;
                    s = 1.0f;
                    spin = true;
                    orbit = false;
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                     event.button.button == SDL_BUTTON_LEFT)
            {
                dragging = true;
                spin = false;
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) dragging = false;
            else if (event.type == SDL_EVENT_MOUSE_MOTION && dragging)
            {
                ry += event.motion.xrel * DRAG_TURN;
                rx += event.motion.yrel * DRAG_TURN;
            }
        }

        // Keys held down scale the cube, move it, and turn it about z, and it
        // spins while it's left alone. The turns are wrapped, so that the
        // readout stays between -180 and 180 degrees.
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_UP]) s += SCALE_SPEED * dt;
        if (keys[SDL_SCANCODE_DOWN]) s -= SCALE_SPEED * dt;
        s = SDL_clamp(s, MIN_SCALE, MAX_SCALE);
        if (keys[SDL_SCANCODE_A]) pos.x -= MOVE_SPEED * dt;
        if (keys[SDL_SCANCODE_D]) pos.x += MOVE_SPEED * dt;
        if (keys[SDL_SCANCODE_W]) pos.y += MOVE_SPEED * dt;
        if (keys[SDL_SCANCODE_S]) pos.y -= MOVE_SPEED * dt;
        if (keys[SDL_SCANCODE_Q]) pos.z -= MOVE_SPEED * dt;
        if (keys[SDL_SCANCODE_E]) pos.z += MOVE_SPEED * dt;
        if (keys[SDL_SCANCODE_Z]) rz -= TURN_SPEED * dt;
        if (keys[SDL_SCANCODE_X]) rz += TURN_SPEED * dt;
        if (spin)
        {
            ry += SPIN_Y * dt;
            rx += SPIN_X * dt;
        }
        rx = wrapAngle(rx);
        ry = wrapAngle(ry);
        rz = wrapAngle(rz);

        // The model matrix, from its parts. T * R * S turns the cube in
        // place; R * T * S turns its position too, so that it orbits.
        Mat4 R = Mat4::rotationZ(rz) * Mat4::rotationY(ry) *
                 Mat4::rotationX(rx);
        Mat4 T = Mat4::translation(pos.x, pos.y, pos.z);
        Mat4 S = Mat4::scale(s, s, s);
        Mat4 model = orbit ? (R * T * S) : (T * R * S);

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);

        // The twelve edges, each corner taken into the world by the model
        // matrix. An edge nearer than the cube's center is brighter, and one
        // farther is dimmer, which the projection, dropping z, can't show.
        Vec3 center = model.apply({ 0.0f, 0.0f, 0.0f });
        for (int e = 0; e < 12; ++e)
        {
            Vec3 a = model.apply(CORNER[EDGE[e][0]]);
            Vec3 b = model.apply(CORNER[EDGE[e][1]]);
            float depth = ((a.z + b.z) * 0.5f - center.z) / s;
            float shade = SDL_clamp(0.62f + depth * 0.22f, 0.3f, 1.0f);
            SDL_SetRenderDrawColor(renderer, (Uint8)(110 * shade),
                                   (Uint8)(170 * shade), (Uint8)(240 * shade),
                                   255);
            Vec2 pa = project(a);
            Vec2 pb = project(b);
            SDL_RenderLine(renderer, pa.x, pa.y, pb.x, pb.y);
        }

        // The cube's own axes, from its center: the columns of its turn, R,
        // in red, green and blue, as long as the cube is from its center to
        // the middle of a face, and half again.
        Vec2 from = project(center);
        float arm = 1.5f * s;
        drawArrow(renderer, from, project(center + column(R, 0) * arm), RED);
        drawArrow(renderer, from, project(center + column(R, 1) * arm), GREEN);
        drawArrow(renderer, from, project(center + column(R, 2) * arm), BLUE);

        // The readout: the order, then the model matrix, with its first three
        // columns in the colors of the axes they hold, and the move in the
        // fourth. Then the turns, the place and the scale, and a note when
        // the turn about y is a quarter turn either way.
        drawText(renderer, font,
                 orbit ? "M = R * T * S: orbit" : "M = T * R * S: in place",
                 640, 40, TEXT);
        const SDL_Color COLUMN[4] = { RED, GREEN, BLUE, TEXT };
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                drawText(renderer, font, padded(model.m[i][j], 2),
                         640 + j * 86.0f, 64 + i * 22.0f, COLUMN[j]);
        drawText(renderer, font,
                 "turns rx " + std::to_string((int)std::lround(rx * 180 / PI)) +
                 ", ry " + std::to_string((int)std::lround(ry * 180 / PI)) +
                 ", rz " + std::to_string((int)std::lround(rz * 180 / PI)) +
                 " degrees", 640, 162, TEXT);
        drawText(renderer, font,
                 "place (" + fmt2(pos.x) + ", " + fmt2(pos.y) + ", " +
                 fmt2(pos.z) + "), scale " + fmt2(s), 640, 184, TEXT);
        if (std::fabs(std::fabs(ry) - PI / 2.0f) < 0.02f)
            drawText(renderer, font, "ry is 90: rx and rz share an axis",
                     640, 216, AMBER);
        drawText(renderer, font,
                 "[Drag] turn   [Z] [X] turn about z   [Space] spin   "
                 "[Up] [Down] scale", 12, WINDOW_HEIGHT - 46, GREY);
        drawText(renderer, font,
                 "[WASD] [Q] [E] move   [O] order   [G] ry 90   [R] reset   "
                 "[Esc] quit", 12, WINDOW_HEIGHT - 26, GREY);

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
