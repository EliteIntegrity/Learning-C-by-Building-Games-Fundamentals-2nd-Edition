// Solid Cube - the Chapter 33 demo (hidden surfaces: culling, sorting and
// the z-buffer) for Learning C++ by Building Games: Fundamentals
//
// A cube with a bar through it, filled by our own rasterizer into a picture
// and a depth for each of its pixels. M switches how hidden surfaces are
// handled: by the z-buffer, by the painter's algorithm, or not at all. C
// switches back-face culling. Drag to orbit the camera, and roll the wheel
// to move it in and out.

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

// The projection: the window's aspect, a field of view of 55 degrees, and
// the near and far planes. Then the scene's spin, in radians a second, and
// the orbit camera's drag, pitch limit, distances and wheel step, as in
// Mesh Viewer, but kept outside the boxes.
constexpr float ASPECT = (float)WINDOW_WIDTH / (float)WINDOW_HEIGHT;
constexpr float FOV = 55.0f * PI / 180.0f;
constexpr float NEAR_DIST = 0.1f;
constexpr float FAR_DIST = 100.0f;
constexpr float SPIN_SPEED = 0.5f;
constexpr float DRAG_TURN = 0.01f;
constexpr float PITCH_LIMIT = 1.4f;
constexpr float MIN_DIST = 4.0f;
constexpr float MAX_DIST = 14.0f;
constexpr float WHEEL_STEP = 0.5f;

// The ways of handling hidden surfaces, in the order M steps through them.
constexpr int ZBUFFER = 0;
constexpr int PAINTER = 1;
constexpr int NONE = 2;
const char* const METHOD_NAME[3] = { "the z-buffer", "the painter's algorithm",
                                     "none" };

// A triangle of the scene, in the scene's own space, with its color.
struct Tri
{
    Vec3 a;
    Vec3 b;
    Vec3 c;
    SDL_Color color;
};

// A corner on the picture: its pixel, and its depth in NDC.
struct ScreenVert
{
    Vec2 p;
    float z;
};

