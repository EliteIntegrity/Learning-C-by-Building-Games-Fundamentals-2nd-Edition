// Tumble - the Chapter 35 demo (rotations done right: quaternions) for
// Learning C++ by Building Games: Fundamentals
//
// One slab, turned through the same four key orientations two ways, side
// by side: on the left by slerping quaternions, and on the right by
// blending Euler angles. Near a pitch of 90 degrees, the Euler angles take
// a long way round, and the readout measures how long. Space pauses, Left
// and Right scrub while it's paused, and R starts again.

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <cmath>
#include <string>
#include "Vec2.h"
#include "Vec3.h"
#include "Mat4.h"
#include "Quat.h"

constexpr int WINDOW_WIDTH  = 1000;
constexpr int WINDOW_HEIGHT = 700;

constexpr SDL_Color TEXT = { 235, 238, 242, 255 };
constexpr SDL_Color GREY = { 90, 100, 112, 255 };
constexpr SDL_Color RED = { 235, 80, 80, 255 };
constexpr SDL_Color GREEN = { 60, 190, 100, 255 };
constexpr SDL_Color BLUE = { 90, 150, 240, 255 };

// The view: pixels for each unit, and where the two slabs' middles are.
// Then the pace: segments a second while playing, and while scrubbing.
constexpr float SCALE = 150.0f;
constexpr float LEFT_X = 250.0f;
constexpr float RIGHT_X = 750.0f;
constexpr float MIDDLE_Y = 370.0f;
constexpr float PLAY_SPEED = 0.28f;
constexpr float SCRUB_SPEED = 0.4f;

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

// Slow at both ends: the cubic 3t^2 - 2t^3, with t^2 taken out.
float smoothstep(float t) { return t * t * (3.0f - 2.0f * t); }

// Euler angles, a yaw about y, then a pitch about x, then a roll about z,
// in radians, as a matrix and as a quaternion. Both multiply their three
// turns in the same order, so that a key is the same orientation both ways.
Mat4 eulerToMat4(Vec3 e)
{
    return Mat4::rotationY(e.x) * Mat4::rotationX(e.y) * Mat4::rotationZ(e.z);
}

Quat eulerToQuat(Vec3 e)
{
    return Quat::fromAxisAngle({ 0, 1, 0 }, e.x) *
           Quat::fromAxisAngle({ 1, 0, 0 }, e.y) *
           Quat::fromAxisAngle({ 0, 0, 1 }, e.z);
}

