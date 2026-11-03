// Path Designer - the Chapter 14 demo (curves and paths) for
// Learning C++ by Building Games: Fundamentals
//
// A path made of cubic Bezier pieces, with points to drag. In Bezier mode,
// four control points shape one curve, and faint lines show de Casteljau's
// lerps finding the point. C switches to a Catmull-Rom loop through six
// waypoints, whose handles are worked out from their neighbors. A dot
// glides along at a steady speed, found from an arc-length table, with its
// tangent and normal. A ghost dot, moved by steady steps of the curve's
// parameter instead, shows why the table is needed.

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "Vec2.h"

constexpr int WINDOW_WIDTH  = 1000;
constexpr int WINDOW_HEIGHT = 700;

constexpr SDL_Color TEXT = { 235, 238, 242, 255 };
constexpr SDL_Color GREY = { 90, 100, 112, 255 };
constexpr SDL_Color BLUE = { 47, 111, 235, 255 };
constexpr SDL_Color AMBER = { 232, 89, 12, 255 };
constexpr SDL_Color GREEN = { 47, 158, 68, 255 };
constexpr SDL_Color FAINT = { 70, 78, 90, 255 };
constexpr SDL_Color SILVER = { 150, 158, 170, 255 };

// The steady dot's speed, in pixels a second, and how many samples the
// arc-length table takes on each piece of the path.
constexpr float SPEED = 240.0f;
constexpr int SAMPLES = 120;

// A cubic Bezier: its four control points. It starts at the anchor p0, is
// pulled toward the handles p1 and p2, and ends at the anchor p3.
struct Cubic
{
    Vec2 p0;
    Vec2 p1;
    Vec2 p2;
    Vec2 p3;
};

