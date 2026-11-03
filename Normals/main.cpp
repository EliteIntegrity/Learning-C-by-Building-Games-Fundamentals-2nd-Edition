// Normals - the Chapter 28 demo (thinking in 3D: coordinates and vectors)
// for Learning C++ by Building Games: Fundamentals
//
// A triangle in 3D, tumbled with the mouse. Its normal is the cross product
// of two of its edges, drawn as an arrow, and it's lit by Lambert's law: as
// bright as the dot product of its normal and the direction to the light.
// A probe, a point fixed in the world, shows its signed distance from the
// triangle's plane. Drag to tumble, Left and Right move the light, W winds
// the triangle the other way, Space spins, and R resets.

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <cmath>
#include <string>
#include <utility>
#include "Vec2.h"
#include "Vec3.h"

constexpr int WINDOW_WIDTH  = 1000;
constexpr int WINDOW_HEIGHT = 700;

constexpr SDL_Color TEXT = { 235, 238, 242, 255 };
constexpr SDL_Color GREY = { 90, 100, 112, 255 };
constexpr SDL_Color BLUE = { 90, 150, 240, 255 };
constexpr SDL_Color AMBER = { 240, 200, 60, 255 };
constexpr SDL_Color EDGE = { 230, 235, 240, 255 };
constexpr SDL_Color PROBE = { 240, 120, 60, 255 };

// The view: pixels for each unit of the world, and where the world's
// origin is in the window. Then how fast things turn, in radians a second,
// or radians a pixel for a drag.
constexpr float SCALE = 130.0f;
constexpr float CX = 380.0f;
constexpr float CY = 360.0f;
constexpr float SPIN_YAW = 0.7f;
constexpr float SPIN_PITCH = 0.25f;
constexpr float LIGHT_SPEED = 1.5f;
constexpr float DRAG_TURN = 0.01f;

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