// A triangle ready to fill: its three corners on the picture, its shaded
// color, and its depth for the painter's algorithm.
struct Draw
{
    ScreenVert v[3];
    SDL_Color color;
    float key;
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

// Add a box to the scene: the built-in cube, placed by a matrix, with each
// face cut into a fan of triangles from its first corner. A face of n
// corners gives n - 2 triangles, each wound as the face is.
void addBox(std::vector<Tri>& tris, const Mat4& place, SDL_Color color)
{
    Mesh cube = builtinCube();
    for (const std::vector<int>& f : cube.faces)
        for (size_t k = 1; k + 1 < f.size(); ++k)
            tris.push_back({ place.apply(cube.verts[f[0]]),
                             place.apply(cube.verts[f[k]]),
                             place.apply(cube.verts[f[k + 1]]), color });
}

// A point in clip space, beyond the near plane, on the picture: the divide
// by w, and the stretch across the picture.
Vec2 toPixels(Vec4 c)
{
    return { (c.x / c.w * 0.5f + 0.5f) * PIC_WIDTH,
             (0.5f - c.y / c.w * 0.5f) * PIC_HEIGHT };
}

// A triangle's corners onto the picture, through clip space, and its
// depth for the painter's algorithm, the average of its corners'. A
// triangle with a corner short of the near plane gives false, to be
// dropped whole.
bool toPicture(const Mat4& toClip, const Vec3 corner[3], Draw& d)
{
    for (int k = 0; k < 3; ++k)
    {
        Vec4 c = toClip.apply4(corner[k]);
        if (c.z + c.w < 0.0f) return false;
        d.v[k] = { toPixels(c), c.z / c.w };
    }
    d.key = (d.v[0].z + d.v[1].z + d.v[2].z) / 3.0f;
    return true;
}

// Fill a triangle into the picture. Each pixel inside it gets its color,
// and with useDepth, only if the triangle is nearer there than what's
// already been drawn. The cross products e0, e1 and e2 are twice the areas
// of the three triangles the pixel's center makes with the edges, with a
// sign. Inside, all three have the area's sign, and over the area, they're
// the corners' shares of the pixel, which blend their depths. Every pixel
// inside is counted in filled.
void fillTriangle(std::vector<SDL_Color>& color, std::vector<float>& depth,
                  const Draw& d, bool useDepth, int& filled)
{
    Vec2 a = d.v[0].p;
    Vec2 b = d.v[1].p;
    Vec2 c = d.v[2].p;
    float area = (b - a).cross(c - a);
    if (std::fabs(area) < 1e-6f) return;

    // The box of pixels the triangle covers, kept inside the picture.
    int x0 = std::max(0, (int)std::floor(std::min({ a.x, b.x, c.x })));
    int x1 = std::min(PIC_WIDTH - 1,
                      (int)std::ceil(std::max({ a.x, b.x, c.x })));
    int y0 = std::max(0, (int)std::floor(std::min({ a.y, b.y, c.y })));
    int y1 = std::min(PIC_HEIGHT - 1,
                      (int)std::ceil(std::max({ a.y, b.y, c.y })));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
        {
            Vec2 p = { x + 0.5f, y + 0.5f };
            float e0 = (c - b).cross(p - b);
            float e1 = (a - c).cross(p - c);
            float e2 = (b - a).cross(p - a);
            bool inside = (e0 >= 0 && e1 >= 0 && e2 >= 0) ||
                          (e0 <= 0 && e1 <= 0 && e2 <= 0);
            if (!inside) continue;
            ++filled;
            float z = (e0 * d.v[0].z + e1 * d.v[1].z + e2 * d.v[2].z) / area;
            int i = y * PIC_WIDTH + x;
            if (useDepth && z >= depth[i]) continue;
            depth[i] = z;
            color[i] = d.color;
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
        SDL_CreateWindow("Solid Cube", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The picture, and the texture it's shown through. A vector of SDL_Color
    // holds each pixel's red, green, blue and alpha bytes in that order,
    // which is RGBA32's order (Chapter 24). Then a depth for each pixel.
    SDL_Texture* picture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                             SDL_TEXTUREACCESS_STREAMING,
                                             PIC_WIDTH, PIC_HEIGHT);
    std::vector<SDL_Color> color(PIC_WIDTH * PIC_HEIGHT);
    std::vector<float> depth(PIC_WIDTH * PIC_HEIGHT);

    // The scene: a cube, and a long, thin bar through it, turned so that the
    // two cross at a slant. Both are made once, in the scene's own space.
    std::vector<Tri> scene;
    addBox(scene, Mat4::scale(1.1f, 1.1f, 1.1f), { 90, 150, 240, 255 });
    addBox(scene, Mat4::rotationY(0.7f) * Mat4::rotationX(0.5f) *
                  Mat4::scale(2.8f, 0.42f, 0.42f), { 235, 140, 60, 255 });

    // The light's direction, toward the light. Then the method, culling,
    // the spin, and the orbit camera.
    const Vec3 LIGHT = Vec3(-0.4f, 0.7f, 0.6f).normalized();
    int method = ZBUFFER;
    bool culling = true;
    float spin = 0.0f;
    bool spinning = true;
    bool dragging = false;
    float yaw = 0.4f;
    float pitch = 0.3f;
    float dist = 6.0f;

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
                else if (event.key.key == SDLK_M) method = (method + 1) % 3;
                else if (event.key.key == SDLK_C) culling = !culling;
                else if (event.key.key == SDLK_SPACE) spinning = !spinning;
                else if (event.key.key == SDLK_R)
                {
                    method = ZBUFFER;
                    culling = true;
                    spin = 0.0f;
                    yaw = 0.4f;
                    pitch = 0.3f;
                    dist = 6.0f;
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

        // The scene spins about y, and more slowly about x. The eye orbits the
        // middle, as in Mesh Viewer, and looks at it.
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

        // Clear the picture to the background, and every depth to 2, beyond
        // anything's NDC depth, so that the first triangle at a pixel wins it.
        std::fill(color.begin(), color.end(), SDL_Color{ 24, 26, 30, 255 });
        std::fill(depth.begin(), depth.end(), 2.0f);

        // Each triangle: into the world by the model matrix, culled if it
        // faces away from the eye, shaded by Lambert's law, with a little
        // light everywhere, as in Normals, and put on the picture. The range
        // of its corners' depths is kept for the readout.
        std::vector<Draw> draws;
        int culled = 0;
        float nearest = 2.0f;
        float farthest = -2.0f;
        for (const Tri& t : scene)
        {
            Vec3 corner[3] = { model.apply(t.a), model.apply(t.b),
                               model.apply(t.c) };
            Vec3 n = (corner[1] - corner[0]).cross(corner[2] - corner[0])
                         .normalized();
            Vec3 center = (corner[0] + corner[1] + corner[2]) / 3.0f;
            if (culling && n.dot(eye - center) <= 0.0f)
            {
                ++culled;
                continue;
            }
            float bright = SDL_clamp(n.dot(LIGHT), 0.0f, 1.0f) * 0.75f + 0.25f;
            Draw d;
            d.color = { (Uint8)(t.color.r * bright),
                        (Uint8)(t.color.g * bright),
                        (Uint8)(t.color.b * bright), 255 };
            if (!toPicture(toClip, corner, d)) continue;
            for (const ScreenVert& v : d.v)
            {
                nearest = std::min(nearest, v.z);
                farthest = std::max(farthest, v.z);
            }
            draws.push_back(d);
        }

        // The painter's algorithm sorts the triangles, the farthest first,
        // and paints the nearer over them. The z-buffer needs no sort.
        if (method == PAINTER)
            std::sort(draws.begin(), draws.end(),
                      [](const Draw& p, const Draw& q)
                      { return p.key > q.key; });
        int filled = 0;
        for (const Draw& d : draws)
            fillTriangle(color, depth, d, method == ZBUFFER, filled);

        // Show the picture: copy it into the texture, four bytes a pixel, and
        // draw the texture over the whole window.
        SDL_UpdateTexture(picture, nullptr, color.data(), PIC_WIDTH * 4);
        SDL_RenderTexture(renderer, picture, nullptr, nullptr);

        // The readout: the method and culling, the triangles drawn and
        // culled, the pixels filled as a share of the picture's, and the
        // corners' range of NDC depths.
        int share = (int)std::lround(100.0 * filled / (PIC_WIDTH * PIC_HEIGHT));
        drawText(renderer, font,
                 std::string("hidden surfaces: ") + METHOD_NAME[method] +
                 ", culling " + (culling ? "on" : "off"), 12, 10, TEXT);
        drawText(renderer, font,
                 std::to_string(draws.size()) + " triangles drawn, " +
                 std::to_string(culled) + " culled, pixels filled " +
                 std::to_string(share) + "% of the picture's", 12, 32, TEXT);
        drawText(renderer, font,
                 "NDC depths of the corners: " + fmt2(nearest) + " to " +
                 fmt2(farthest), 12, 54, TEXT);
        drawText(renderer, font,
                 "[M] method   [C] culling   [Space] spin   [Drag] orbit   "
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