// Draw a slab under a turn, its middle at (cx, MIDDLE_Y), seen from the
// front with z dropped, as Normals drew. Its edges are shaded by depth, as
// Transforming a Cube's were, and its own axes are drawn from the turn's
// columns: x in red, y in green and z in blue.
void drawSlab(SDL_Renderer* renderer, const Mat4& turn, float cx)
{
    const Vec3 CORNER[8] = { { -0.9f, -0.55f, -0.3f }, { 0.9f, -0.55f, -0.3f },
                             { 0.9f, 0.55f, -0.3f }, { -0.9f, 0.55f, -0.3f },
                             { -0.9f, -0.55f, 0.3f }, { 0.9f, -0.55f, 0.3f },
                             { 0.9f, 0.55f, 0.3f }, { -0.9f, 0.55f, 0.3f } };
    const int EDGE[12][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
                              { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
                              { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
    for (int e = 0; e < 12; ++e)
    {
        Vec3 a = turn.apply(CORNER[EDGE[e][0]]);
        Vec3 b = turn.apply(CORNER[EDGE[e][1]]);
        float shade = SDL_clamp(0.6f + (a.z + b.z) * 0.5f * 0.35f, 0.35f, 1.0f);
        SDL_SetRenderDrawColor(renderer, (Uint8)(150 * shade),
                               (Uint8)(160 * shade), (Uint8)(175 * shade), 255);
        SDL_RenderLine(renderer, cx + a.x * SCALE, MIDDLE_Y - a.y * SCALE,
                       cx + b.x * SCALE, MIDDLE_Y - b.y * SCALE);
    }
    const SDL_Color AXIS_COLOR[3] = { RED, GREEN, BLUE };
    const float AXIS_LENGTH[3] = { 1.2f, 1.05f, 0.9f };
    Vec2 middle = { cx, MIDDLE_Y };
    for (int j = 0; j < 3; ++j)
    {
        Vec3 axis = column(turn, j) * AXIS_LENGTH[j];
        drawArrow(renderer, middle,
                  { cx + axis.x * SCALE, MIDDLE_Y - axis.y * SCALE },
                  AXIS_COLOR[j]);
    }
}

// A blend of two Vec3s, each part on its own, by Chapter 8's lerp.
Vec3 lerp(Vec3 a, Vec3 b, float t)
{
    return { lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t) };
}

// How far each way of blending turns the slab from one key to the next:
// the angles between 200 orientations along the way, added up, in
// radians. The easing doesn't change the path, so it's left out.
void segmentTurns(Vec3 ea, Vec3 eb, float& bySlerp, float& byEuler)
{
    const int STEPS = 200;
    Quat qa = eulerToQuat(ea);
    Quat qb = eulerToQuat(eb);
    Quat lastS = qa;
    Quat lastE = qa;
    bySlerp = 0.0f;
    byEuler = 0.0f;
    for (int i = 1; i <= STEPS; ++i)
    {
        float t = (float)i / STEPS;
        Quat s = slerp(qa, qb, t);
        Quat q = eulerToQuat(lerp(ea, eb, t));
        bySlerp += turnBetween(lastS, s);
        byEuler += turnBetween(lastE, q);
        lastS = s;
        lastE = q;
    }
}

// Degrees, from radians, as whole-number text.
std::string degrees(float radians)
{
    return std::to_string((int)std::lround(radians * 180.0f / PI));
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
        SDL_CreateWindow("Tumble", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The four keys, as a yaw, a pitch and a roll, in degrees, turned into
    // radians. Keys 1 and 2 have a pitch of 85 degrees, near the lock, and
    // yaws far apart. The playhead runs from 0 to 4, a segment a unit, and
    // wraps round.
    const float DEG = PI / 180.0f;
    const Vec3 KEY[4] = { { 0, 0, 0 }, Vec3(150, 85, 0) * DEG,
                          Vec3(-150, 85, 80) * DEG, Vec3(0, -85, 0) * DEG };
    float playhead = 0.0f;
    bool paused = false;

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

        // Events: quitting, and the keys that happen once.
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT) running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN)
            {
                if (event.key.key == SDLK_ESCAPE) running = false;
                else if (event.key.key == SDLK_SPACE) paused = !paused;
                else if (event.key.key == SDLK_R)
                {
                    playhead = 0.0f;
                    paused = false;
                }
            }
        }

        // The playhead moves on while playing, or with Left and Right while
        // paused, and wraps round the four segments.
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if (!paused) playhead += PLAY_SPEED * dt;
        else if (keys[SDL_SCANCODE_LEFT]) playhead -= SCRUB_SPEED * dt;
        else if (keys[SDL_SCANCODE_RIGHT]) playhead += SCRUB_SPEED * dt;
        playhead = std::fmod(playhead + 4.0f, 4.0f);

        // The segment, from key a to key b, and how far along it, eased by
        // Chapter 8's smoothstep, the same for both sides. The left side
        // slerps between the keys' quaternions; the right side blends their
        // Euler angles, each on its own.
        int a = (int)playhead;
        int b = (a + 1) % 4;
        float s = smoothstep(playhead - a);
        Quat left = slerp(eulerToQuat(KEY[a]), eulerToQuat(KEY[b]), s);
        Vec3 right = lerp(KEY[a], KEY[b], s);
        float bySlerp = 0.0f;
        float byEuler = 0.0f;
        segmentTurns(KEY[a], KEY[b], bySlerp, byEuler);

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);

        // A line between the two sides, and the two slabs: the left turned by
        // the slerped quaternion, the right by the blended Euler angles.
        setColor(renderer, GREY);
        float mid = WINDOW_WIDTH / 2.0f;
        SDL_RenderLine(renderer, mid, 110.0f, mid, WINDOW_HEIGHT - 50.0f);
        drawSlab(renderer, left.toMat4(), LEFT_X);
        drawSlab(renderer, eulerToMat4(right), RIGHT_X);

        // The readout: the segment, and how far each side turns over it.
        // Then each side's numbers: the quaternion's four, and the Euler
        // angles' three.
        drawText(renderer, font,
                 "key " + std::to_string(a) + " to key " + std::to_string(b) +
                 ": slerp turns " + degrees(bySlerp) + " degrees, the Euler "
                 "angles " + degrees(byEuler), 12, 10, TEXT);
        drawText(renderer, font, std::string(paused ? "paused" : "playing"),
                 12, 32, GREY);
        drawText(renderer, font, "slerp: quaternion", LEFT_X - 80, 80, TEXT);
        drawText(renderer, font,
                 "(" + fmt2(left.w) + ", " + fmt2(left.x) + ", " +
                 fmt2(left.y) + ", " + fmt2(left.z) + ")", LEFT_X - 120, 102,
                 TEXT);
        drawText(renderer, font, "blended Euler angles", RIGHT_X - 100, 80,
                 TEXT);
        drawText(renderer, font,
                 "yaw " + degrees(right.x) + ", pitch " + degrees(right.y) +
                 ", roll " + degrees(right.z), RIGHT_X - 130, 102, TEXT);
        drawText(renderer, font,
                 "[Space] pause   [Left] [Right] scrub while paused   "
                 "[R] start again   [Esc] quit", 12, WINDOW_HEIGHT - 26, GREY);

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
