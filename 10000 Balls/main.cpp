// 10000 Balls - the Chapter 19 demo (spatial partitioning) for
// Learning C++ by Building Games: Fundamentals
//
// Thousands of small balls bounce and collide. Space switches the broad
// phase, which finds the pairs worth testing, between brute force, a
// uniform grid and a quadtree. Up and Down change the count, C pulls the
// balls into a clump, and V shows the grid's cells or the quadtree's
// boxes. The readout gives the pairs tested, and the time the collision
// pass takes.

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>
#include "Vec2.h"

constexpr int WINDOW_WIDTH  = 1000;
constexpr int WINDOW_HEIGHT = 700;

constexpr SDL_Color TEXT = { 235, 238, 242, 255 };
constexpr SDL_Color GREY = { 90, 100, 112, 255 };
constexpr SDL_Color BLUE = { 47, 111, 235, 255 };
constexpr SDL_Color AMBER = { 232, 89, 12, 255 };
constexpr SDL_Color FAINT = { 58, 64, 74, 255 };
constexpr SDL_Color PANEL = { 24, 26, 30, 220 };

// The balls' radius, and the grid's cell size: one diameter, the smallest
// size at which a search of the cells around a ball can be trusted.
constexpr float RADIUS = 3.0f;
constexpr float CELL = 2.0f * RADIUS;

// The quadtree's limits: a box splits when a ball arrives and it already
// holds CAPACITY, unless it's MAX_DEPTH levels down.
constexpr int CAPACITY = 8;
constexpr int MAX_DEPTH = 8;

// How hard the clump pulls the balls toward the middle, in pixels a second,
// every second.
constexpr float PULL = 400.0f;

// The three broad phases.
enum BroadPhase { BRUTE_FORCE, GRID, QUADTREE };
const char* const PHASE_NAMES[] = { "brute force", "uniform grid", "quadtree" };

// An axis-aligned box: its smallest and largest corners. Neither of min's
// coordinates is ever more than max's.
struct AABB
{
    Vec2 min;
    Vec2 max;
};

// A ball: where it is, and its velocity.
struct Ball
{
    Vec2 pos;
    Vec2 vel;
};

// What a collision pass did: the pairs it tested, and how many touched.
struct PassCounts
{
    long checks;
    long contacts;
};

// A uniform grid: its columns and rows, and its cells, each holding the
// balls whose centers are in it, by their places in the list of balls.
struct Grid
{
    int cols;
    int rows;
    std::vector<std::vector<int>> cells;
};

// One box of a quadtree: its bounds, the balls in it while it's a leaf,
// and where its four children start in the tree's list of boxes, or -1
// while it has none.
struct QuadNode
{
    AABB bounds;
    std::vector<int> balls;
    int firstChild;
};

