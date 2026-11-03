// Flood - the Chapter 22 demo (breadth-first search and Dijkstra) for
// Learning C++ by Building Games: Fundamentals
//
// A grid of ground, walls and swamp. A search floods out from the green
// start, and each cell is shaded by how far it is, so the waves show. The
// path to the red goal is drawn in amber. Space switches between a
// breadth-first search, which counts steps, and Dijkstra's, which counts
// costs, where a swamp costs five. Drag to paint walls, or swamp after B,
// right-drag to erase, and drag the start and goal. Z changes the order
// neighbors are tried in, Enter replays the flood, C clears the grid, and
// R puts the starting map back.

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <functional>
#include <queue>
#include <string>
#include <utility>
#include <vector>
#include "Vec2.h"

constexpr int WINDOW_WIDTH  = 1000;
constexpr int WINDOW_HEIGHT = 700;

constexpr SDL_Color TEXT = { 235, 238, 242, 255 };
constexpr SDL_Color GREY = { 90, 100, 112, 255 };
constexpr SDL_Color GREEN = { 47, 158, 68, 255 };
constexpr SDL_Color RED = { 224, 49, 49, 255 };
constexpr SDL_Color AMBER = { 232, 89, 12, 255 };
constexpr SDL_Color STONE = { 55, 60, 70, 255 };
constexpr SDL_Color MUD = { 50, 46, 28, 255 };

// The grid: cells CELL pixels square, COLS across and ROWS down, below a
// strip GRID_TOP pixels high for the readout.
constexpr int CELL = 25;
constexpr int COLS = 40;
constexpr int ROWS = 24;
constexpr int GRID_TOP = 50;

// What a step into a swamp costs, where a step onto ground costs 1.
constexpr int SWAMP_COST = 5;

// How long a replay of the flood takes to spread, in seconds, and how many
// steps (or how much cost) each band of the shading spans.
constexpr float REPLAY_SECONDS = 3.0f;
constexpr int BAND = 4;

// What a cell holds, and what a drag with the mouse is doing.
enum Tile { GROUND, WALL, SWAMP };
enum Drag { NONE, PAINT, ERASE, MOVE_START, MOVE_GOAL };

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

