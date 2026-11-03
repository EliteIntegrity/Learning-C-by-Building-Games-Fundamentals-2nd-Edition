// Texture Quad - the Chapter 24 demo (textures and UV mapping) for
// Learning C++ by Building Games: Fundamentals
//
// A texture built in code, an atlas of four test patterns, is mapped onto a
// quad whose four corners you drag, warping the image. Each vertex carries
// its own UV, and SDL_RenderGeometry fills the triangles. Keys 1 to 4 pick
// a cell of the atlas, A shows the whole atlas, and T tiles it. N switches
// between nearest and linear sampling, G between many triangles and two, E
// moves a cell's UVs in from its edges, and R puts the corners back.

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
constexpr SDL_Color AMBER = { 232, 89, 12, 255 };
constexpr SDL_Color HANDLE = { 200, 210, 220, 255 };

// The atlas: 256 texels square, a 2 by 2 grid of cells 128 texels square,
// and the cells' names, for the readout.
constexpr int ATLAS = 256;
constexpr int CELL = 128;
const char* const CELL_NAMES[] = { "gradient", "checkerboard", "rings",
                                   "stripes" };

// The quad is cut into GRID by GRID squares, two triangles each, unless G
// asks for one square. A corner can be grabbed within GRAB pixels.
constexpr int GRID = 12;
constexpr float GRAB = 22.0f;

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

// Set one texel of the pixel buffer: four bytes, red, green, blue and
// alpha, starting at (y * ATLAS + x) * 4.
void setTexel(std::vector<Uint8>& pixels, int x, int y, SDL_Color c)
{
    int i = (y * ATLAS + x) * 4;
    pixels[i] = c.r;
    pixels[i + 1] = c.g;
    pixels[i + 2] = c.b;
    pixels[i + 3] = c.a;
}

// The color at (x, y) in cell n's pattern. A pale line every 16 texels
// makes any warp easy to read.
SDL_Color patternTexel(int n, int x, int y)
{
    SDL_Color c = { 0, 0, 0, 255 };
    if (n == 0)
    {
        // A gradient: red grows to the right, and green downward.
        c = { (Uint8)(x * 255 / (CELL - 1)), (Uint8)(y * 255 / (CELL - 1)),
              64, 255 };
    }
    else if (n == 1)
    {
        // A checkerboard of squares 16 texels wide.
        bool dark = (x / 16 + y / 16) % 2 == 1;
        c = dark ? SDL_Color{ 40, 90, 200, 255 }
                 : SDL_Color{ 200, 220, 255, 255 };
    }
    else if (n == 2)
    {
        // Rings 10 texels wide, around the cell's middle.
        float d = std::sqrt((x - 63.5f) * (x - 63.5f) +
                            (y - 63.5f) * (y - 63.5f));
        bool dark = (int)(d / 10.0f) % 2 == 1;
        c = dark ? SDL_Color{ 230, 120, 40, 255 }
                 : SDL_Color{ 60, 30, 10, 255 };
    }
    else
    {
        // Diagonal stripes, 16 texels apart.
        bool dark = ((x + y) / 16) % 2 == 1;
        c = dark ? SDL_Color{ 40, 180, 90, 255 }
                 : SDL_Color{ 15, 60, 35, 255 };
    }
    if (x % 16 == 0 || y % 16 == 0)
        c = { (Uint8)((c.r + 255) / 2), (Uint8)((c.g + 255) / 2),
              (Uint8)((c.b + 255) / 2), 255 };
    return c;
}

// The atlas, as bytes: each cell's pattern, with a red square in its
// top-left corner, so that its way up shows under any warp.
std::vector<Uint8> buildAtlas()
{
    std::vector<Uint8> pixels(ATLAS * ATLAS * 4);
    for (int n = 0; n < 4; ++n)
    {
        int ox = (n % 2) * CELL;
        int oy = (n / 2) * CELL;
        for (int y = 0; y < CELL; ++y)
            for (int x = 0; x < CELL; ++x)
            {
                SDL_Color c = (x < 8 && y < 8) ? SDL_Color{ 240, 40, 40, 255 }
                                               : patternTexel(n, x, y);
                setTexel(pixels, ox + x, oy + y, c);
            }
    }
    return pixels;
}

