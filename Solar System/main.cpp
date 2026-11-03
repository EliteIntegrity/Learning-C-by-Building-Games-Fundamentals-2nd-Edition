// Solar System - the Chapter 25 demo (transforms and the scene graph) for
// Learning C++ by Building Games: Fundamentals
//
// A sun, two planets and their moons, as a scene graph. Each body keeps
// only its transform relative to its parent, and its place in the window
// is worked out from its parent's, every frame. Drag the sun, and the whole
// system follows. S switches between children that ride their parent's
// spin and children that hang on a pivot that doesn't spin. A click picks
// a body by taking the mouse into the body's own space. Space pauses, Up
// and Down change the speed, Left and Right scale the sun, and R resets.

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
constexpr SDL_Color RING = { 50, 55, 66, 255 };
constexpr SDL_Color LINK = { 70, 77, 90, 255 };
constexpr SDL_Color TICK = { 245, 245, 245, 255 };
constexpr SDL_Color AMBER = { 232, 89, 12, 255 };

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

// A circle's outline: 72 short lines around its edge, each from one point
// on the circle to the next, a 72nd of a turn on.
void drawCircleOutline(SDL_Renderer* renderer, Vec2 center, float radius,
                       SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    Vec2 prev = center + Vec2{ radius, 0.0f };
    for (int i = 1; i <= 72; ++i)
    {
        float angle = (float)i / 72.0f * 2.0f * PI;
        Vec2 p = center + Vec2::fromAngle(angle, radius);
        SDL_RenderLine(renderer, prev.x, prev.y, p.x, p.y);
        prev = p;
    }
}

