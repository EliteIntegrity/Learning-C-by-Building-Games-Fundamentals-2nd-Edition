// Projector - the Chapter 30 demo (projection: from 3D to a 2D screen) for
// Learning C++ by Building Games: Fundamentals
//
// Five cubes of one size, in a row going away from the camera, drawn
// through a perspective projection or an orthographic one. Space swaps the
// two, Up and Down change the field of view, and W and S move the camera.
// Q and E move the near plane, and Z and X the far one. Along the bottom,
// one ruler shows each cube's distance in front of the camera, and another
// the depth that the projection gives it.

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
constexpr SDL_Color AMBER = { 240, 200, 60, 255 };

// The five cubes' colors, from the nearest to the farthest.
constexpr SDL_Color CUBE_COLOR[5] = { { 235, 85, 85, 255 },
                                      { 245, 150, 50, 255 },
                                      { 90, 205, 110, 255 },
                                      { 90, 160, 245, 255 },
                                      { 190, 125, 245, 255 } };

// The window's width over its height. The projection needs it, so that a
// cube is as wide on the window as it's tall.
constexpr float ASPECT = (float)WINDOW_WIDTH / (float)WINDOW_HEIGHT;

// How fast the controls change things, a second, and their limits: the
// field of view, in degrees, the camera's distance, and the near and far
// planes' distances in front of the camera. Then the spin, and how far a
// drag turns the cubes, a pixel.
constexpr float FOV_SPEED = 25.0f;
constexpr float MIN_FOV = 20.0f;
constexpr float MAX_FOV = 110.0f;
constexpr float DOLLY_SPEED = 3.0f;
constexpr float MIN_DIST = 2.0f;
constexpr float MAX_DIST = 22.0f;
constexpr float NEAR_SPEED = 2.0f;
constexpr float FAR_SPEED = 8.0f;
constexpr float MIN_NEAR = 0.1f;
constexpr float MAX_FAR = 50.0f;
constexpr float SPIN_X = 0.2f;
constexpr float SPIN_Y = 0.5f;
constexpr float DRAG_TURN = 0.01f;

// The rulers along the bottom: where their ends are on the window, how far
// down each one is, and the distance in front of the camera that the first
// one reaches.
constexpr float RULER_LEFT = 130.0f;
constexpr float RULER_RIGHT = 950.0f;
constexpr float DIST_Y = 570.0f;
constexpr float DEPTH_Y = 616.0f;
constexpr float RULER_DIST = 50.0f;

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

// A place on a ruler, from a fraction of the way along it: 0 at its left
// end and 1 at its right.
float rulerX(float t)
{
    return RULER_LEFT + SDL_clamp(t, 0.0f, 1.0f) * (RULER_RIGHT - RULER_LEFT);
}