// A path of cubic pieces, each starting where the one before it ends, and
// its arc-length table: the points at SAMPLES equal steps of t on each
// piece, and how far along the path each point is.
struct Path
{
    std::vector<Cubic> pieces;
    std::vector<Vec2> points;
    std::vector<float> distances;
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

// A faint grid, with a line every 50 pixels, to read positions against.
void drawGrid(SDL_Renderer* renderer)
{
    SDL_SetRenderDrawColor(renderer, 38, 42, 50, 255);
    for (int x = 0; x < WINDOW_WIDTH; x += 50)
        SDL_RenderLine(renderer, (float)x, 0, (float)x, WINDOW_HEIGHT);
    for (int y = 0; y < WINDOW_HEIGHT; y += 50)
        SDL_RenderLine(renderer, 0, (float)y, WINDOW_WIDTH, (float)y);
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

// The point at t on a cubic Bezier, by de Casteljau's algorithm: three
// lerps between the control points, two between those, and one more.
Vec2 bezier(const Cubic& curve, float t)
{
    Vec2 a = lerp(curve.p0, curve.p1, t);
    Vec2 b = lerp(curve.p1, curve.p2, t);
    Vec2 c = lerp(curve.p2, curve.p3, t);
    Vec2 d = lerp(a, b, t);
    Vec2 e = lerp(b, c, t);
    return lerp(d, e, t);
}

// The velocity of the point at t on a cubic Bezier, in pixels per unit of
// t: three times de Casteljau's last segment, from d to e. Its direction
// is the way the curve heads, and its length is how fast t moves along it.
Vec2 bezierVelocity(const Cubic& curve, float t)
{
    Vec2 a = lerp(curve.p0, curve.p1, t);
    Vec2 b = lerp(curve.p1, curve.p2, t);
    Vec2 c = lerp(curve.p2, curve.p3, t);
    return (lerp(b, c, t) - lerp(a, b, t)) * 3.0f;
}

// The Catmull-Rom piece from p1 to p2, as a cubic Bezier. Its velocity at
// each end is half the step between that end's neighbors, so each handle
// is a sixth of that step from its anchor.
Cubic catmullRomPiece(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3)
{
    return { p1, p1 + (p2 - p0) / 6.0f, p2 - (p3 - p1) / 6.0f, p2 };
}

// A closed Catmull-Rom loop through some waypoints: a piece from each one
// to the next, and from the last back to the first, so each has neighbors.
std::vector<Cubic> catmullRomLoop(const std::vector<Vec2>& waypoints)
{
    size_t n = waypoints.size();
    std::vector<Cubic> pieces;
    for (size_t i = 0; i < n; ++i)
    {
        pieces.push_back(catmullRomPiece(waypoints[(i + n - 1) % n],
                                         waypoints[i],
                                         waypoints[(i + 1) % n],
                                         waypoints[(i + 2) % n]));
    }
    return pieces;
}

// A path's parameter, u, runs from 0 to its number of pieces: its whole
// part picks the piece, and the rest is t on that piece. These two give
// the point, and the velocity, at u.
Vec2 pointAt(const Path& path, float u)
{
    int last = (int)path.pieces.size() - 1;
    int i = std::clamp((int)u, 0, last);
    return bezier(path.pieces[i], u - (float)i);
}

Vec2 velocityAt(const Path& path, float u)
{
    int last = (int)path.pieces.size() - 1;
    int i = std::clamp((int)u, 0, last);
    return bezierVelocity(path.pieces[i], u - (float)i);
}

// The arc-length table: the path's points at SAMPLES equal steps of t on
// each piece, and the distance along the path to each, adding up the
// straight hops from one point to the next.
void buildTable(Path& path)
{
    int count = (int)path.pieces.size() * SAMPLES;
    path.points = { pointAt(path, 0.0f) };
    path.distances = { 0.0f };
    for (int k = 1; k <= count; ++k)
    {
        Vec2 p = pointAt(path, (float)k / SAMPLES);
        float hop = distance(path.points.back(), p);
        path.distances.push_back(path.distances.back() + hop);
        path.points.push_back(p);
    }
}

// The path's u at a distance s along it, from the table. A binary search,
// std::lower_bound, finds the first sample at least s along. The path is
// taken as straight between that sample and the one before it, so u is
// the same share of the way between their values of u.
float uAtDistance(const Path& path, float s)
{
    const std::vector<float>& d = path.distances;
    auto it = std::lower_bound(d.begin(), d.end(), s);
    if (it == d.begin()) return 0.0f;
    if (it == d.end()) return (float)path.pieces.size();
    size_t k = (size_t)(it - d.begin());
    float span = d[k] - d[k - 1];
    float share = (span > 0.0f) ? (s - d[k - 1]) / span : 0.0f;
    return ((float)(k - 1) + share) / SAMPLES;
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
        SDL_CreateWindow("Path Designer", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The points to drag: Bezier mode's four control points, and
    // Catmull-Rom mode's six waypoints, which make a loop.
    Vec2 controls[4] = { { 150, 520 }, { 340, 130 }, { 660, 130 },
                         { 870, 500 } };
    std::vector<Vec2> waypoints = { { 190, 330 }, { 330, 140 }, { 640, 190 },
                                    { 840, 330 }, { 660, 560 }, { 330, 520 } };
    bool catmullRom = false;

    // Pointers to each mode's points, so that one loop can grab any of
    // them, as Shape Gallery's handles do. Neither list ever grows, so the
    // pointers stay good.
    std::vector<Vec2*> controlHandles;
    for (Vec2& p : controls) controlHandles.push_back(&p);
    std::vector<Vec2*> waypointHandles;
    for (Vec2& p : waypoints) waypointHandles.push_back(&p);
    Vec2* grabbed = nullptr;

    // The path, rebuilt with its table only when a point moves or the mode
    // changes, and the steady dot's distance along it.
    Path path;
    bool rebuild = true;
    float traveled = 0.0f;
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

        // Events: quitting, and the keys and clicks that happen once.
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT) running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN)
            {
                if (event.key.key == SDLK_ESCAPE) running = false;
                else if (event.key.key == SDLK_C)
                {
                    catmullRom = !catmullRom;
                    grabbed = nullptr;
                    rebuild = true;
                }
                else if (event.key.key == SDLK_SPACE) paused = !paused;
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                     event.button.button == SDL_BUTTON_LEFT)
            {
                const std::vector<Vec2*>& handles =
                    catmullRom ? waypointHandles : controlHandles;
                Vec2 press = { event.button.x, event.button.y };
                for (Vec2* h : handles)
                {
                    if (distance(press, *h) < 15.0f)
                    {
                        grabbed = h;
                        break;
                    }
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) grabbed = nullptr;
        }

        // The mouse's position, read once a frame.
        float mx, my;
        SDL_GetMouseState(&mx, &my);

        // A grabbed point follows the mouse, so the path must be rebuilt.
        if (grabbed)
        {
            *grabbed = { mx, my };
            rebuild = true;
        }

        // The path's pieces, and its table, when something has changed.
        if (rebuild)
        {
            if (catmullRom)
                path.pieces = catmullRomLoop(waypoints);
            else
                path.pieces = { { controls[0], controls[1], controls[2],
                                  controls[3] } };
            buildTable(path);
            rebuild = false;
        }

        // The steady dot goes SPEED pixels a second along the path, and its
        // distance wraps at the path's length, so that it laps. Wrapping the
        // distance itself keeps it from growing without end.
        float total = path.distances.back();
        if (total > 0.0f)
        {
            float step = paused ? 0.0f : SPEED * dt;
            traveled = std::fmod(traveled + step, total);
        }
        float u = uAtDistance(path, traveled);

        // The ghost's u is the same share of the path's pieces as the steady
        // dot's distance is of its length. So the two lap together, but the
        // ghost moves by steady steps of u, not of distance. Its speed is
        // the velocity's length there, times how fast its u moves.
        float pieceCount = (float)path.pieces.size();
        float ghostU = (total > 0.0f) ? traveled / total * pieceCount : 0.0f;
        float uRate = (total > 0.0f) ? pieceCount * SPEED / total : 0.0f;
        float ghostSpeed = velocityAt(path, ghostU).length() * uRate;

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);
        drawGrid(renderer);

