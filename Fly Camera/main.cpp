// Fly Camera - the Chapter 31 demo (the 3D camera: view matrix and look-at)
// for Learning C++ by Building Games: Fundamentals
//
// A wireframe world of a ground grid and five cubes, seen through a camera
// that flies. The mouse turns it, by a yaw and a pitch, and a look-at view
// matrix places it. WASD moves it along the ground, and E and Q move it up
// and down. Lines that cross the near plane are clipped there. Tab frees
// the mouse, and R puts the camera back.

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <cmath>
#include <string>
#include <vector>
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

// The projection: the window's aspect, a field of view of 70 degrees, and
// the near and far planes. Then the camera's speed, in units a second, how
// far the mouse turns it, in radians a pixel, and the limit on its pitch,
// just short of a quarter turn.
constexpr float ASPECT = (float)WINDOW_WIDTH / (float)WINDOW_HEIGHT;
constexpr float FOV = 70.0f * PI / 180.0f;
constexpr float NEAR_DIST = 0.1f;
constexpr float FAR_DIST = 200.0f;
constexpr float FLY_SPEED = 7.0f;
constexpr float LOOK_TURN = 0.0022f;
constexpr float PITCH_LIMIT = 1.5f;

// A line of the world, from a to b, in its color.
struct Seg
{
    Vec3 a;
    Vec3 b;
    SDL_Color color;
};

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

// Clip a line in view space at the near plane, z = -nearDist. A line that's
// all on the camera's side of the plane gives false. A line that crosses
// the plane has its end on the camera's side moved to where it crosses.
bool clipNear(Vec3& a, Vec3& b, float nearDist)
{
    bool aOut = a.z > -nearDist;
    bool bOut = b.z > -nearDist;
    if (aOut && bOut) return false;
    if (aOut || bOut)
    {
        float t = (-nearDist - a.z) / (b.z - a.z);
        Vec3 onPlane = a + (b - a) * t;
        if (aOut) a = onPlane;
        else b = onPlane;
    }
    return true;
}