// A point on the window, from the matrix that takes it to clip space: the
// depth test, the divide by w, and the stretch across the window. For a
// point outside the near and far planes, it returns false.
bool toWindow(const Mat4& toClip, Vec3 p, Vec2& out)
{
    Vec4 c = toClip.apply4(p);
    if (c.z < -c.w || c.z > c.w) return false;
    Vec2 ndc = { c.x / c.w, c.y / c.w };
    out = { (ndc.x * 0.5f + 0.5f) * WINDOW_WIDTH,
            (0.5f - ndc.y * 0.5f) * WINDOW_HEIGHT };
    return true;
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
        SDL_CreateWindow("Projector", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // A cube in its own space, 1.6 units on a side: eight corners at every
    // mix of -0.8 and 0.8, and twelve edges, as Transforming a Cube's were.
    const Vec3 CORNER[8] = { { -0.8f, -0.8f, -0.8f }, { 0.8f, -0.8f, -0.8f },
                             { 0.8f, 0.8f, -0.8f }, { -0.8f, 0.8f, -0.8f },
                             { -0.8f, -0.8f, 0.8f }, { 0.8f, -0.8f, 0.8f },
                             { 0.8f, 0.8f, 0.8f }, { -0.8f, 0.8f, 0.8f } };
    const int EDGE[12][2] = { { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
                              { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
                              { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };

    // The five cubes' centers, in a row going away from the camera, each 3
    // units behind the one before, and 1.8 to its right, so that none hides
    // another.
    const Vec3 CENTER[5] = { { -3.6f, 0.0f, 0.0f }, { -1.8f, 0.0f, -3.0f },
                             { 0.0f, 0.0f, -6.0f }, { 1.8f, 0.0f, -9.0f },
                             { 3.6f, 0.0f, -12.0f } };

    // The projection and its controls: which kind, the field of view in
    // degrees, the camera's z, and the near and far planes' distances in
    // front of the camera. Then the cubes' turn, whether they spin, and
    // whether they're being dragged.
    bool ortho = false;
    float fovDeg = 60.0f;
    float camDist = 6.0f;
    float nearDist = 0.5f;
    float farDist = 40.0f;
    float rx = 0.4f;
    float ry = 0.5f;
    bool spin = true;
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
        // turns the cubes, as in Transforming a Cube, and stops their spin.
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT) running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN)
            {
                if (event.key.key == SDLK_ESCAPE) running = false;
                else if (event.key.key == SDLK_SPACE) ortho = !ortho;
                else if (event.key.key == SDLK_R)
                {
                    ortho = false;
                    fovDeg = 60.0f;
                    camDist = 6.0f;
                    nearDist = 0.5f;
                    farDist = 40.0f;
                    rx = 0.4f;
                    ry = 0.5f;
                    spin = true;
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

        // Keys held down change the field of view, move the camera, and move
        // the near and far planes, each within its limits. The near plane
        // stops 1 short of the far one, and the far plane 1 beyond the near
        // one. The cubes spin while they're left alone.
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_UP]) fovDeg += FOV_SPEED * dt;
        if (keys[SDL_SCANCODE_DOWN]) fovDeg -= FOV_SPEED * dt;
        fovDeg = SDL_clamp(fovDeg, MIN_FOV, MAX_FOV);
        if (keys[SDL_SCANCODE_W]) camDist -= DOLLY_SPEED * dt;
        if (keys[SDL_SCANCODE_S]) camDist += DOLLY_SPEED * dt;
        camDist = SDL_clamp(camDist, MIN_DIST, MAX_DIST);
        if (keys[SDL_SCANCODE_Q]) nearDist -= NEAR_SPEED * dt;
        if (keys[SDL_SCANCODE_E]) nearDist += NEAR_SPEED * dt;
        nearDist = SDL_clamp(nearDist, MIN_NEAR, farDist - 1.0f);
        if (keys[SDL_SCANCODE_Z]) farDist -= FAR_SPEED * dt;
        if (keys[SDL_SCANCODE_X]) farDist += FAR_SPEED * dt;
        farDist = SDL_clamp(farDist, nearDist + 1.0f, MAX_FAR);
        if (spin)
        {
            ry += SPIN_Y * dt;
            rx += SPIN_X * dt;
        }

        // The projection, from the controls. The orthographic box is as tall
        // as the perspective view is at the first cube, so that the first
        // cube is the same size in both. The camera sits at (0, 0, camDist),
        // looking down -z, and the view matrix undoes that, with a move.
        float fov = fovDeg * PI / 180.0f;
        float halfHeight = std::tan(fov * 0.5f) * camDist;
        Mat4 proj = Mat4::perspective(fov, ASPECT, nearDist, farDist);
        if (ortho)
            proj = Mat4::orthographic(halfHeight, ASPECT, nearDist, farDist);
        Mat4 view = Mat4::translation(0.0f, 0.0f, -camDist);
        Mat4 R = Mat4::rotationY(ry) * Mat4::rotationX(rx);

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);

        // The cubes. One matrix takes each corner from its cube's own space
        // to clip space: the projection, times the view, times the model.
        // Then toWindow puts the corner on the window, if it passes the depth
        // test, and an edge is drawn if both its corners did.
        for (int i = 0; i < 5; ++i)
        {
            Vec3 c = CENTER[i];
            Mat4 model = Mat4::translation(c.x, c.y, c.z) * R;
            Mat4 toClip = proj * view * model;
            Vec2 at[8];
            bool inside[8];
            for (int k = 0; k < 8; ++k)
                inside[k] = toWindow(toClip, CORNER[k], at[k]);
            setColor(renderer, CUBE_COLOR[i]);
            for (int e = 0; e < 12; ++e)
            {
                int a = EDGE[e][0];
                int b = EDGE[e][1];
                if (!inside[a] || !inside[b]) continue;
                SDL_RenderLine(renderer, at[a].x, at[a].y, at[b].x, at[b].y);
            }
        }

        // Strips of the background's color for the readout and the rulers,
        // so that the cubes don't show through the text.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_FRect top = { 0.0f, 0.0f, (float)WINDOW_WIDTH, 104.0f };
        SDL_FRect bottom = { 0.0f, 548.0f, (float)WINDOW_WIDTH, 152.0f };
        SDL_RenderFillRect(renderer, &top);
        SDL_RenderFillRect(renderer, &bottom);

        // The rulers. The first shows distance in front of the camera, from 0
        // to 50, with the space between the near and far planes shaded. The
        // second shows the depth after the divide, from -1 to 1.
        float nearX = rulerX(nearDist / RULER_DIST);
        float farX = rulerX(farDist / RULER_DIST);
        SDL_SetRenderDrawColor(renderer, 38, 62, 50, 255);
        SDL_FRect band = { nearX, DIST_Y - 8.0f, farX - nearX, 16.0f };
        SDL_RenderFillRect(renderer, &band);
        setColor(renderer, GREY);
        SDL_RenderLine(renderer, RULER_LEFT, DIST_Y, RULER_RIGHT, DIST_Y);
        SDL_RenderLine(renderer, RULER_LEFT, DEPTH_Y, RULER_RIGHT, DEPTH_Y);
        setColor(renderer, TEXT);
        SDL_RenderLine(renderer, nearX, DIST_Y - 12.0f, nearX, DIST_Y + 12.0f);
        SDL_RenderLine(renderer, farX, DIST_Y - 12.0f, farX, DIST_Y + 12.0f);
        drawText(renderer, font, "distance", 12, DIST_Y - 10, TEXT);
        drawText(renderer, font, "near", nearX - 42, DIST_Y + 8, TEXT);
        drawText(renderer, font, "far", farX + 6, DIST_Y + 8, TEXT);
        drawText(renderer, font, "NDC z", 12, DEPTH_Y - 10, TEXT);
        drawText(renderer, font, "-1", RULER_LEFT - 10, DEPTH_Y + 6, GREY);
        drawText(renderer, font, "0", 536, DEPTH_Y + 6, GREY);
        drawText(renderer, font, "1", RULER_RIGHT - 5, DEPTH_Y + 6, GREY);

        // Each cube's marks, in its color: at its center's distance in front
        // of the camera, and at the depth the projection gives it. A cube
        // whose center fails the depth test has a grey mark on the first
        // ruler, and none on the second.
        for (int i = 0; i < 5; ++i)
        {
            Vec3 v = view.apply(CENTER[i]);
            Vec4 c = proj.apply4(v);
            bool inside = c.z >= -c.w && c.z <= c.w;
            setColor(renderer, inside ? CUBE_COLOR[i] : GREY);
            SDL_FRect mark = { rulerX(-v.z / RULER_DIST) - 2.0f, DIST_Y - 8.0f,
                               4.0f, 16.0f };
            SDL_RenderFillRect(renderer, &mark);
            if (!inside) continue;
            mark.x = rulerX((c.z / c.w + 1.0f) * 0.5f) - 2.0f;
            mark.y = DEPTH_Y - 8.0f;
            SDL_RenderFillRect(renderer, &mark);
        }

        // The readout: the projection and the controls, then how tall a unit
        // is on the window at the first cube and at the last. A point 1 above
        // the middle of the view, at each one's z, is taken to clip space and
        // divided by w, and half the window's height is 1 in NDC.
        std::string kind = "perspective, field of view " +
                           std::to_string((int)std::lround(fovDeg)) +
                           " degrees";
        if (ortho)
            kind = "orthographic, a box " + fmt2(2.0f * halfHeight) + " tall";
        drawText(renderer, font, kind, 12, 10, TEXT);
        drawText(renderer, font,
                 "camera at z " + fmt2(camDist) + ", near " + fmt2(nearDist) +
                 ", far " + fmt2(farDist), 12, 32, TEXT);
        Mat4 viewProj = proj * view;
        Vec4 first = viewProj.apply4({ 0.0f, 1.0f, CENTER[0].z });
        Vec4 last = viewProj.apply4({ 0.0f, 1.0f, CENTER[4].z });
        int firstPx = (int)std::lround(first.y / first.w * WINDOW_HEIGHT / 2);
        int lastPx = (int)std::lround(last.y / last.w * WINDOW_HEIGHT / 2);
        drawText(renderer, font,
                 "a unit is " + std::to_string(firstPx) +
                 " px tall at the first cube, " + std::to_string(lastPx) +
                 " at the last", 12, 54, TEXT);

        // The projection matrix, with its bottom row, which makes w, in amber.
        // Then the controls.
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                drawText(renderer, font, padded(proj.m[i][j], 3),
                         640 + j * 86.0f, 10 + i * 22.0f,
                         i == 3 ? AMBER : TEXT);
        drawText(renderer, font,
                 "[Space] perspective or orthographic   [Up] [Down] field of "
                 "view   [Drag] turn", 12, WINDOW_HEIGHT - 46, GREY);
        drawText(renderer, font,
                 "[W] [S] move the camera   [Q] [E] near plane   [Z] [X] far "
                 "plane   [R] reset   [Esc] quit", 12, WINDOW_HEIGHT - 26,
                 GREY);

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
