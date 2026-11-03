// A-Star Explorer - the Chapter 23 demo (A* search) for
// Learning C++ by Building Games: Fundamentals
//
// A* searches a grid for the cheapest path, guided by a heuristic, an
// estimate of the cost left to the goal. The cells it explored are blue,
// the cells still waiting are teal, and the path is amber. H steps through
// the heuristics, D turns diagonal steps on and off, W weights the
// heuristic, and T turns the tie-breaking on and off. Drag to paint walls,
// right-drag to erase, and drag the start and goal. C clears the grid, and
// R puts the starting map back.

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <climits>
#include <cmath>
#include <functional>
#include <queue>
#include <string>
#include <tuple>
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
constexpr SDL_Color EXPLORED = { 40, 70, 120, 255 };
constexpr SDL_Color WAITING = { 40, 110, 110, 255 };

// The grid, as in Flood: cells CELL pixels square, COLS across and ROWS
// down, below a strip GRID_TOP pixels high for the readout.
constexpr int CELL = 25;
constexpr int COLS = 40;
constexpr int ROWS = 24;
constexpr int GRID_TOP = 50;

// What a step costs: 100 straight, and 142 diagonally, which is 100 times
// the square root of 2, 141.4, rounded up. Whole numbers make equal costs
// exactly equal.
constexpr int STRAIGHT = 100;
constexpr int DIAGONAL = 142;

// The weight on the heuristic when W is on, in tenths: 1.8.
constexpr int HEAVY = 18;

// What a cell holds, what a drag with the mouse is doing, and the
// heuristics, with their names.
enum Tile { GROUND, WALL };
enum Drag { NONE, PAINT, ERASE, MOVE_START, MOVE_GOAL };
enum Heuristic { NO_HEURISTIC, MANHATTAN, EUCLIDEAN, CHEBYSHEV, OCTILE };
const char* const HEURISTIC_NAMES[] = { "none, so Dijkstra's", "Manhattan",
                                        "Euclidean", "Chebyshev", "octile" };

