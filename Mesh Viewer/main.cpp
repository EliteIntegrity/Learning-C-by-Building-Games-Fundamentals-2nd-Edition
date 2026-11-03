// Mesh Viewer - the Chapter 32 demo (the full pipeline and wireframe
// rendering) for Learning C++ by Building Games: Fundamentals
//
// Meshes in wireframe, through the whole pipeline: one matrix from a mesh's
// own space to clip space, a clip at the near plane, the divide and the
// stretch across the window. Three meshes are built in, and a fourth is
// loaded from pyramid.obj. Drag to orbit the camera, and roll the wheel to
// move it nearer or farther. N shows the next mesh, C traces the next
// corner, and Space stops the spin.

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <cmath>
#include <string>
#include <utility>
#include <vector>
#include "Vec2.h"
#include "Vec3.h"
#include "Mat4.h"
#include "Mesh.h"

constexpr int WINDOW_WIDTH  = 1000;
constexpr int WINDOW_HEIGHT = 700;

constexpr SDL_Color TEXT = { 235, 238, 242, 255 };
constexpr SDL_Color GREY = { 90, 100, 112, 255 };
constexpr SDL_Color AMBER = { 240, 200, 60, 255 };

// The projection: the window's aspect, a field of view of 60 degrees, and
// the near and far planes. Then the mesh's spin, in radians a second, how
// far a drag turns the camera, in radians a pixel, and the camera's limits:
// its pitch, its distance from the middle, and the wheel's step.
constexpr float ASPECT = (float)WINDOW_WIDTH / (float)WINDOW_HEIGHT;
constexpr float FOV = 60.0f * PI / 180.0f;
constexpr float NEAR_DIST = 0.1f;
constexpr float FAR_DIST = 100.0f;
constexpr float SPIN_SPEED = 0.6f;
constexpr float DRAG_TURN = 0.01f;
constexpr float PITCH_LIMIT = 1.4f;
constexpr float MIN_DIST = 0.5f;
constexpr float MAX_DIST = 20.0f;
constexpr float WHEEL_STEP = 0.4f;