        // Each piece's handles, faint: a line from each anchor to its handle,
        // and in Bezier mode, the line between the handles too, which
        // completes the control polygon.
        for (const Cubic& piece : path.pieces)
        {
            setColor(renderer, FAINT);
            SDL_RenderLine(renderer, piece.p0.x, piece.p0.y, piece.p1.x,
                           piece.p1.y);
            SDL_RenderLine(renderer, piece.p2.x, piece.p2.y, piece.p3.x,
                           piece.p3.y);
            if (!catmullRom)
                SDL_RenderLine(renderer, piece.p1.x, piece.p1.y, piece.p2.x,
                               piece.p2.y);
            setColor(renderer, GREY);
            fillCircle(renderer, piece.p1, 3.0f);
            fillCircle(renderer, piece.p2, 3.0f);
        }

        // The path, through the table's points.
        setColor(renderer, BLUE);
        for (size_t k = 1; k < path.points.size(); ++k)
        {
            const Vec2& a = path.points[k - 1];
            const Vec2& b = path.points[k];
            SDL_RenderLine(renderer, a.x, a.y, b.x, b.y);
        }

        // In Bezier mode, de Casteljau's lerps at the steady dot's t: the
        // first row, a to b to c, in grey, and the second, d to e, in silver.
        if (!catmullRom)
        {
            const Cubic& curve = path.pieces[0];
            Vec2 a = lerp(curve.p0, curve.p1, u);
            Vec2 b = lerp(curve.p1, curve.p2, u);
            Vec2 c = lerp(curve.p2, curve.p3, u);
            Vec2 d = lerp(a, b, u);
            Vec2 e = lerp(b, c, u);
            setColor(renderer, GREY);
            SDL_RenderLine(renderer, a.x, a.y, b.x, b.y);
            SDL_RenderLine(renderer, b.x, b.y, c.x, c.y);
            for (Vec2 p : { a, b, c }) fillCircle(renderer, p, 3.0f);
            setColor(renderer, SILVER);
            SDL_RenderLine(renderer, d.x, d.y, e.x, e.y);
            for (Vec2 p : { d, e }) fillCircle(renderer, p, 3.0f);
        }

        // The ghost, a ring moved by steady steps of u.
        drawCircleOutline(renderer, pointAt(path, ghostU), 8.0f, SILVER);

        // The steady dot, with its tangent, the velocity's direction, as a
        // green arrow, and its normal, a quarter turn of that, in amber.
        Vec2 dot = pointAt(path, u);
        Vec2 tangent = velocityAt(path, u).normalized();
        drawArrow(renderer, dot, dot + tangent * 60.0f, GREEN);
        drawArrow(renderer, dot, dot + tangent.perp() * 36.0f, AMBER);
        setColor(renderer, AMBER);
        fillCircle(renderer, dot, 7.0f);

        // The points to drag, named in Bezier mode.
        const std::vector<Vec2*>& handles =
            catmullRom ? waypointHandles : controlHandles;
        for (size_t i = 0; i < handles.size(); ++i)
        {
            setColor(renderer, (handles[i] == grabbed) ? TEXT : AMBER);
            fillCircle(renderer, *handles[i], 6.0f);
            if (!catmullRom)
                drawText(renderer, font, "P" + std::to_string(i),
                         handles[i]->x + 10, handles[i]->y - 8, GREY);
        }

        // The readout: the path and its length, then each dot's u, and its
        // speed on the screen.
        std::string name = catmullRom
            ? "Catmull-Rom loop through " + std::to_string(waypoints.size()) +
              " waypoints"
            : "Bezier curve";
        drawText(renderer, font,
                 name + ": " + std::to_string(std::lround(total)) + " px long",
                 12, 10, TEXT);
        drawText(renderer, font,
                 "steady dot: u = " + fmt2(u) + ", " +
                 std::to_string(std::lround(SPEED)) + " px/s", 12, 30, AMBER);
        drawText(renderer, font,
                 "ghost: u = " + fmt2(ghostU) + ", " +
                 std::to_string(std::lround(ghostSpeed)) + " px/s",
                 12, 50, SILVER);
        drawText(renderer, font,
                 "[Drag] a point   [C] Bezier or Catmull-Rom   [Space] pause"
                 "   [Esc] quit", 12, WINDOW_HEIGHT - 26, GREY);

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
