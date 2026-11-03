// Textured Cube - the Chapter 34 demo (texturing in 3D) for Learning C++
// by Building Games: Fundamentals
//
// A cube with a checkerboard on every face, turning under a light, filled
// by our own rasterizer. T switches the texture coordinates between affine,
// blended straight across the picture, and perspective-correct, blended
// over w and then divided. The readout says how far apart the two are.
// Drag to orbit the camera, and roll the wheel to move it in and out.

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "Vec2.h"
#include "Vec3.h"
#include "Mat4.h"
#include "Mesh.h"

constexpr int WINDOW_WIDTH  = 1000;
constexpr int WINDOW_HEIGHT = 700;

constexpr SDL_Color TEXT = { 235, 238, 242, 255 };
constexpr SDL_Color GREY = { 90, 100, 112, 255 };

// The picture the rasterizer fills: a pixel for each of the window's.
constexpr int PIC_WIDTH = WINDOW_WIDTH;
constexpr int PIC_HEIGHT = WINDOW_HEIGHT;

// The projection, the spin and the orbit camera, as in Solid Cube, with
// the camera allowed a little nearer.
constexpr float ASPECT = (float)WINDOW_WIDTH / (float)WINDOW_HEIGHT;
constexpr float FOV = 55.0f * PI / 180.0f;
constexpr float NEAR_DIST = 0.1f;
constexpr float FAR_DIST = 100.0f;
constexpr float SPIN_SPEED = 0.5f;
constexpr float DRAG_TURN = 0.01f;
constexpr float PITCH_LIMIT = 1.4f;
constexpr float MIN_DIST = 2.4f;
constexpr float MAX_DIST = 12.0f;
constexpr float WHEEL_STEP = 0.4f;

// The texture: a checkerboard of 8 by 8, light and blue, with a red stripe
// along its left edge and its top, so that its way round shows.
constexpr int CHECKS = 8;
constexpr SDL_Color LIGHT_CHECK = { 235, 235, 240, 255 };
constexpr SDL_Color DARK_CHECK = { 45, 100, 195, 255 };
constexpr SDL_Color STRIPE = { 220, 70, 60, 255 };

// A triangle of the cube, in its own space, with a texture coordinate at
// each corner.
struct TexTri
{
    Vec3 p[3];
    Vec2 uv[3];
};

// A corner on the picture: its pixel, its depth in NDC, 1 over its w, and
// its texture coordinate.
struct TexVert
{
    Vec2 p;
    float z;
    float invW;
    Vec2 uv;
};

// A triangle ready to fill: its corners on the picture, and its brightness.
struct TexDraw
{
    TexVert v[3];
    float bright;
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

// The texture, made by a function of u and v rather than stored. Both are
// clamped first, since a blend at a triangle's edge can come out a hair
// outside 0 to 1, and the 8 by 8 checks come from their whole parts.
SDL_Color sampleChecker(Vec2 uv)
{
    float u = SDL_clamp(uv.x, 0.0f, 0.999f);
    float v = SDL_clamp(uv.y, 0.0f, 0.999f);
    if (u < 0.06f || v < 0.06f) return STRIPE;
    bool light = ((int)(u * CHECKS) + (int)(v * CHECKS)) % 2 == 1;
    return light ? LIGHT_CHECK : DARK_CHECK;
}

// The cube's textured triangles: each face of the built-in cube, scaled,
// with the texture's corners on its corners, cut into a fan of two
// triangles that carry their corners' texture coordinates. Each face's
// loop starts at its bottom left corner, seen from outside, and v grows
// down the texture, as SDL's does.
std::vector<TexTri> texturedCube(float size)
{
    const Vec2 FACE_UV[4] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };
    Mesh cube = builtinCube();
    std::vector<TexTri> tris;
    for (const std::vector<int>& f : cube.faces)
        for (int k = 1; k + 1 < (int)f.size(); ++k)
        {
            const int corner[3] = { 0, k, k + 1 };
            TexTri t;
            for (int i = 0; i < 3; ++i)
            {
                t.p[i] = cube.verts[f[corner[i]]] * size;
                t.uv[i] = FACE_UV[corner[i]];
            }
            tris.push_back(t);
        }
    return tris;
}

// A point in clip space, beyond the near plane, on the picture: the divide
// by w, and the stretch across the picture.
Vec2 toPixels(Vec4 c)
{
    return { (c.x / c.w * 0.5f + 0.5f) * PIC_WIDTH,
             (0.5f - c.y / c.w * 0.5f) * PIC_HEIGHT };
}