// A quadtree: all its boxes in one list, the whole window first. A box
// finds its children by their place in the list, not by a reference,
// since a reference into a vector dangles when the vector grows.
struct QuadTree
{
    std::vector<QuadNode> nodes;
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

// A random float from lo to hi. The generator is made once, and seeded
// from the system, so that every run is different.
std::mt19937 rng{ std::random_device{}() };

float randRange(float lo, float hi)
{
    std::uniform_real_distribution<float> spread(lo, hi);
    return spread(rng);
}

// Whether a point is in a box: inside its range on both axes. On an edge
// counts as in.
bool pointInBox(Vec2 P, const AABB& box)
{
    return P.x >= box.min.x && P.x <= box.max.x &&
           P.y >= box.min.y && P.y <= box.max.y;
}

// Whether two boxes overlap: their ranges overlap on both axes, so each
// box's min is no more than the other's max. Touching counts.
bool boxesOverlap(const AABB& a, const AABB& b)
{
    return a.min.x <= b.max.x && b.min.x <= a.max.x &&
           a.min.y <= b.max.y && b.min.y <= a.max.y;
}

// A box as SDL's rectangle: its top-left corner, its width and its height.
SDL_FRect rectOf(const AABB& box)
{
    return { box.min.x, box.min.y, box.max.x - box.min.x,
             box.max.y - box.min.y };
}

// A whole number with commas every three digits, such as 1,999,000.
std::string withCommas(long n)
{
    std::string digits = std::to_string(n);
    std::string out;
    for (size_t i = 0; i < digits.size(); ++i)
    {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return out;
}

// A fresh set of balls, scattered over the window, each heading a random
// way at from 40 to 160 pixels a second.
std::vector<Ball> scatter(int count)
{
    std::vector<Ball> balls;
    for (int i = 0; i < count; ++i)
    {
        Vec2 pos = { randRange(RADIUS, WINDOW_WIDTH - RADIUS),
                     randRange(RADIUS, WINDOW_HEIGHT - RADIUS) };
        float speed = randRange(40.0f, 160.0f);
        balls.push_back({ pos, Vec2::fromAngle(randRange(0, 2 * PI), speed) });
    }
    return balls;
}

// Keep a ball in the window, bouncing it off the edges.
void keepInWindow(Ball& b)
{
    if (b.pos.x < RADIUS)
    {
        b.pos.x = RADIUS;
        b.vel.x = std::fabs(b.vel.x);
    }
    if (b.pos.x > WINDOW_WIDTH - RADIUS)
    {
        b.pos.x = WINDOW_WIDTH - RADIUS;
        b.vel.x = -std::fabs(b.vel.x);
    }
    if (b.pos.y < RADIUS)
    {
        b.pos.y = RADIUS;
        b.vel.y = std::fabs(b.vel.y);
    }
    if (b.pos.y > WINDOW_HEIGHT - RADIUS)
    {
        b.pos.y = WINDOW_HEIGHT - RADIUS;
        b.vel.y = -std::fabs(b.vel.y);
    }
}

// The narrow phase, for one pair. If the balls overlap, each is pushed out
// by half the depth, along the line between their centers, as Chapter 18
// pushed a ball out of a wall. Then, if they're closing, they swap the
// parts of their velocities along that line: a perfect bounce between
// equal masses. Centers in the same place give no line, so they're left.
bool collide(Ball& a, Ball& b)
{
    Vec2 d = b.pos - a.pos;
    float distSq = d.lengthSquared();
    float reach = 2.0f * RADIUS;
    if (distSq > reach * reach || distSq < 0.000001f) return false;
    float dist = std::sqrt(distSq);
    Vec2 n = d / dist;
    float depth = reach - dist;
    a.pos -= n * (0.5f * depth);
    b.pos += n * (0.5f * depth);
    float va = a.vel.dot(n);
    float vb = b.vel.dot(n);
    if (va > vb)
    {
        a.vel += n * (vb - va);
        b.vel += n * (va - vb);
    }
    return true;
}

// Brute force: every ball against every ball after it in the list.
PassCounts bruteForce(std::vector<Ball>& balls)
{
    PassCounts counts = { 0, 0 };
    for (size_t i = 0; i < balls.size(); ++i)
        for (size_t j = i + 1; j < balls.size(); ++j)
        {
            ++counts.checks;
            if (collide(balls[i], balls[j])) ++counts.contacts;
        }
    return counts;
}

// The grid's column or row for a coordinate, kept inside the grid, since
// a push can leave a ball a hair outside the window.
int cellIndex(float v, int count)
{
    return std::clamp((int)(v / CELL), 0, count - 1);
}

// A grid over the window, with no balls in it yet.
Grid makeGrid()
{
    int cols = (int)std::ceil(WINDOW_WIDTH / CELL);
    int rows = (int)std::ceil(WINDOW_HEIGHT / CELL);
    return { cols, rows, std::vector<std::vector<int>>((size_t)(cols * rows)) };
}

// The grid: put each ball in the cell its center is in, then test each
// ball against the balls in its own cell and the eight around it, after
// it in the list, so that each pair is tested once.
PassCounts gridPass(std::vector<Ball>& balls, Grid& grid)
{
    for (std::vector<int>& cell : grid.cells) cell.clear();
    for (int i = 0; i < (int)balls.size(); ++i)
    {
        int cx = cellIndex(balls[i].pos.x, grid.cols);
        int cy = cellIndex(balls[i].pos.y, grid.rows);
        grid.cells[cy * grid.cols + cx].push_back(i);
    }

    PassCounts counts = { 0, 0 };
    for (int i = 0; i < (int)balls.size(); ++i)
    {
        int cx = cellIndex(balls[i].pos.x, grid.cols);
        int cy = cellIndex(balls[i].pos.y, grid.rows);
        int x0 = std::max(cx - 1, 0);
        int x1 = std::min(cx + 1, grid.cols - 1);
        int y0 = std::max(cy - 1, 0);
        int y1 = std::min(cy + 1, grid.rows - 1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                for (int j : grid.cells[y * grid.cols + x])
                    if (j > i)
                    {
                        ++counts.checks;
                        if (collide(balls[i], balls[j])) ++counts.contacts;
                    }
    }
    return counts;
}

// The child of a box that a point falls in. A point at or past the middle
// goes right, or down, so one on a dividing line goes one way only.
int childFor(const QuadTree& tree, int node, Vec2 p)
{
    const QuadNode& box = tree.nodes[node];
    Vec2 mid = lerp(box.bounds.min, box.bounds.max, 0.5f);
    int quarter = (p.x >= mid.x ? 1 : 0) + (p.y >= mid.y ? 2 : 0);
    return box.firstChild + quarter;
}

// Split a leaf into its four quarters, added to the end of the list: top
// left, top right, bottom left and bottom right.
void split(QuadTree& tree, int node)
{
    AABB b = tree.nodes[node].bounds;
    Vec2 mid = lerp(b.min, b.max, 0.5f);
    AABB quarters[4] = { { b.min, mid },
                         { { mid.x, b.min.y }, { b.max.x, mid.y } },
                         { { b.min.x, mid.y }, { mid.x, b.max.y } },
                         { mid, b.max } };
    int first = (int)tree.nodes.size();
    for (const AABB& q : quarters) tree.nodes.push_back({ q, {}, -1 });
    tree.nodes[node].firstChild = first;
}

// Add ball i to the tree, starting at a box: into the child it falls in,
// if the box has split; into the box itself, if it has room or is as deep
// as boxes go; and otherwise, split the box, and move its balls down.
void insert(QuadTree& tree, int node, int depth, int i,
            const std::vector<Ball>& balls)
{
    if (tree.nodes[node].firstChild >= 0)
    {
        insert(tree, childFor(tree, node, balls[i].pos), depth + 1, i, balls);
        return;
    }
    if ((int)tree.nodes[node].balls.size() < CAPACITY || depth == MAX_DEPTH)
    {
        tree.nodes[node].balls.push_back(i);
        return;
    }
    split(tree, node);
    std::vector<int> moving = tree.nodes[node].balls;
    tree.nodes[node].balls.clear();
    for (int j : moving) insert(tree, node, depth, j, balls);
    insert(tree, node, depth, i, balls);
}

// Every ball whose center is in a range. A box the range misses is skipped
// whole, with everything under it, and only the leaves the range reaches
// are searched, ball by ball.
void query(const QuadTree& tree, int node, const AABB& range,
           const std::vector<Ball>& balls, std::vector<int>& found)
{
    const QuadNode& box = tree.nodes[node];
    if (!boxesOverlap(box.bounds, range)) return;
    if (box.firstChild < 0)
    {
        for (int i : box.balls)
            if (pointInBox(balls[i].pos, range)) found.push_back(i);
        return;
    }
    for (int c = 0; c < 4; ++c)
        query(tree, box.firstChild + c, range, balls, found);
}

// The quadtree: build it afresh, then search a box around each ball that
// reaches a diameter each way, which holds every ball it could touch.
// Each pair is tested once, from the ball earlier in the list.
PassCounts quadtreePass(std::vector<Ball>& balls, QuadTree& tree)
{
    AABB window = { { 0, 0 }, { WINDOW_WIDTH, WINDOW_HEIGHT } };
    tree.nodes.clear();
    tree.nodes.push_back({ window, {}, -1 });
    for (int i = 0; i < (int)balls.size(); ++i) insert(tree, 0, 0, i, balls);

    PassCounts counts = { 0, 0 };
    std::vector<int> found;
    Vec2 reach = { 2.0f * RADIUS, 2.0f * RADIUS };
    for (int i = 0; i < (int)balls.size(); ++i)
    {
        found.clear();
        query(tree, 0, { balls[i].pos - reach, balls[i].pos + reach }, balls,
              found);
        for (int j : found)
            if (j > i)
            {
                ++counts.checks;
                if (collide(balls[i], balls[j])) ++counts.contacts;
            }
    }
    return counts;
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
        SDL_CreateWindow("10000 Balls", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The balls, the broad phase in use, the clump, whether to show the
    // outlines, and the grid and the quadtree, kept between frames.
    int count = 2000;
    std::vector<Ball> balls = scatter(count);
    int phase = GRID;
    bool clump = false;
    bool outlines = false;
    Grid grid = makeGrid();
    QuadTree tree;

    // The readout's timings: sums over each half second, and the averages
    // last shown. Translucent panels behind the text need blending.
    float frameSum = 0.0f;
    float passSum = 0.0f;
    int frames = 0;
    float shownFrameMs = 0.0f;
    float shownPassMs = 0.0f;
    PassCounts counts = { 0, 0 };
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // Delta time: the seconds since the last frame, from SDL's counter.
    Uint64 previous = SDL_GetPerformanceCounter();
    const double frequency = (double)SDL_GetPerformanceFrequency();

    bool running = true;
    while (running)
    {
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)((now - previous) / frequency);
        previous = now;
        float frameSeconds = dt;   // Before the clamp, for the readout.
        if (dt > 0.05f) dt = 0.05f;   // A long stall counts as 1/20 s.

        // Events: quitting, and the keys and clicks that happen once.
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT) running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN)
            {
                if (event.key.key == SDLK_ESCAPE) running = false;
                else if (event.key.key == SDLK_SPACE) phase = (phase + 1) % 3;
                else if (event.key.key == SDLK_C) clump = !clump;
                else if (event.key.key == SDLK_V) outlines = !outlines;
                else if (event.key.key == SDLK_UP ||
                         event.key.key == SDLK_DOWN)
                {
                    int change = (event.key.key == SDLK_UP) ? 1000 : -1000;
                    count = std::clamp(count + change, 1000, 10000);
                    balls = scatter(count);
                }
            }
        }

        // Each ball moves, pulled toward the middle while clumping, and
        // bounces off the window's edges.
        Vec2 middle = { WINDOW_WIDTH / 2.0f, WINDOW_HEIGHT / 2.0f };
        for (Ball& b : balls)
        {
            if (clump) b.vel += (middle - b.pos).normalized() * (PULL * dt);
            b.pos += b.vel * dt;
            keepInWindow(b);
        }

        // The collision pass, timed by SDL's counter: the broad phase in
        // use, and the narrow phase for each pair it finds.
        Uint64 passStart = SDL_GetPerformanceCounter();
        if (phase == BRUTE_FORCE) counts = bruteForce(balls);
        else if (phase == GRID) counts = gridPass(balls, grid);
        else counts = quadtreePass(balls, tree);
        Uint64 passEnd = SDL_GetPerformanceCounter();

        // The timings, averaged over each half second, so they can be read.
        frameSum += frameSeconds * 1000.0f;
        passSum += (float)((passEnd - passStart) * 1000.0 / frequency);
        ++frames;
        if (frameSum >= 500.0f)
        {
            shownFrameMs = frameSum / frames;
            shownPassMs = passSum / frames;
            frameSum = passSum = 0.0f;
            frames = 0;
        }

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);