// Set the color the renderer draws with, from an SDL_Color.
void setColor(SDL_Renderer* renderer, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

// A turn about the x axis: x stays, and y and z turn as Chapter 6's x and
// y did.
Vec3 rotX(Vec3 p, float a)
{
    float c = std::cos(a);
    float s = std::sin(a);
    return { p.x, p.y * c - p.z * s, p.y * s + p.z * c };
}

// A turn about the y axis: y stays, and z and x turn as Chapter 6's x and
// y did.
Vec3 rotY(Vec3 p, float a)
{
    float c = std::cos(a);
    float s = std::sin(a);
    return { p.x * c + p.z * s, p.y, -p.x * s + p.z * c };
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
        SDL_CreateWindow("Normals", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The triangle, in its own space, in the plane z = 0; the probe, a
    // point fixed in the world; and their starting turn and light.
    const Vec3 TRI[3] = { { -1.2f, -1.0f, 0.0f }, { 1.3f, -0.8f, 0.0f },
                          { 0.1f, 1.3f, 0.0f } };
    const Vec3 PROBE_AT = { 1.6f, 1.0f, 0.5f };
    const float START_YAW = 0.5f;
    const float START_PITCH = -0.4f;
    const float START_LIGHT = 2.2f;

    // How the triangle is turned, and where the light is around the scene;
    // whether it spins, whether it's being dragged, and whether its corners
    // are listed the other way round.
    float yaw = START_YAW;
    float pitch = START_PITCH;
    float lightAngle = START_LIGHT;
    bool spin = true;
    bool dragging = false;
    bool otherWay = false;

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
        // turns the triangle by the mouse's movement.
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT) running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN)
            {
                if (event.key.key == SDLK_ESCAPE) running = false;
                else if (event.key.key == SDLK_SPACE) spin = !spin;
                else if (event.key.key == SDLK_W) otherWay = !otherWay;
                else if (event.key.key == SDLK_R)
                {
                    yaw = START_YAW;
                    pitch = START_PITCH;
                    lightAngle = START_LIGHT;
                    spin = true;
                    otherWay = false;
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
                yaw += event.motion.xrel * DRAG_TURN;
                pitch += event.motion.yrel * DRAG_TURN;
            }
        }

        // Left and Right move the light around the scene, and the triangle
        // spins while it's left alone.
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_LEFT]) lightAngle -= LIGHT_SPEED * dt;
        if (keys[SDL_SCANCODE_RIGHT]) lightAngle += LIGHT_SPEED * dt;
        if (spin)
        {
            yaw += SPIN_YAW * dt;
            pitch += SPIN_PITCH * dt;
        }

        // The corners, turned into the world: about x, then about y. The
        // other way round lists the same corners as a, c, b.
        Vec3 a = rotY(rotX(TRI[0], pitch), yaw);
        Vec3 b = rotY(rotX(TRI[1], pitch), yaw);
        Vec3 c = rotY(rotX(TRI[2], pitch), yaw);
        if (otherWay) std::swap(b, c);

        // The normal: the cross product of two edges, at right angles to
        // both, and so to the triangle. Its length is twice the triangle's
        // area, so it's normalized to a length of 1.
        Vec3 cross = (b - a).cross(c - a);
        Vec3 normal = cross.normalized();
        Vec3 centroid = (a + b + c) / 3.0f;

        // The direction toward the light, and Lambert's law: the brightness
        // is the dot product of the normal and the light, clamped at 0, on
        // top of a little ambient light.
        Vec3 light = Vec3{ std::cos(lightAngle), 0.5f,
                           std::sin(lightAngle) }.normalized();
        float ndotl = normal.dot(light);
        float bright = SDL_max(ndotl, 0.0f) * 0.85f + 0.15f;

        // The probe's signed distance from the triangle's plane, along the
        // normal, and its foot: the point on the plane straight below it.
        float above = normal.dot(PROBE_AT - a);
        Vec3 foot = PROBE_AT - normal * above;

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);

        // The triangle, filled in its shade of teal, and outlined.
        Vec2 pa = project(a);
        Vec2 pb = project(b);
        Vec2 pc = project(c);
        SDL_FColor fill = { 0.20f * bright, 0.80f * bright, 0.75f * bright,
                            1.0f };
        SDL_Vertex verts[3] = { { { pa.x, pa.y }, fill, { 0, 0 } },
                                { { pb.x, pb.y }, fill, { 0, 0 } },
                                { { pc.x, pc.y }, fill, { 0, 0 } } };
        SDL_RenderGeometry(renderer, nullptr, verts, 3, nullptr, 0);
        setColor(renderer, EDGE);
        SDL_RenderLine(renderer, pa.x, pa.y, pb.x, pb.y);
        SDL_RenderLine(renderer, pb.x, pb.y, pc.x, pc.y);
        SDL_RenderLine(renderer, pc.x, pc.y, pa.x, pa.y);

        // The normal and the light, as arrows from the centroid, each 1.1
        // units long in the world, then projected.
        Vec2 from = project(centroid);
        Vec2 toNormal = project(centroid + normal * 1.1f);
        Vec2 toLight = project(centroid + light * 1.1f);
        drawArrow(renderer, from, toNormal, BLUE);
        drawArrow(renderer, from, toLight, AMBER);
        drawText(renderer, font, "normal", toNormal.x + 6, toNormal.y - 9,
                 BLUE);
        drawText(renderer, font, "light", toLight.x + 6, toLight.y - 9,
                 AMBER);

        // The probe, a line to its foot on the plane, and the foot.
        Vec2 p = project(PROBE_AT);
        Vec2 f = project(foot);
        setColor(renderer, PROBE);
        SDL_RenderLine(renderer, p.x, p.y, f.x, f.y);
        SDL_FRect dot = { p.x - 5, p.y - 5, 10, 10 };
        SDL_RenderFillRect(renderer, &dot);
        SDL_FRect ring = { f.x - 5, f.y - 5, 10, 10 };
        SDL_RenderRect(renderer, &ring);

        // The readout: the normal, the cross product's length, the light's
        // dot product, its angle and the brightness, the facing, and the
        // probe. Then the controls.
        float degrees = std::acos(SDL_clamp(ndotl, -1.0f, 1.0f)) * 180.0f / PI;
        drawText(renderer, font,
                 "normal (" + fmt2(normal.x) + ", " + fmt2(normal.y) + ", " +
                 fmt2(normal.z) + ")", 640, 40, TEXT);
        drawText(renderer, font,
                 "cross length " + fmt2(cross.length()) + ", twice the area",
                 640, 62, TEXT);
        drawText(renderer, font,
                 "N dot light " + fmt2(ndotl) + ", " +
                 std::to_string((int)std::lround(degrees)) + " degrees apart",
                 640, 84, TEXT);
        drawText(renderer, font,
                 "brightness " +
                 std::to_string((int)std::lround(bright * 100.0f)) + "%",
                 640, 106, TEXT);
        drawText(renderer, font,
                 std::string("facing ") +
                 (normal.z > 0.0f ? "front: normal z above 0"
                                  : "back: normal z below 0"), 640, 128, TEXT);
        drawText(renderer, font,
                 "probe " + fmt2(std::fabs(above)) +
                 (above >= 0.0f ? " in front of the plane"
                                : " behind the plane"), 640, 150, PROBE);
        drawText(renderer, font,
                 "[Drag] tumble   [Left] [Right] move the light   "
                 "[Space] spin", 12, WINDOW_HEIGHT - 46, GREY);
        drawText(renderer, font,
                 "[W] wind the other way   [R] reset   [Esc] quit",
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