// A point in view space, beyond the near plane, on the window: the
// projection, the divide by w, and the stretch across the window.
Vec2 toPixels(const Mat4& proj, Vec3 p)
{
    Vec4 c = proj.apply4(p);
    return { (c.x / c.w * 0.5f + 0.5f) * WINDOW_WIDTH,
             (0.5f - c.y / c.w * 0.5f) * WINDOW_HEIGHT };
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
        SDL_CreateWindow("Fly Camera", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The world, as a list of lines. First the ground: a grid on y = 0, 40
    // units across, with a line every 2.
    std::vector<Seg> world;
    const SDL_Color GRID = { 60, 66, 78, 255 };
    for (int i = -10; i <= 10; ++i)
    {
        float k = i * 2.0f;
        world.push_back({ { k, 0.0f, -20.0f }, { k, 0.0f, 20.0f }, GRID });
        world.push_back({ { -20.0f, 0.0f, k }, { 20.0f, 0.0f, k }, GRID });
    }

    // Then five cubes standing on it, each Transforming a Cube's cube moved
    // into place: its corners, its edges, the cubes' centers and colors.
    const Vec3 CORNER[8] = { { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 },
                             { -1, 1, -1 }, { -1, -1, 1 }, { 1, -1, 1 },
                             { 1, 1, 1 }, { -1, 1, 1 } };
    const int EDGE[12][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
                              { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
                              { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
    const Vec3 CUBE_AT[5] = { { -6, 1, -6 }, { 5, 1, -8 }, { 0, 1, -13 },
                              { 8, 1, 3 }, { -7, 1, 5 } };
    const SDL_Color CUBE_COLOR[5] = { { 90, 150, 240, 255 },
                                      { 230, 120, 60, 255 },
                                      { 80, 200, 120, 255 },
                                      { 220, 80, 110, 255 },
                                      { 200, 190, 70, 255 } };
    for (int c = 0; c < 5; ++c)
        for (int e = 0; e < 12; ++e)
            world.push_back({ CUBE_AT[c] + CORNER[EDGE[e][0]],
                              CUBE_AT[c] + CORNER[EDGE[e][1]],
                              CUBE_COLOR[c] });

    // The camera: where it is, and its yaw and pitch, in radians. A yaw of
    // 0 looks down -z, and a pitch of 0 looks level. The mouse is captured
    // for looking: hidden, and read as movement only.
    Vec3 eye = { 0.0f, 2.0f, 10.0f };
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool captured = true;
    SDL_SetWindowRelativeMouseMode(window, captured);

    // The projection, which never changes.
    Mat4 proj = Mat4::perspective(FOV, ASPECT, NEAR_DIST, FAR_DIST);

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

        // Events: quitting, and the keys that happen once. Tab frees the
        // mouse or captures it again. While it's captured, its movement turns
        // the camera: across for the yaw, and up and down for the pitch.
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT) running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN)
            {
                if (event.key.key == SDLK_ESCAPE) running = false;
                else if (event.key.key == SDLK_TAB)
                {
                    captured = !captured;
                    SDL_SetWindowRelativeMouseMode(window, captured);
                }
                else if (event.key.key == SDLK_R)
                {
                    eye = { 0.0f, 2.0f, 10.0f };
                    yaw = 0.0f;
                    pitch = 0.0f;
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_MOTION && captured)
            {
                yaw += event.motion.xrel * LOOK_TURN;
                pitch -= event.motion.yrel * LOOK_TURN;
                pitch = SDL_clamp(pitch, -PITCH_LIMIT, PITCH_LIMIT);
            }
        }

        // The camera's directions, from its yaw and pitch. Forward is the way
        // it looks. Flat is forward made level, and right is at right angles
        // to flat, along the ground.
        Vec3 forward = { std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                         -std::cos(yaw) * std::cos(pitch) };
        Vec3 flat = { std::sin(yaw), 0.0f, -std::cos(yaw) };
        Vec3 right = { std::cos(yaw), 0.0f, std::sin(yaw) };

        // Keys held down move the camera: W and S along flat, D and A along
        // right, and E and Q straight up and down. Their sum is normalized,
        // so that two keys at once are no faster than one.
        const bool* keys = SDL_GetKeyboardState(nullptr);
        Vec3 move;
        if (keys[SDL_SCANCODE_W]) move += flat;
        if (keys[SDL_SCANCODE_S]) move -= flat;
        if (keys[SDL_SCANCODE_D]) move += right;
        if (keys[SDL_SCANCODE_A]) move -= right;
        if (keys[SDL_SCANCODE_E]) move.y += 1.0f;
        if (keys[SDL_SCANCODE_Q]) move.y -= 1.0f;
        eye += move.normalized() * (FLY_SPEED * dt);

        // The view matrix, from the eye, a point 1 unit ahead of it, and the
        // world's up as the hint.
        const Vec3 UP = { 0.0f, 1.0f, 0.0f };
        Mat4 view = Mat4::lookAt(eye, eye + forward, UP);

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);

        // The world, line by line. Both ends go into view space, the line is
        // clipped at the near plane, and its ends are put on the window.
        for (const Seg& seg : world)
        {
            Vec3 a = view.apply(seg.a);
            Vec3 b = view.apply(seg.b);
            if (!clipNear(a, b, NEAR_DIST)) continue;
            Vec2 pa = toPixels(proj, a);
            Vec2 pb = toPixels(proj, b);
            setColor(renderer, seg.color);
            SDL_RenderLine(renderer, pa.x, pa.y, pb.x, pb.y);
        }

        // Strips of the background's color for the readout and the controls,
        // so that the world's lines don't show through the text.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_FRect top = { 0.0f, 0.0f, (float)WINDOW_WIDTH, 100.0f };
        SDL_FRect bottom = { 0.0f, WINDOW_HEIGHT - 36.0f, (float)WINDOW_WIDTH,
                             36.0f };
        SDL_RenderFillRect(renderer, &top);
        SDL_RenderFillRect(renderer, &bottom);

        // The readout: the eye, the yaw and pitch in degrees, forward, and
        // the length of forward x up, which lookAt normalizes to get right.
        int yawDeg = (int)std::lround(yaw * 180.0f / PI);
        int pitchDeg = (int)std::lround(pitch * 180.0f / PI);
        drawText(renderer, font,
                 "eye (" + fmt2(eye.x) + ", " + fmt2(eye.y) + ", " +
                 fmt2(eye.z) + ")", 12, 10, TEXT);
        drawText(renderer, font,
                 "yaw " + std::to_string(yawDeg) + ", pitch " +
                 std::to_string(pitchDeg) + " degrees", 12, 32, TEXT);
        drawText(renderer, font,
                 "forward (" + fmt2(forward.x) + ", " + fmt2(forward.y) +
                 ", " + fmt2(forward.z) + ")", 12, 54, TEXT);
        drawText(renderer, font,
                 "forward x up is " + fmt2(forward.cross(UP).length()) +
                 " long", 12, 76, TEXT);

        // The view matrix, with its rows in the colors of the camera's axes:
        // right in red, up in green and back in blue. Then the controls.
        const SDL_Color ROW[4] = { RED, GREEN, BLUE, TEXT };
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                drawText(renderer, font, padded(view.m[i][j], 2),
                         640 + j * 86.0f, 10 + i * 22.0f, ROW[i]);
        drawText(renderer, font,
                 "[Mouse] look   [WASD] move   [E] [Q] up and down   "
                 "[Tab] free the mouse   [R] reset   [Esc] quit",
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