// Set the color the renderer draws with, from an SDL_Color.
void setColor(SDL_Renderer* renderer, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

// A transform: a place, a turn and a size. It places a point by scaling
// it, then rotating it, then moving it by pos.
struct Transform
{
    Vec2 pos;
    float rot = 0.0f;
    float scale = 1.0f;
};

// A point in a transform's own space, taken out into the space around it:
// scaled, then rotated, then moved.
Vec2 toWorld(const Transform& t, Vec2 local)
{
    return t.pos + (local * t.scale).rotated(t.rot);
}

// The inverse: a point in the space around a transform, taken into the
// transform's own space. It undoes the steps in reverse order: the move,
// then the turn, then the scale.
Vec2 toLocal(const Transform& t, Vec2 world)
{
    return (world - t.pos).rotated(-t.rot) / t.scale;
}

// A child's world transform, from its parent's and its own local one. Its
// place is its local position, taken into the world by its parent. Its
// turn adds to its parent's, and its scale multiplies its parent's.
Transform combine(const Transform& parent, const Transform& local)
{
    return { toWorld(parent, local.pos), parent.rot + local.rot,
             parent.scale * local.scale };
}

// A body of the solar system. Its orbit around its parent gives its local
// position, its spin gives its turn, and its scale its size.
struct Body
{
    int parent;          // The parent's index in the list, or -1 for the root.
    float orbitRadius;   // How far out from its parent it orbits,
    float orbitAngle;    // the angle it's at, in radians,
    float orbitSpeed;    // and how fast that angle grows, a second.
    float spinAngle;     // Its own turn, and how fast that grows.
    float spinSpeed;
    float scale;         // Its size, as a multiple of its parent's.
    float radius;        // How big it's drawn, in its own units.
    SDL_Color color;
    const char* name;
};

// How fast body i goes around its parent, as the window shows it, in
// radians a second of the demo's time: its own orbit speed, and, while
// children inherit spin, the spin of every body above it.
float screenSpeed(const std::vector<Body>& body, int i, bool inherit)
{
    float speed = body[i].orbitSpeed;
    if (inherit)
        for (int p = body[i].parent; p >= 0; p = body[p].parent)
            speed += body[p].spinSpeed;
    return speed;
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
        SDL_CreateWindow("Solar System", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The scene graph: each body's parent comes before it in the list, so
    // one pass down the list always finds a parent's transform worked out.
    std::vector<Body> body = {
        { -1, 0.0f, 0.0f, 0.0f, 0.0f, 0.2f, 1.0f, 34.0f,
          { 240, 190, 60, 255 }, "Sun" },
        { 0, 150.0f, 0.0f, 0.5f, 0.0f, 1.0f, 1.0f, 18.0f,
          { 70, 130, 220, 255 }, "Planet A" },
        { 1, 46.0f, 0.0f, 1.8f, 0.0f, 1.5f, 1.0f, 8.0f,
          { 150, 160, 175, 255 }, "Moon A" },
        { 0, 220.0f, 1.0f, 0.3f, 0.0f, 0.8f, 1.0f, 22.0f,
          { 60, 170, 90, 255 }, "Planet B" },
        { 3, 50.0f, 0.0f, 1.2f, 0.0f, 1.3f, 1.0f, 9.0f,
          { 220, 80, 80, 255 }, "Moon B" },
    };

    // Each body's pivot, its world transform without its own spin, and its
    // world transform, which turns the pivot by the spin.
    std::vector<Transform> pivot(body.size());
    std::vector<Transform> world(body.size());

    // Where the sun sits, and where R puts it back. Then the speed of time,
    // and whether it's paused; whether children inherit their parent's
    // spin; the body selected; and whether the sun is being dragged, with
    // its offset from the mouse when it was grabbed.
    const Vec2 SUN_HOME = { 500.0f, 360.0f };
    Vec2 sunPos = SUN_HOME;
    float timeScale = 1.0f;
    bool paused = false;
    bool inherit = true;
    int selected = 2;
    bool dragging = false;
    Vec2 grab;

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
                else if (event.key.key == SDLK_SPACE) paused = !paused;
                else if (event.key.key == SDLK_S) inherit = !inherit;
                else if (event.key.key == SDLK_UP)
                    timeScale = SDL_min(timeScale + 0.25f, 4.0f);
                else if (event.key.key == SDLK_DOWN)
                    timeScale = SDL_max(timeScale - 0.25f, 0.25f);
                else if (event.key.key == SDLK_RIGHT)
                    body[0].scale = SDL_min(body[0].scale + 0.1f, 2.0f);
                else if (event.key.key == SDLK_LEFT)
                    body[0].scale = SDL_max(body[0].scale - 0.1f, 0.4f);
                else if (event.key.key == SDLK_R)
                {
                    sunPos = SUN_HOME;
                    body[0].scale = 1.0f;
                    timeScale = 1.0f;
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                     event.button.button == SDL_BUTTON_LEFT)
            {
                // The body under the mouse, tested in its own space, where
                // its radius is its own. The last drawn is on top, so the
                // test runs from the end of the list.
                Vec2 press = { event.button.x, event.button.y };
                for (int i = (int)body.size() - 1; i >= 0; --i)
                {
                    if (toLocal(world[i], press).length() <= body[i].radius)
                    {
                        selected = i;
                        dragging = (i == 0);
                        grab = sunPos - press;
                        break;
                    }
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) dragging = false;
        }

        // The mouse's position, read once a frame.
        float mx, my;
        SDL_GetMouseState(&mx, &my);

        // A dragged sun follows the mouse, with the point grabbed under it.
        if (dragging) sunPos = Vec2{ mx, my } + grab;

        // Each body's angles grow at their own speeds. Nothing here touches
        // a position in the window.
        if (!paused)
        {
            for (Body& b : body)
            {
                b.orbitAngle += b.orbitSpeed * dt * timeScale;
                b.spinAngle += b.spinSpeed * dt * timeScale;
            }
        }

        // The transforms, worked out afresh. The sun's pivot is its local
        // transform, since its parent is the window. Each other body's local
        // transform puts it on its orbit, and combines with its parent's
        // world transform, spin and all, or with S, its parent's pivot.
        // Then a spin, a transform of its own, turns the pivot.
        for (size_t i = 0; i < body.size(); ++i)
        {
            const Body& b = body[i];
            Transform local = { Vec2::fromAngle(b.orbitAngle, b.orbitRadius),
                                0.0f, b.scale };
            if (b.parent < 0) pivot[i] = { sunPos, 0.0f, b.scale };
            else if (inherit) pivot[i] = combine(world[b.parent], local);
            else pivot[i] = combine(pivot[b.parent], local);
            Transform spin = { { 0.0f, 0.0f }, b.spinAngle, 1.0f };
            world[i] = combine(pivot[i], spin);
        }

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);

        // Each orbit, a ring around the parent, scaled with it, and the scene
        // graph's links, a line from each parent to its child.
        for (size_t i = 0; i < body.size(); ++i)
        {
            if (body[i].parent < 0) continue;
            Transform parent = world[body[i].parent];
            drawCircleOutline(renderer, parent.pos,
                              body[i].orbitRadius * parent.scale, RING);
            setColor(renderer, LINK);
            SDL_RenderLine(renderer, parent.pos.x, parent.pos.y,
                           world[i].pos.x, world[i].pos.y);
        }

        // The bodies, each with a tick along its own x axis, from its center
        // to its edge, which shows its turn. The one selected is ringed.
        for (size_t i = 0; i < body.size(); ++i)
        {
            Vec2 center = world[i].pos;
            float radius = body[i].radius * world[i].scale;
            setColor(renderer, body[i].color);
            fillCircle(renderer, center, radius);
            Vec2 edge = toWorld(world[i], { body[i].radius, 0.0f });
            setColor(renderer, TICK);
            SDL_RenderLine(renderer, center.x, center.y, edge.x, edge.y);
            if ((int)i == selected)
                drawCircleOutline(renderer, center, radius + 5.0f, AMBER);
            drawText(renderer, font, body[i].name, center.x + radius + 6.0f,
                     center.y - 9.0f, TEXT);
        }

        // The readout: the time, and how children hang on their parents.
        // Then the body selected: how fast it goes around its parent, as the
        // window shows it, and the mouse in its own space. Then the controls.
        drawText(renderer, font,
                 "time x" + fmt2(timeScale) + (paused ? ", paused" : "") +
                 (inherit ? "   children ride their parent's spin"
                          : "   children hang on a pivot that doesn't spin"),
                 12, 8, TEXT);
        const Body& b = body[selected];
        std::string lap = std::string(b.name) + ": the root, in the window";
        if (b.parent >= 0)
        {
            float speed = screenSpeed(body, selected, inherit) * timeScale;
            lap = std::string(b.name) + " around " + body[b.parent].name +
                  ": " + fmt2(speed) + " rad/s, a lap every " +
                  fmt2(2.0f * PI / speed) + " s";
        }
        drawText(renderer, font, lap, 12, 26, TEXT);
        Vec2 mouse = toLocal(world[selected], { mx, my });
        drawText(renderer, font,
                 std::string(b.name) + "'s space: the mouse at (" +
                 fmt2(mouse.x) + ", " + fmt2(mouse.y) + "), " +
                 fmt2(mouse.length()) + " from its center, " +
                 (mouse.length() <= b.radius ? "inside" : "outside"),
                 12, 44, GREY);
        drawText(renderer, font,
                 "[Click] select a body   [Drag] the sun   "
                 "[S] spin inherited or not   [Space] pause",
                 12, WINDOW_HEIGHT - 46, GREY);
        drawText(renderer, font,
                 "[Up] [Down] speed   [Left] [Right] the sun's scale   "
                 "[R] reset   [Esc] quit", 12, WINDOW_HEIGHT - 26, GREY);

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
