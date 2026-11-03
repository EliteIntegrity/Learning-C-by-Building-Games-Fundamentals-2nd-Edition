// Camera - the Chapter 27 demo (the 2D camera) for
// Learning C++ by Building Games: Fundamentals
//
// A camera over a world of tiles, larger than the window. The view, the
// matrix that takes the world onto the window, is the inverse of the
// camera's place in the world, and its own inverse takes the window back
// into the world: to pan, to pick the tile under a click, and to draw only
// the tiles the window can show. Drag or use the arrows to pan, the wheel
// to zoom, and Q and E to turn. F follows a wandering rover, and a
// right-click picks a tile.

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <cmath>
#include <string>
#include <vector>
#include "Vec2.h"
#include "Mat3.h"

constexpr int WINDOW_WIDTH  = 1000;
constexpr int WINDOW_HEIGHT = 700;

constexpr SDL_Color TEXT = { 235, 238, 242, 255 };
constexpr SDL_Color GREY = { 90, 100, 112, 255 };
constexpr SDL_Color AMBER = { 232, 89, 12, 255 };
constexpr SDL_Color ROVER = { 220, 80, 80, 255 };
constexpr SDL_Color EDGE = { 120, 130, 145, 255 };

// The world: TILES by TILES tiles, each TILE units across.
constexpr int TILES = 40;
constexpr float TILE = 64.0f;
constexpr float WORLD = TILES * TILE;

// How fast the arrows pan, in pixels on the window a second; how fast Q
// and E turn, in radians a second; how much one notch of the wheel zooms,
// and the zoom's limits. Then the dead zone's half width and half height,
// in pixels, and how fast the camera closes on its goal when following.
constexpr float PAN_SPEED = 300.0f;
constexpr float TURN_SPEED = 1.2f;
constexpr float ZOOM_STEP = 1.12f;
constexpr float MIN_ZOOM = 0.2f;
constexpr float MAX_ZOOM = 5.0f;
constexpr float DEAD_X = 150.0f;
constexpr float DEAD_Y = 110.0f;
constexpr float FOLLOW_RATE = 12.0f;

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

// The view: the matrix that takes the world onto the window. The camera's
// place in the world is T(camPos) R(camRot) S(1 / zoom), and the view is
// its inverse, its parts undone in reverse order: move by -camPos, turn by
// -camRot, and scale by zoom. Then a move puts the camera's center in the
// middle of the window.
Mat3 makeView(Vec2 camPos, float camRot, float zoom)
{
    return Mat3::translation(WINDOW_WIDTH / 2.0f, WINDOW_HEIGHT / 2.0f) *
           Mat3::scale(zoom, zoom) * Mat3::rotation(-camRot) *
           Mat3::translation(-camPos.x, -camPos.y);
}

// The four corners of tile (tx, ty), taken from the world onto the window
// by the view: top left, top right, bottom right and bottom left.
void tileCorners(const Mat3& view, int tx, int ty, Vec2 out[4])
{
    Vec2 w = { tx * TILE, ty * TILE };
    out[0] = view.apply(w);
    out[1] = view.apply(w + Vec2{ TILE, 0.0f });
    out[2] = view.apply(w + Vec2{ TILE, TILE });
    out[3] = view.apply(w + Vec2{ 0.0f, TILE });
}