        // The outlines, if they're shown: the grid's lines, one by one, or
        // the quadtree's boxes, all in one call.
        if (outlines)
        {
            setColor(renderer, FAINT);
            if (phase == GRID)
            {
                for (int x = 0; x <= grid.cols; ++x)
                    SDL_RenderLine(renderer, x * CELL, 0, x * CELL,
                                   WINDOW_HEIGHT);
                for (int y = 0; y <= grid.rows; ++y)
                    SDL_RenderLine(renderer, 0, y * CELL, WINDOW_WIDTH,
                                   y * CELL);
            }
            else if (phase == QUADTREE)
            {
                std::vector<SDL_FRect> boxes;
                for (const QuadNode& node : tree.nodes)
                    boxes.push_back(rectOf(node.bounds));
                SDL_RenderRects(renderer, boxes.data(), (int)boxes.size());
            }
        }

        // The balls, as squares a diameter across, all in one call, so that
        // drawing them costs little beside the collisions.
        std::vector<SDL_FRect> squares;
        for (const Ball& b : balls)
            squares.push_back({ b.pos.x - RADIUS, b.pos.y - RADIUS,
                                2.0f * RADIUS, 2.0f * RADIUS });
        setColor(renderer, BLUE);
        SDL_RenderFillRects(renderer, squares.data(), (int)squares.size());

        // The readout, on panels the balls show faintly through.
        setColor(renderer, PANEL);
        SDL_FRect top = { 0, 0, WINDOW_WIDTH, 52 };
        SDL_FRect bottom = { 0, WINDOW_HEIGHT - 32, WINDOW_WIDTH, 32 };
        SDL_RenderFillRect(renderer, &top);
        SDL_RenderFillRect(renderer, &bottom);
        drawText(renderer, font,
                 std::string(PHASE_NAMES[phase]) + ": " + withCommas(count) +
                 " balls, " + withCommas(counts.checks) + " pairs tested, " +
                 withCommas(counts.contacts) + " touching",
                 12, 10, (phase == BRUTE_FORCE) ? AMBER : TEXT);
        drawText(renderer, font,
                 "collision pass " + fmt2(shownPassMs) + " ms   frame " +
                 fmt2(shownFrameMs) + " ms   clump " +
                 (clump ? "on" : "off"),
                 12, 30, TEXT);
        drawText(renderer, font,
                 "[Space] broad phase   [Up] [Down] balls   [C] clump   "
                 "[V] view   [Esc] quit", 12, WINDOW_HEIGHT - 26, GREY);

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