// The quad, as a grid of grid by grid squares, two triangles each. A vertex
// at fractions s across and t down is placed between the four corners:
// along the top and bottom edges by s, then between those by t. Its UV is
// placed the same way, between (u0, v0) and (u1, v1). The corners are 0
// top left, 1 top right, 2 bottom left and 3 bottom right.
void buildQuad(const Vec2 corner[4], float u0, float v0, float u1, float v1,
               int grid, std::vector<SDL_Vertex>& verts,
               std::vector<int>& indices)
{
    verts.clear();
    indices.clear();
    for (int j = 0; j <= grid; ++j)
        for (int i = 0; i <= grid; ++i)
        {
            float s = (float)i / grid;
            float t = (float)j / grid;
            Vec2 top = lerp(corner[0], corner[1], s);
            Vec2 bottom = lerp(corner[2], corner[3], s);
            Vec2 p = lerp(top, bottom, t);
            SDL_Vertex v;
            v.position = { p.x, p.y };
            v.color = { 1.0f, 1.0f, 1.0f, 1.0f };
            v.tex_coord = { lerp(u0, u1, s), lerp(v0, v1, t) };
            verts.push_back(v);
        }
    for (int j = 0; j < grid; ++j)
        for (int i = 0; i < grid; ++i)
        {
            int a = j * (grid + 1) + i;   // The square's top left,
            int b = a + 1;                // its top right,
            int c = a + grid + 1;         // its bottom left,
            int d = c + 1;                // and its bottom right.
            indices.insert(indices.end(), { a, b, d, a, d, c });
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
        SDL_CreateWindow("Texture Quad", WINDOW_WIDTH, WINDOW_HEIGHT, 0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_SetRenderVSync(renderer, 1);

    // The font is copied beside the program when it's built. SDL_GetBasePath
    // gives the program's folder, wherever the program is started from.
    const char* base = SDL_GetBasePath();
    std::string folder = base ? base : "";
    std::string fontPath = folder + "RobotoMono-Light.ttf";
    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 16);
    if (!font) SDL_Log("Font not found, so no labels: %s", SDL_GetError());

    // The atlas, built as bytes, then copied into a texture.
    std::vector<Uint8> pixels = buildAtlas();
    SDL_Texture* atlas = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                           SDL_TEXTUREACCESS_STATIC,
                                           ATLAS, ATLAS);
    SDL_UpdateTexture(atlas, nullptr, pixels.data(), ATLAS * 4);

    // The corners, and where R puts them back. Then the corner being
    // dragged, or -1; the cell shown, 0 to 3, or -1 for the whole atlas; how
    // many times it repeats each way; and the settings: linear sampling,
    // many triangles or two, and UVs moved in from a cell's edges.
    const Vec2 HOME[4] = { { 320, 150 }, { 680, 150 },
                           { 320, 510 }, { 680, 510 } };
    Vec2 corner[4] = { HOME[0], HOME[1], HOME[2], HOME[3] };
    int drag = -1;
    int cell = 0;
    int reps = 1;
    bool linear = false;
    bool fine = true;
    bool inset = false;
    std::vector<SDL_Vertex> verts;
    std::vector<int> indices;

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
                else if (event.key.key >= SDLK_1 && event.key.key <= SDLK_4)
                {
                    cell = (int)(event.key.key - SDLK_1);
                    reps = 1;
                }
                else if (event.key.key == SDLK_A)
                {
                    cell = -1;
                    reps = 1;
                }
                else if (event.key.key == SDLK_T)
                {
                    cell = -1;
                    reps = reps % 3 + 1;
                }
                else if (event.key.key == SDLK_N) linear = !linear;
                else if (event.key.key == SDLK_G) fine = !fine;
                else if (event.key.key == SDLK_E) inset = !inset;
                else if (event.key.key == SDLK_R)
                    for (int i = 0; i < 4; ++i) corner[i] = HOME[i];
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                     event.button.button == SDL_BUTTON_LEFT)
            {
                // The nearest corner within GRAB pixels, if there is one.
                Vec2 press = { event.button.x, event.button.y };
                float nearest = GRAB;
                for (int i = 0; i < 4; ++i)
                {
                    if (distance(press, corner[i]) < nearest)
                    {
                        nearest = distance(press, corner[i]);
                        drag = i;
                    }
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) drag = -1;
        }

        // The mouse's position, read once a frame.
        float mx, my;
        SDL_GetMouseState(&mx, &my);

        // A dragged corner follows the mouse.
        if (drag >= 0) corner[drag] = { mx, my };

        // The UVs to map: a cell, a quarter of the atlas, or the whole atlas,
        // reps times each way. With inset on, a cell's UVs move in by half a
        // texel, so that linear sampling stays inside the cell.
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = (float)reps;
        float v1 = (float)reps;
        if (cell >= 0)
        {
            float half = inset ? 0.5f / ATLAS : 0.0f;
            u0 = (cell % 2) * 0.5f + half;
            v0 = (cell / 2) * 0.5f + half;
            u1 = u0 + 0.5f - 2.0f * half;
            v1 = v0 + 0.5f - 2.0f * half;
        }
        buildQuad(corner, u0, v0, u1, v1, fine ? GRID : 1, verts, indices);

        // Draw: clear to the background, draw the frame, then show it.
        SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
        SDL_RenderClear(renderer);

        // The quad, sampled as N says, and repeating the texture when it's
        // tiled, with UVs past 1.
        SDL_SetTextureScaleMode(atlas, linear ? SDL_SCALEMODE_LINEAR
                                              : SDL_SCALEMODE_NEAREST);
        SDL_TextureAddressMode address = (reps > 1) ? SDL_TEXTURE_ADDRESS_WRAP
                                                    : SDL_TEXTURE_ADDRESS_CLAMP;
        SDL_SetRenderTextureAddressMode(renderer, address, address);
        SDL_RenderGeometry(renderer, atlas, verts.data(), (int)verts.size(),
                           indices.data(), (int)indices.size());

        // The quad's outline, and its corners, white when grabbed or near.
        setColor(renderer, AMBER);
        const int edge[8] = { 0, 1, 1, 3, 3, 2, 2, 0 };
        for (int e = 0; e < 8; e += 2)
            SDL_RenderLine(renderer, corner[edge[e]].x, corner[edge[e]].y,
                           corner[edge[e + 1]].x, corner[edge[e + 1]].y);
        for (int i = 0; i < 4; ++i)
        {
            bool hot = drag == i || distance(corner[i], { mx, my }) < GRAB;
            setColor(renderer, hot ? TEXT : HANDLE);
            SDL_FRect handle = { corner[i].x - 6, corner[i].y - 6, 12, 12 };
            SDL_RenderFillRect(renderer, &handle);
        }

        // A small copy of the atlas, with the cell shown outlined.
        SDL_FRect thumb = { WINDOW_WIDTH - 176.0f, 76.0f, 160.0f, 160.0f };
        SDL_SetTextureScaleMode(atlas, SDL_SCALEMODE_NEAREST);
        SDL_RenderTexture(renderer, atlas, nullptr, &thumb);
        setColor(renderer, GREY);
        SDL_RenderRect(renderer, &thumb);
        drawText(renderer, font, "the atlas", thumb.x, thumb.y - 22, GREY);
        if (cell >= 0)
        {
            SDL_FRect shown = { thumb.x + (cell % 2) * 80.0f,
                                thumb.y + (cell / 2) * 80.0f, 80.0f, 80.0f };
            setColor(renderer, AMBER);
            SDL_RenderRect(renderer, &shown);
        }

        // The readout: what's shown, then the settings, then the controls.
        std::string shownText = "the whole atlas, " + std::to_string(reps) +
                                " by " + std::to_string(reps);
        if (cell >= 0)
            shownText = "cell " + std::to_string(cell + 1) + " of 4, the " +
                        CELL_NAMES[cell];
        drawText(renderer, font, "showing " + shownText, 12, 8, TEXT);
        drawText(renderer, font,
                 std::string("sampling ") + (linear ? "linear" : "nearest") +
                 "   " + std::to_string(indices.size() / 3) + " triangles" +
                 "   cell edges " + (inset ? "moved in half a texel" :
                                     "as they are"), 12, 26, GREY);
        drawText(renderer, font,
                 "[Drag] a corner   [1] to [4] a cell   [A] whole atlas   "
                 "[T] tile   [R] reset", 12, WINDOW_HEIGHT - 46, GREY);
        drawText(renderer, font,
                 "[N] nearest or linear   [G] triangles   [E] cell edges   "
                 "[Esc] quit", 12, WINDOW_HEIGHT - 26, GREY);

        SDL_RenderPresent(renderer);
    }

    // Clean up, in the reverse order of creation.
    SDL_DestroyTexture(atlas);
    if (font) TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