// How far a point on the window is outside the dead zone, the box around
// the window's middle, each way: 0 while it's inside.
Vec2 beyondDeadZone(Vec2 s)
{
    Vec2 middle = { WINDOW_WIDTH / 2.0f, WINDOW_HEIGHT / 2.0f };
    Vec2 over = { 0.0f, 0.0f };
    if (s.x < middle.x - DEAD_X) over.x = s.x - (middle.x - DEAD_X);
    if (s.x > middle.x + DEAD_X) over.x = s.x - (middle.x + DEAD_X);
    if (s.y < middle.y - DEAD_Y) over.y = s.y - (middle.y - DEAD_Y);
    if (s.y > middle.y + DEAD_Y) over.y = s.y - (middle.y + DEAD_Y);
    return over;
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
        SDL_CreateWindow("Camera", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The camera: where it is in the world, its turn, and its zoom, which
    // is how many pixels on the window each unit of the world covers.
    const Vec2 HOME = { WORLD / 2.0f, WORLD / 2.0f };
    Vec2 camPos = HOME;
    float camRot = 0.0f;
    float zoom = 1.0f;

    // The view and its inverse, kept from one frame to the next, so that a
    // click is read against the world the window shows.
    Mat3 view = makeView(camPos, camRot, zoom);
    Mat3 screenToWorld = inverse(view);

    // The drag, and where the mouse was last frame; the following; the tile
    // picked, or -1; the rover and its clock; and the tiles' triangles.
    bool dragging = false;
    Vec2 prevMouse = { 0.0f, 0.0f };
    bool follow = false;
    int pickedX = -1;
    int pickedY = -1;
    Vec2 rover = HOME;
    float roverTime = 0.0f;
    std::vector<SDL_Vertex> verts;
    std::vector<int> indices;

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
                else if (event.key.key == SDLK_F) follow = !follow;
                else if (event.key.key == SDLK_R)
                {
                    camPos = HOME;
                    camRot = 0.0f;
                    zoom = 1.0f;
                    follow = false;
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_WHEEL)
            {
                // Each notch multiplies the zoom, up or down.
                zoom *= std::pow(ZOOM_STEP, event.wheel.y);
                zoom = SDL_clamp(zoom, MIN_ZOOM, MAX_ZOOM);
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            {
                Vec2 press = { event.button.x, event.button.y };
                if (event.button.button == SDL_BUTTON_LEFT)
                {
                    dragging = true;
                    prevMouse = press;
                }
                else if (event.button.button == SDL_BUTTON_RIGHT)
                {
                    // The tile under the click: the click, taken into the
                    // world, divided by the tile's size and rounded down.
                    Vec2 w = screenToWorld.apply(press);
                    pickedX = (int)std::floor(w.x / TILE);
                    pickedY = (int)std::floor(w.y / TILE);
                    if (pickedX < 0 || pickedX >= TILES ||
                        pickedY < 0 || pickedY >= TILES)
                    {
                        pickedX = -1;
                        pickedY = -1;
                    }
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) dragging = false;
        }

        // The mouse's position, read once a frame.
        float mx, my;
        SDL_GetMouseState(&mx, &my);

        // Q and E turn the camera. The rover wanders on a slow, looping path.
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_Q]) camRot = wrapAngle(camRot - TURN_SPEED * dt);
        if (keys[SDL_SCANCODE_E]) camRot = wrapAngle(camRot + TURN_SPEED * dt);
        roverTime += dt;
        rover = HOME + Vec2{ std::cos(roverTime * 0.5f) * 700.0f,
                             std::sin(roverTime * 0.35f) * 520.0f };

        // The view and its inverse, for the camera as it now stands.
        view = makeView(camPos, camRot, zoom);
        screenToWorld = inverse(view);

        // Panning, dragging and following each start as a move on the
        // window, which applyDir takes into the world: turned with the
        // camera, and divided by its zoom.
        Vec2 pan = { 0.0f, 0.0f };
        if (keys[SDL_SCANCODE_LEFT]) pan.x -= PAN_SPEED * dt;
        if (keys[SDL_SCANCODE_RIGHT]) pan.x += PAN_SPEED * dt;
        if (keys[SDL_SCANCODE_UP]) pan.y -= PAN_SPEED * dt;
        if (keys[SDL_SCANCODE_DOWN]) pan.y += PAN_SPEED * dt;
        camPos += screenToWorld.applyDir(pan);
        Vec2 mouse = { mx, my };
        if (dragging) camPos -= screenToWorld.applyDir(mouse - prevMouse);
        prevMouse = mouse;

        // Following: the dead zone decides whether the camera moves, by how
        // far the rover is outside it. The rate decides how fast: the share
        // of the way to its goal that the camera moves this frame is the
        // same each second, whatever the frame rate (Chapter 8).
        if (follow)
        {
            Vec2 over = beyondDeadZone(view.apply(rover));
            float share = 1.0f - std::exp(-FOLLOW_RATE * dt);
            camPos += screenToWorld.applyDir(over) * share;
        }

        // The view and its inverse again, after the camera moved. Then how
        // far outside the dead zone the rover is drawn, for the readout.
        view = makeView(camPos, camRot, zoom);
        screenToWorld = inverse(view);
        float outside = beyondDeadZone(view.apply(rover)).length();

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);

        // Only the tiles the window can show: its four corners, taken into
        // the world, give a box of tiles that holds all of it.
        const Vec2 WINDOW_CORNER[4] = {
            { 0.0f, 0.0f }, { (float)WINDOW_WIDTH, 0.0f },
            { (float)WINDOW_WIDTH, (float)WINDOW_HEIGHT },
            { 0.0f, (float)WINDOW_HEIGHT } };
        Vec2 low = screenToWorld.apply(WINDOW_CORNER[0]);
        Vec2 high = low;
        for (int i = 1; i < 4; ++i)
        {
            Vec2 w = screenToWorld.apply(WINDOW_CORNER[i]);
            low = { SDL_min(low.x, w.x), SDL_min(low.y, w.y) };
            high = { SDL_max(high.x, w.x), SDL_max(high.y, w.y) };
        }
        int tx0 = SDL_clamp((int)std::floor(low.x / TILE), 0, TILES - 1);
        int tx1 = SDL_clamp((int)std::floor(high.x / TILE), 0, TILES - 1);
        int ty0 = SDL_clamp((int)std::floor(low.y / TILE), 0, TILES - 1);
        int ty1 = SDL_clamp((int)std::floor(high.y / TILE), 0, TILES - 1);

        // Those tiles, as a checkerboard of two colors, all filled by one call:
        // four corners for each tile, and two triangles.
        verts.clear();
        indices.clear();
        for (int ty = ty0; ty <= ty1; ++ty)
            for (int tx = tx0; tx <= tx1; ++tx)
            {
                SDL_FColor color = ((tx + ty) % 2 == 1)
                    ? SDL_FColor{ 0.20f, 0.24f, 0.32f, 1.0f }
                    : SDL_FColor{ 0.14f, 0.17f, 0.23f, 1.0f };
                Vec2 c[4];
                tileCorners(view, tx, ty, c);
                int first = (int)verts.size();
                for (int i = 0; i < 4; ++i)
                    verts.push_back({ { c[i].x, c[i].y }, color, { 0, 0 } });
                indices.insert(indices.end(), { first, first + 1, first + 2,
                                                first, first + 2, first + 3 });
            }
        SDL_RenderGeometry(renderer, nullptr, verts.data(), (int)verts.size(),
                           indices.data(), (int)indices.size());

        // The picked tile, outlined, and the rover, sized with the zoom.
        if (pickedX >= 0)
        {
            Vec2 c[4];
            tileCorners(view, pickedX, pickedY, c);
            setColor(renderer, AMBER);
            for (int i = 0; i < 4; ++i)
                SDL_RenderLine(renderer, c[i].x, c[i].y,
                               c[(i + 1) % 4].x, c[(i + 1) % 4].y);
        }
        setColor(renderer, ROVER);
        fillCircle(renderer, view.apply(rover), 14.0f * zoom);

        // The dead zone, while following. It belongs to the window, not to
        // the world, so it's drawn without the view.
        if (follow)
        {
            setColor(renderer, EDGE);
            SDL_FRect box = { WINDOW_WIDTH / 2.0f - DEAD_X,
                              WINDOW_HEIGHT / 2.0f - DEAD_Y,
                              2.0f * DEAD_X, 2.0f * DEAD_Y };
            SDL_RenderRect(renderer, &box);
        }

        // Strips of the background's color for the readout and the controls,
        // so that the tiles don't show through the text. Like the dead zone,
        // they belong to the window, and are drawn without the view.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_FRect top = { 0.0f, 0.0f, (float)WINDOW_WIDTH, 66.0f };
        SDL_FRect bottom = { 0.0f, WINDOW_HEIGHT - 52.0f, (float)WINDOW_WIDTH,
                             52.0f };
        SDL_RenderFillRect(renderer, &top);
        SDL_RenderFillRect(renderer, &bottom);

        // The readout: the camera, the tiles drawn and the tile picked, and
        // how far outside the dead zone the rover is. Then the controls.
        int drawn = (tx1 - tx0 + 1) * (ty1 - ty0 + 1);
        drawText(renderer, font,
                 "camera (" + std::to_string((int)std::lround(camPos.x)) +
                 ", " + std::to_string((int)std::lround(camPos.y)) +
                 ")   zoom " + fmt2(zoom) + "   turn " +
                 std::to_string((int)std::lround(camRot * 180.0f / PI)) +
                 " degrees", 12, 8, TEXT);
        std::string picked = "none";
        if (pickedX >= 0)
            picked = "(" + std::to_string(pickedX) + ", " +
                     std::to_string(pickedY) + ")";
        drawText(renderer, font,
                 "tiles drawn " + std::to_string(drawn) + " of " +
                 std::to_string(TILES * TILES) + "   picked tile " + picked,
                 12, 26, TEXT);
        std::string beyond = "follow off";
        if (follow)
            beyond = "following: the rover is " +
                     std::to_string((int)std::lround(outside)) +
                     " pixels outside the dead zone";
        drawText(renderer, font, beyond, 12, 44, TEXT);
        drawText(renderer, font,
                 "[Drag] or [Arrows] pan   [Wheel] zoom   [Q] [E] turn   "
                 "[F] follow the rover", 12, WINDOW_HEIGHT - 46, GREY);
        drawText(renderer, font,
                 "[Right-click] pick a tile   [R] reset   [Esc] quit",
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