// What a search finds: each cell's cost from the start so far, g, the cell
// it was reached from, and whether it's closed; how many cells it closed,
// and whether one of them was the goal.
struct Search
{
    std::vector<int> g;
    std::vector<int> came;
    std::vector<bool> closed;
    int explored;
    bool found;
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

// The starting map: a cup of walls, open toward the start, between the
// start and the goal.
std::vector<Tile> startingMap()
{
    std::vector<Tile> tiles(COLS * ROWS, GROUND);
    for (int row = 5; row <= 18; ++row) tiles[row * COLS + 23] = WALL;
    for (int col = 15; col <= 23; ++col)
    {
        tiles[5 * COLS + col] = WALL;
        tiles[18 * COLS + col] = WALL;
    }
    return tiles;
}

// The heuristic's estimate of the cost from cell a to cell b, from how far
// apart they are across, dx, and down, dy.
int estimate(Heuristic kind, int a, int b)
{
    int dx = std::abs(a % COLS - b % COLS);
    int dy = std::abs(a / COLS - b / COLS);
    switch (kind)
    {
    case MANHATTAN:
        return STRAIGHT * (dx + dy);
    case EUCLIDEAN:
        return (int)(STRAIGHT * std::sqrt((double)(dx * dx + dy * dy)));
    case CHEBYSHEV:
        return STRAIGHT * std::max(dx, dy);
    case OCTILE:
        return STRAIGHT * std::max(dx, dy) +
               (DIAGONAL - STRAIGHT) * std::min(dx, dy);
    default:
        return 0;
    }
}

// The cell one step from cell in direction k, or -1 if the step isn't
// allowed: off the grid, into a wall, or diagonally past a wall's corner.
// Directions 0 to 3 are right, left, down and up, and 4 to 7 are diagonal.
int stepFrom(const std::vector<Tile>& tiles, int cell, int k)
{
    const int dx[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
    const int dy[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
    int col = cell % COLS;
    int row = cell / COLS;
    int c = col + dx[k];
    int r = row + dy[k];
    if (c < 0 || c >= COLS || r < 0 || r >= ROWS) return -1;
    if (tiles[r * COLS + c] == WALL) return -1;
    // A diagonal step passes between two cells, and both must be open.
    if (k >= 4 &&
        (tiles[row * COLS + c] == WALL || tiles[r * COLS + col] == WALL))
        return -1;
    return r * COLS + c;
}

// A* from start to goal. The cell taken next is the waiting one with the
// least f: its cost so far, g, plus the heuristic's estimate of the rest,
// times a weight in tenths. Between equal f, with tieBreak on, the one with
// the larger g, which is further along, comes out first.
Search aStar(const std::vector<Tile>& tiles, int start, int goal,
             Heuristic kind, bool diagonals, int weight, bool tieBreak)
{
    Search s = { std::vector<int>(COLS * ROWS, INT_MAX),
                 std::vector<int>(COLS * ROWS, -1),
                 std::vector<bool>(COLS * ROWS, false), 0, false };
    // Each entry is f, a tie-breaker and a cell; the least comes out first.
    using Entry = std::tuple<int, int, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    s.g[start] = 0;
    open.push({ estimate(kind, start, goal) * weight / 10, 0, start });
    while (!open.empty())
    {
        int cell = std::get<2>(open.top());
        open.pop();
        if (s.closed[cell]) continue;   // Stale: closed already.
        s.closed[cell] = true;
        ++s.explored;
        if (cell == goal)
        {
            s.found = true;
            break;
        }
        for (int k = 0; k < (diagonals ? 8 : 4); ++k)
        {
            int next = stepFrom(tiles, cell, k);
            if (next < 0 || s.closed[next]) continue;
            int through = s.g[cell] + (k >= 4 ? DIAGONAL : STRAIGHT);
            if (through < s.g[next])
            {
                s.g[next] = through;
                s.came[next] = cell;
                int f = through + estimate(kind, next, goal) * weight / 10;
                open.push({ f, tieBreak ? -through : 0, next });
            }
        }
    }
    return s;
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
        SDL_CreateWindow("A-Star Explorer", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The map, and the start and the goal. Then the settings: the
    // heuristic, diagonal steps, the weight and the tie-breaking. Then the
    // drag in progress.
    std::vector<Tile> tiles = startingMap();
    int start = 15 * COLS + 6;
    int goal = 9 * COLS + 34;
    Heuristic kind = OCTILE;
    bool diagonals = true;
    bool heavy = false;
    bool tieBreak = true;
    Drag drag = NONE;

    bool running = true;
    while (running)
    {
        // Events: quitting, and the keys and clicks that happen once.
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT) running = false;
            else if (event.type == SDL_EVENT_KEY_DOWN)
            {
                if (event.key.key == SDLK_ESCAPE) running = false;
                else if (event.key.key == SDLK_H)
                    kind = (Heuristic)((kind + 1) % 5);
                else if (event.key.key == SDLK_D) diagonals = !diagonals;
                else if (event.key.key == SDLK_W) heavy = !heavy;
                else if (event.key.key == SDLK_T) tieBreak = !tieBreak;
                else if (event.key.key == SDLK_C)
                    tiles.assign(COLS * ROWS, GROUND);
                else if (event.key.key == SDLK_R) tiles = startingMap();
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            {
                // A left drag from the start or the goal moves it, and any
                // other left drag paints walls. A right drag erases.
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

        // The drag works on the cell under the mouse, as in Flood.
        int hover = cellAt(mx, my);
        if (hover >= 0)
        {
            bool free = hover != start && hover != goal;
            if (drag == PAINT && free) tiles[hover] = WALL;
            else if (drag == ERASE && free) tiles[hover] = GROUND;
            else if (drag == MOVE_START && free && tiles[hover] != WALL)
                start = hover;
            else if (drag == MOVE_GOAL && free && tiles[hover] != WALL)
                goal = hover;
        }

        // The search, afresh every frame.
        Search s = aStar(tiles, start, goal, kind, diagonals,
                         heavy ? HEAVY : 10, tieBreak);
        std::vector<int> path;
        if (s.found) path = pathTo(s.came, start, goal);

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);

        // Each cell: walls in stone, explored cells blue, and cells still
        // waiting, reached but not explored, teal.
        for (int cell = 0; cell < COLS * ROWS; ++cell)
        {
            if (tiles[cell] == WALL) fillCell(renderer, cell, STONE);
            else if (s.closed[cell]) fillCell(renderer, cell, EXPLORED);
            else if (s.g[cell] != INT_MAX) fillCell(renderer, cell, WAITING);
        }

        // The path, then the start and the goal on top.
        for (int cell : path) fillCell(renderer, cell, AMBER);
        fillCell(renderer, start, GREEN);
        fillCell(renderer, goal, RED);

        // The readout: the search and its path, then the settings.
        std::string result = "no path";
        if (s.found)
            result = "a path of " + std::to_string(path.size() - 1) +
                     " steps, costing " + fmt2(s.g[goal] / 100.0f);
        drawText(renderer, font,
                 "A*, heuristic " + std::string(HEURISTIC_NAMES[kind]) + ": " +
                 std::to_string(s.explored) + " cells explored, " + result,
                 12, 8, TEXT);
        drawText(renderer, font,
                 std::string("diagonals ") + (diagonals ? "on" : "off") +
                 "   weight " + (heavy ? "1.8" : "1") + "   ties " +
                 (tieBreak ? "to the one further along" : "by cell number"),
                 12, 26, GREY);
        drawText(renderer, font,
                 "[H] heuristic   [D] diagonals   [W] weight   [T] ties   "
                 "[C] clear   [R] reset", 12, WINDOW_HEIGHT - 46, GREY);
        drawText(renderer, font,
                 "[Drag] walls   [Right-drag] erase   drag the green or red "
                 "cell   [Esc] quit", 12, WINDOW_HEIGHT - 26, GREY);

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