// A triangle's corners onto the picture, through clip space, each with
// its NDC depth, 1 over its w and its texture coordinate. A triangle with
// a corner short of the near plane gives false, to be dropped whole.
bool toPicture(const Mat4& toClip, const Vec3 corner[3], const Vec2 uv[3],
               TexDraw& d)
{
    for (int k = 0; k < 3; ++k)
    {
        Vec4 c = toClip.apply4(corner[k]);
        if (c.z + c.w < 0.0f) return false;
        d.v[k] = { toPixels(c), c.z / c.w, 1.0f / c.w, uv[k] };
    }
    return true;
}

// Fill a textured triangle into the picture, with the z-buffer, by Solid
// Cube's cross products. Over the area, they're the weights b0, b1 and b2.
// Each pixel's texture coordinate is blended two ways: affine, from the
// corners' coordinates, and perspective-correct, from their coordinates
// over w and their 1 over w, then divided. The texture is sampled at the
// one asked for, and the largest gap between the two is kept in worst.
void fillTextured(std::vector<SDL_Color>& color, std::vector<float>& depth,
                  const TexDraw& d, bool perspective, float& worst)
{
    const TexVert& v0 = d.v[0];
    const TexVert& v1 = d.v[1];
    const TexVert& v2 = d.v[2];
    float area = (v1.p - v0.p).cross(v2.p - v0.p);
    if (std::fabs(area) < 1e-6f) return;

    // The box of pixels the triangle covers, kept inside the picture.
    int x0 = std::max(0, (int)std::floor(std::min({ v0.p.x, v1.p.x, v2.p.x })));
    int x1 = std::min(PIC_WIDTH - 1,
                      (int)std::ceil(std::max({ v0.p.x, v1.p.x, v2.p.x })));
    int y0 = std::max(0, (int)std::floor(std::min({ v0.p.y, v1.p.y, v2.p.y })));
    int y1 = std::min(PIC_HEIGHT - 1,
                      (int)std::ceil(std::max({ v0.p.y, v1.p.y, v2.p.y })));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
        {
            Vec2 p = { x + 0.5f, y + 0.5f };
            float e0 = (v2.p - v1.p).cross(p - v1.p);
            float e1 = (v0.p - v2.p).cross(p - v2.p);
            float e2 = (v1.p - v0.p).cross(p - v0.p);
            bool inside = (e0 >= 0 && e1 >= 0 && e2 >= 0) ||
                          (e0 <= 0 && e1 <= 0 && e2 <= 0);
            if (!inside) continue;
            float b0 = e0 / area;
            float b1 = e1 / area;
            float b2 = e2 / area;
            float z = b0 * v0.z + b1 * v1.z + b2 * v2.z;
            int i = y * PIC_WIDTH + x;
            if (z >= depth[i]) continue;

            Vec2 affine = v0.uv * b0 + v1.uv * b1 + v2.uv * b2;
            float invW = b0 * v0.invW + b1 * v1.invW + b2 * v2.invW;
            Vec2 correct = (v0.uv * (b0 * v0.invW) + v1.uv * (b1 * v1.invW) +
                            v2.uv * (b2 * v2.invW)) / invW;
            worst = std::max(worst, (correct - affine).length());
            SDL_Color texel = sampleChecker(perspective ? correct : affine);
            depth[i] = z;
            color[i] = { (Uint8)(texel.r * d.bright),
                         (Uint8)(texel.g * d.bright),
                         (Uint8)(texel.b * d.bright), 255 };
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
        SDL_CreateWindow("Textured Cube", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The picture, its texture and its depths, as in Solid Cube.
    SDL_Texture* picture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                             SDL_TEXTUREACCESS_STREAMING,
                                             PIC_WIDTH, PIC_HEIGHT);
    std::vector<SDL_Color> color(PIC_WIDTH * PIC_HEIGHT);
    std::vector<float> depth(PIC_WIDTH * PIC_HEIGHT);

    // The cube, 2.6 units on a side, made once. Then the light's direction,
    // and the controls: the texture coordinates' blend, the spin and the
    // orbit camera.
    std::vector<TexTri> cube = texturedCube(1.3f);
    const Vec3 LIGHT = Vec3(-0.35f, 0.7f, 0.6f).normalized();
    bool perspective = true;
    float spin = 0.0f;
    bool spinning = true;
    bool dragging = false;
    float yaw = 0.5f;
    float pitch = 0.35f;
    float dist = 5.0f;

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

        // Events: quitting, and the keys, clicks and turns of the wheel that
        // happen once. A drag orbits the camera, and the wheel moves it.
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT) running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN)
            {
                if (event.key.key == SDLK_ESCAPE) running = false;
                else if (event.key.key == SDLK_T) perspective = !perspective;
                else if (event.key.key == SDLK_SPACE) spinning = !spinning;
                else if (event.key.key == SDLK_R)
                {
                    perspective = true;
                    spin = 0.0f;
                    yaw = 0.5f;
                    pitch = 0.35f;
                    dist = 5.0f;
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                     event.button.button == SDL_BUTTON_LEFT) dragging = true;
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) dragging = false;
            else if (event.type == SDL_EVENT_MOUSE_MOTION && dragging)
            {
                yaw -= event.motion.xrel * DRAG_TURN;
                pitch += event.motion.yrel * DRAG_TURN;
                pitch = SDL_clamp(pitch, -PITCH_LIMIT, PITCH_LIMIT);
            }
            else if (event.type == SDL_EVENT_MOUSE_WHEEL)
                dist = SDL_clamp(dist - event.wheel.y * WHEEL_STEP, MIN_DIST,
                                 MAX_DIST);
        }

        // The cube spins, and the eye orbits the middle, as in Solid Cube.
        if (spinning) spin += SPIN_SPEED * dt;
        Mat4 model = Mat4::rotationY(spin) * Mat4::rotationX(spin * 0.5f);
        Vec3 eye = { dist * std::cos(pitch) * std::sin(yaw),
                     dist * std::sin(pitch),
                     dist * std::cos(pitch) * std::cos(yaw) };
        const Vec3 MIDDLE = { 0.0f, 0.0f, 0.0f };
        const Vec3 UP = { 0.0f, 1.0f, 0.0f };
        Mat4 toClip = proj * Mat4::lookAt(eye, MIDDLE, UP);

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);

        // Clear the picture and its depths, as in Solid Cube.
        std::fill(color.begin(), color.end(), SDL_Color{ 24, 26, 30, 255 });
        std::fill(depth.begin(), depth.end(), 2.0f);

        // Each triangle: into the world, culled if it faces away, lit, put
        // on the picture with its texture coordinates, and filled.
        float worst = 0.0f;
        for (const TexTri& t : cube)
        {
            Vec3 world[3] = { model.apply(t.p[0]), model.apply(t.p[1]),
                              model.apply(t.p[2]) };
            Vec3 n = (world[1] - world[0]).cross(world[2] - world[0])
                         .normalized();
            Vec3 center = (world[0] + world[1] + world[2]) / 3.0f;
            if (n.dot(eye - center) <= 0.0f) continue;
            TexDraw d;
            d.bright = SDL_clamp(n.dot(LIGHT), 0.0f, 1.0f) * 0.7f + 0.3f;
            if (!toPicture(toClip, world, t.uv, d)) continue;
            fillTextured(color, depth, d, perspective, worst);
        }

        // Show the picture, as in Solid Cube.
        SDL_UpdateTexture(picture, nullptr, color.data(), PIC_WIDTH * 4);
        SDL_RenderTexture(renderer, picture, nullptr, nullptr);

        // Strips of the background's color for the readout and the controls,
        // so that the cube doesn't show through the text.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_FRect top = { 0.0f, 0.0f, (float)WINDOW_WIDTH, 60.0f };
        SDL_FRect bottom = { 0.0f, WINDOW_HEIGHT - 36.0f, (float)WINDOW_WIDTH,
                             36.0f };
        SDL_RenderFillRect(renderer, &top);
        SDL_RenderFillRect(renderer, &bottom);

        // The readout: the blend in use, and the largest gap between the two
        // blends on any pixel drawn, as a share of the texture's width.
        drawText(renderer, font,
                 std::string("texture coordinates: ") +
                 (perspective ? "perspective-correct" : "affine"),
                 12, 10, TEXT);
        drawText(renderer, font,
                 "the two blends differ by up to " + fmt2(worst) +
                 " of the texture", 12, 32, TEXT);
        drawText(renderer, font,
                 "[T] affine or correct   [Space] spin   [Drag] orbit   "
                 "[Wheel] in and out   [R] reset   [Esc] quit",
                 12, WINDOW_HEIGHT - 26, GREY);

        SDL_RenderPresent(renderer);
    }

    // Clean up, in the reverse order of creation.
    SDL_DestroyTexture(picture);
    if (font) TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