// Set the color the renderer draws with, from an SDL_Color.
void setColor(SDL_Renderer* renderer, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

// Fill a cell, a pixel in from its edges, so that the gaps between cells
// show as the grid's lines. A cell is a number: its row times COLS, plus its
// column.
void fillCell(SDL_Renderer* renderer, int cell, SDL_Color color)
{
    setColor(renderer, color);
    SDL_FRect r = { (float)(cell % COLS * CELL + 1),
                    (float)(GRID_TOP + cell / COLS * CELL + 1),
                    CELL - 2.0f, CELL - 2.0f };
    SDL_RenderFillRect(renderer, &r);
}

// The cell at a point in the window, or -1 if the point is off the grid.
int cellAt(float x, float y)
{
    if (x < 0.0f || y < GRID_TOP) return -1;
    int col = (int)(x / CELL);
    int row = (int)((y - GRID_TOP) / CELL);
    if (col >= COLS || row >= ROWS) return -1;
    return row * COLS + col;
}

// The starting map: a wall with a gap near the start, a swamp lake in the
// middle, and a wall hanging down from the top near the goal.
std::vector<Tile> startingMap()
{
    std::vector<Tile> tiles(COLS * ROWS, GROUND);
    for (int row = 3; row <= 20; ++row)
        if (row != 11 && row != 12) tiles[row * COLS + 11] = WALL;
    for (int row = 0; row < ROWS; ++row)
        for (int col = 0; col < COLS; ++col)
        {
            float dx = (col - 21) / 5.5f;
            float dy = (row - 11.5f) / 8.0f;
            if (dx * dx + dy * dy <= 1.0f) tiles[row * COLS + col] = SWAMP;
        }
    for (int row = 0; row <= 14; ++row) tiles[row * COLS + 30] = WALL;
    return tiles;
}

// The cost of a step into a cell.
int stepCost(Tile tile) { return (tile == SWAMP) ? SWAMP_COST : 1; }

// The open neighbors of a cell, up to four, in out: those on the grid that
// aren't walls. They're tried right, left, down, then up, or with zigzag
// on, the other way around on every other cell.
int openNeighbors(const std::vector<Tile>& tiles, int cell, bool zigzag,
                  int out[4])
{
    const int dx[4] = { 1, -1, 0, 0 };
    const int dy[4] = { 0, 0, 1, -1 };
    int col = cell % COLS;
    int row = cell / COLS;
    bool backward = zigzag && (col + row) % 2 == 1;
    int count = 0;
    for (int k = 0; k < 4; ++k)
    {
        int d = backward ? 3 - k : k;
        int c = col + dx[d];
        int r = row + dy[d];
        if (c < 0 || c >= COLS || r < 0 || r >= ROWS) continue;
        if (tiles[r * COLS + c] == WALL) continue;
        out[count++] = r * COLS + c;
    }
    return count;
}

// Breadth-first search from start. Each cell's distance from it, in steps,
// goes in dist, or -1 if it can't be reached, and the cell it was reached
// from goes in came. A swamp is just ground to it, since it counts steps.
void bfs(const std::vector<Tile>& tiles, int start, bool zigzag,
         std::vector<int>& dist, std::vector<int>& came)
{
    std::fill(dist.begin(), dist.end(), -1);
    std::fill(came.begin(), came.end(), -1);
    std::queue<int> waiting;
    dist[start] = 0;
    waiting.push(start);
    while (!waiting.empty())
    {
        int cell = waiting.front();
        waiting.pop();
        int next[4];
        int count = openNeighbors(tiles, cell, zigzag, next);
        for (int k = 0; k < count; ++k)
        {
            if (dist[next[k]] != -1) continue;   // Queued already.
            dist[next[k]] = dist[cell] + 1;
            came[next[k]] = cell;
            waiting.push(next[k]);
        }
    }
}

// Dijkstra's search from start. Each cell's cost from it goes in cost, or -1
// if it can't be reached, and the cell it was reached from goes in came. The
// cell taken next is always the cheapest one waiting.
void dijkstra(const std::vector<Tile>& tiles, int start, bool zigzag,
              std::vector<int>& cost, std::vector<int>& came)
{
    std::fill(cost.begin(), cost.end(), -1);
    std::fill(came.begin(), came.end(), -1);
    // Each entry is a cost and a cell; std::greater puts the least on top.
    using Entry = std::pair<int, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> waiting;
    cost[start] = 0;
    waiting.push({ 0, start });
    while (!waiting.empty())
    {
        int sofar = waiting.top().first;
        int cell = waiting.top().second;
        waiting.pop();
        if (sofar > cost[cell]) continue;   // Stale: a cheaper way was found.
        int next[4];
        int count = openNeighbors(tiles, cell, zigzag, next);
        for (int k = 0; k < count; ++k)
        {
            int through = sofar + stepCost(tiles[next[k]]);
            if (cost[next[k]] == -1 || through < cost[next[k]])
            {
                cost[next[k]] = through;
                came[next[k]] = cell;
                waiting.push({ through, next[k] });
            }
        }
    }
}

// The path from start to goal, by the came-from links: followed back from
// the goal, then turned around. Empty if the goal wasn't reached.
std::vector<int> pathTo(const std::vector<int>& came, int start, int goal)
{
    std::vector<int> path;
    if (goal != start && came[goal] == -1) return path;
    for (int cell = goal; cell != start; cell = came[cell])
        path.push_back(cell);
    path.push_back(start);
    std::reverse(path.begin(), path.end());
    return path;
}

// What a path costs: the cost of each step, into every cell after the first.
int pathCost(const std::vector<Tile>& tiles, const std::vector<int>& path)
{
    int total = 0;
    for (size_t i = 1; i < path.size(); ++i) total += stepCost(tiles[path[i]]);
    return total;
}

// A reached cell's color, from dark when it's near to light when it's far:
// blue on ground, and brown in a swamp. On ground, every other band of
// BAND steps is a little lighter, so that the flood's waves show.
SDL_Color shade(Tile tile, int d, int farthest)
{
    float t = (float)d / farthest;
    float lift = (tile == GROUND && (d / BAND) % 2 == 1) ? 18.0f : 0.0f;
    if (tile == SWAMP)
        return { (Uint8)(lerp(70, 150, t) + lift),
                 (Uint8)(lerp(60, 130, t) + lift),
                 (Uint8)(lerp(30, 60, t) + lift), 255 };
    return { (Uint8)(lerp(40, 70, t) + lift), (Uint8)(lerp(70, 180, t) + lift),
             (Uint8)(lerp(120, 210, t) + lift), 255 };
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
        SDL_CreateWindow("Flood", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The map, and the start and the goal. Then the settings: the search,
    // the brush and the neighbor order. Then the drag in progress, the
    // replay's time so far, or -1 when there's no replay, and the search's
    // results: each cell's distance or cost, and where it was reached from.
    std::vector<Tile> tiles = startingMap();
    int start = 11 * COLS + 4;
    int goal = 11 * COLS + 35;
    bool useDijkstra = false;
    Tile brush = WALL;
    bool zigzag = false;
    Drag drag = NONE;
    float replay = -1.0f;
    std::vector<int> dist(COLS * ROWS);
    std::vector<int> came(COLS * ROWS);

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
                else if (event.key.key == SDLK_SPACE)
                    useDijkstra = !useDijkstra;
                else if (event.key.key == SDLK_B)
                    brush = (brush == WALL) ? SWAMP : WALL;
                else if (event.key.key == SDLK_Z) zigzag = !zigzag;
                else if (event.key.key == SDLK_RETURN) replay = 0.0f;
                else if (event.key.key == SDLK_C)
                    tiles.assign(COLS * ROWS, GROUND);
                else if (event.key.key == SDLK_R) tiles = startingMap();
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            {
                // A left drag from the start or the goal moves it, and any
                // other left drag paints. A right drag erases.
                int at = cellAt(event.button.x, event.button.y);
                if (at < 0) continue;
                if (event.button.button == SDL_BUTTON_LEFT)
                {
                    if (at == start) drag = MOVE_START;
                    else if (at == goal) drag = MOVE_GOAL;
                    else drag = PAINT;
                }
                else if (event.button.button == SDL_BUTTON_RIGHT) drag = ERASE;
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) drag = NONE;
        }

        // The mouse's position, read once a frame.
        float mx, my;
        SDL_GetMouseState(&mx, &my);

        // The drag works on the cell under the mouse. Painting and erasing
        // leave the start and the goal alone, and neither can move onto a
        // wall or onto the other.
        int hover = cellAt(mx, my);
        if (hover >= 0)
        {
            bool free = hover != start && hover != goal;
            if (drag == PAINT && free) tiles[hover] = brush;
            else if (drag == ERASE && free) tiles[hover] = GROUND;
            else if (drag == MOVE_START && free && tiles[hover] != WALL)
                start = hover;
            else if (drag == MOVE_GOAL && free && tiles[hover] != WALL)
                goal = hover;
        }

        // The search runs afresh every frame, since the map may have changed.
        if (useDijkstra) dijkstra(tiles, start, zigzag, dist, came);
        else bfs(tiles, start, zigzag, dist, came);
        std::vector<int> path = pathTo(came, start, goal);
        int farthest = std::max(1, *std::max_element(dist.begin(), dist.end()));

        // A replay shows the flood spreading: only the cells whose distance
        // is within its share of the farthest, so far.
        int shown = farthest;
        if (replay >= 0.0f)
        {
            replay += dt;
            shown = (int)(farthest * replay / REPLAY_SECONDS);
            if (replay >= REPLAY_SECONDS) replay = -1.0f;
        }

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);

        // Each cell: walls in stone, reached cells shaded by how far they
        // are, and swamp that isn't reached yet in mud.
        for (int cell = 0; cell < COLS * ROWS; ++cell)
        {
            if (tiles[cell] == WALL)
                fillCell(renderer, cell, STONE);
            else if (dist[cell] >= 0 && dist[cell] <= shown)
                fillCell(renderer, cell,
                         shade(tiles[cell], dist[cell], farthest));
            else if (tiles[cell] == SWAMP)
                fillCell(renderer, cell, MUD);
        }

        // The path, once the flood has reached the goal, then the start and
        // the goal on top.
        if (!path.empty() && dist[goal] <= shown)
            for (int cell : path) fillCell(renderer, cell, AMBER);
        fillCell(renderer, start, GREEN);
        fillCell(renderer, goal, RED);

        // The readout: the search and its path, then the settings.
        int reached = (int)std::count_if(dist.begin(), dist.end(),
                                         [](int d) { return d >= 0; });
        std::string result = "no path: the goal is walled off";
        if (!path.empty())
            result = "a path of " + std::to_string(path.size() - 1) +
                     " steps, costing " + std::to_string(pathCost(tiles, path));
        drawText(renderer, font,
                 std::string(useDijkstra ? "Dijkstra's search" :
                             "breadth-first search") + ": " + result,
                 12, 8, TEXT);
        drawText(renderer, font,
                 "cells reached " + std::to_string(reached) + "   brush " +
                 (brush == WALL ? "wall" : "swamp") + "   neighbor order " +
                 (zigzag ? "zigzag" : "fixed"), 12, 26, GREY);
        drawText(renderer, font,
                 "[Drag] paint   [Right-drag] erase   [B] wall or swamp   "
                 "drag the green or red cell", 12, WINDOW_HEIGHT - 46, GREY);
        drawText(renderer, font,
                 "[Space] BFS or Dijkstra   [Z] zigzag   [Enter] replay   "
                 "[C] clear   [R] reset   [Esc] quit", 12, WINDOW_HEIGHT - 26,
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