// An edge of a mesh: its two corners, by index, the lower one first.
struct Edge
{
    int a;
    int b;
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

// A mesh's edges, each once. Each face's loop gives an edge from each
// corner to the next, and from its last corner back to its first. An edge
// shared by two faces comes up twice, and is kept the first time.
std::vector<Edge> edgesOf(const Mesh& m)
{
    std::vector<Edge> edges;
    for (const std::vector<int>& f : m.faces)
        for (size_t k = 0; k < f.size(); ++k)
        {
            int a = f[k];
            int b = f[(k + 1) % f.size()];
            if (a > b) std::swap(a, b);
            bool seen = false;
            for (const Edge& e : edges)
                if (e.a == a && e.b == b) seen = true;
            if (!seen) edges.push_back({ a, b });
        }
    return edges;
}

// Clip an edge in clip space at the near plane, where z = -w. A point is
// beyond the plane when z + w is positive. An edge with both ends short of
// it is dropped, and one that crosses it has its short end moved to where
// z + w is 0. Before the divide, a straight edge is still a straight line
// in four coordinates, so the crossing is found as in Fly Camera.
bool clipAtNear(Vec4& a, Vec4& b)
{
    float da = a.z + a.w;
    float db = b.z + b.w;
    if (da < 0.0f && db < 0.0f) return false;
    if (da < 0.0f || db < 0.0f)
    {
        float t = da / (da - db);
        Vec4 onPlane = { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                         a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t };
        if (da < 0.0f) a = onPlane;
        else b = onPlane;
    }
    return true;
}

// A point in clip space, beyond the near plane, on the window: the divide
// by w, and the stretch across the window.
Vec2 toPixels(Vec4 c)
{
    return { (c.x / c.w * 0.5f + 0.5f) * WINDOW_WIDTH,
             (0.5f - c.y / c.w * 0.5f) * WINDOW_HEIGHT };
}

// A Vec3 as text, such as (1.00, -0.50, 2.25).
std::string vecText(Vec3 v)
{
    return "(" + fmt2(v.x) + ", " + fmt2(v.y) + ", " + fmt2(v.z) + ")";
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
        SDL_CreateWindow("Mesh Viewer", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The meshes: three built in, and a fourth from pyramid.obj, beside the
    // program, if it's there and it loads. Then the edges of the one shown.
    std::vector<Mesh> meshes = { builtinCube(), builtinOctahedron(),
                                 builtinTetrahedron() };
    Mesh loaded;
    if (loadObj(folder + "pyramid.obj", loaded)) meshes.push_back(loaded);
    int current = 0;
    std::vector<Edge> edges = edgesOf(meshes[current]);

    // The orbit camera: its yaw and pitch about the middle, in radians, and
    // its distance from it. Then the mesh's spin, whether it spins, whether
    // the camera is being dragged, and the corner the readout traces.
    float yaw = 0.6f;
    float pitch = 0.4f;
    float dist = 5.0f;
    float spin = 0.0f;
    bool spinning = true;
    bool dragging = false;
    int traced = 0;

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
        // happen once. A drag orbits the camera, and the wheel moves it
        // nearer the middle or farther from it.
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT) running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN)
            {
                if (event.key.key == SDLK_ESCAPE) running = false;
                else if (event.key.key == SDLK_N)
                {
                    current = (current + 1) % (int)meshes.size();
                    edges = edgesOf(meshes[current]);
                    traced = 0;
                }
                else if (event.key.key == SDLK_C)
                    traced = (traced + 1) % (int)meshes[current].verts.size();
                else if (event.key.key == SDLK_SPACE) spinning = !spinning;
                else if (event.key.key == SDLK_R)
                {
                    yaw = 0.6f;
                    pitch = 0.4f;
                    dist = 5.0f;
                    spin = 0.0f;
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

        // The mesh spins about y, and more slowly about x.
        if (spinning) spin += SPIN_SPEED * dt;
        Mat4 model = Mat4::rotationY(spin) * Mat4::rotationX(spin * 0.4f);

        // The eye sits on a sphere around the middle, at the camera's yaw,
        // pitch and distance, and looks at the middle. Then one matrix does
        // the model, the view and the projection.
        Vec3 eye = { dist * std::cos(pitch) * std::sin(yaw),
                     dist * std::sin(pitch),
                     dist * std::cos(pitch) * std::cos(yaw) };
        const Vec3 MIDDLE = { 0.0f, 0.0f, 0.0f };
        const Vec3 UP = { 0.0f, 1.0f, 0.0f };
        Mat4 view = Mat4::lookAt(eye, MIDDLE, UP);
        Mat4 mvp = proj * view * model;

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);

        // Every corner, once: into clip space by the one matrix.
        const Mesh& mesh = meshes[current];
        std::vector<Vec4> clip(mesh.verts.size());
        for (size_t i = 0; i < mesh.verts.size(); ++i)
            clip[i] = mvp.apply4(mesh.verts[i]);

        // Every edge, once: clipped at the near plane, divided and put on the
        // window. Its shade comes from its depth, w, against the middle's,
        // which is the camera's distance: nearer edges are brighter. The
        // edges drawn and the edges cut at the near plane are counted.
        int drawn = 0;
        int cut = 0;
        for (const Edge& e : edges)
        {
            Vec4 a = clip[e.a];
            Vec4 b = clip[e.b];
            bool whole = a.z + a.w >= 0.0f && b.z + b.w >= 0.0f;
            if (!clipAtNear(a, b)) continue;
            ++drawn;
            if (!whole) ++cut;
            float depth = (a.w + b.w) * 0.5f - dist;
            float shade = SDL_clamp(0.62f - depth * 0.22f, 0.3f, 1.0f);
            SDL_SetRenderDrawColor(renderer, (Uint8)(110 * shade),
                                   (Uint8)(175 * shade), (Uint8)(240 * shade),
                                   255);
            Vec2 pa = toPixels(a);
            Vec2 pb = toPixels(b);
            SDL_RenderLine(renderer, pa.x, pa.y, pb.x, pb.y);
        }

        // Strips of the background's color for the readout and the controls,
        // so that the mesh's edges don't show through the text.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_FRect top = { 0.0f, 0.0f, (float)WINDOW_WIDTH, 124.0f };
        SDL_FRect bottom = { 0.0f, WINDOW_HEIGHT - 36.0f, (float)WINDOW_WIDTH,
                             36.0f };
        SDL_RenderFillRect(renderer, &top);
        SDL_RenderFillRect(renderer, &bottom);

        // The readout: the mesh, its counts, the eye, and the edges drawn.
        drawText(renderer, font,
                 mesh.name + ": " + std::to_string(mesh.verts.size()) +
                 " corners, " + std::to_string(mesh.faces.size()) +
                 " faces, " + std::to_string(edges.size()) + " edges",
                 12, 10, TEXT);
        drawText(renderer, font,
                 "eye " + vecText(eye) + ", " + fmt2(dist) + " from the middle",
                 12, 32, TEXT);
        drawText(renderer, font,
                 std::to_string(mesh.verts.size()) + " corners transformed, " +
                 std::to_string(drawn) + " edges drawn, " +
                 std::to_string(cut) + " cut", 12, 54, TEXT);

        // The traced corner, stage by stage: the model and the view take it
        // to view space, and the projection to clip space. The divide gives
        // NDC, and the stretch its pixel, where an amber dot marks it, unless
        // it's short of the near plane.
        Vec3 corner = mesh.verts[traced];
        Vec3 inView = (view * model).apply(corner);
        Vec4 c = proj.apply4(inView);
        drawText(renderer, font,
                 "corner " + std::to_string(traced) + " " + vecText(corner),
                 560, 10, AMBER);
        drawText(renderer, font, "view   " + vecText(inView), 560, 32, TEXT);
        drawText(renderer, font,
                 "clip   (" + fmt2(c.x) + ", " + fmt2(c.y) + ", " + fmt2(c.z) +
                 ", " + fmt2(c.w) + ")", 560, 54, TEXT);
        if (c.z + c.w >= 0.0f)
        {
            Vec3 ndc = { c.x / c.w, c.y / c.w, c.z / c.w };
            Vec2 pixel = toPixels(c);
            drawText(renderer, font, "NDC    " + vecText(ndc), 560, 76, TEXT);
            drawText(renderer, font,
                     "pixel  (" + std::to_string((int)std::lround(pixel.x)) +
                     ", " + std::to_string((int)std::lround(pixel.y)) + ")",
                     560, 98, TEXT);
            setColor(renderer, AMBER);
            fillCircle(renderer, pixel, 4.0f);
        }
        else
            drawText(renderer, font, "short of the near plane", 560, 76, TEXT);
        drawText(renderer, font,
                 "[Drag] orbit   [Wheel] in and out   [N] mesh   [C] corner   "
                 "[Space] spin   [R] reset   [Esc] quit",
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
