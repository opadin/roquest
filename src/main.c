#include <stdio.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// TODO tile-size should be variable
const int TILE_WIDTH = 8;
const int TILE_HEIGHT = 16;

const int WINDOW_WIDTH = TILE_WIDTH * 100;
const int WINDOW_HEIGHT = TILE_HEIGHT * 50;

const int ZOOMX = 1;
const int ZOOMY = 1;

#define COLS  32
#define ROWS  32

struct global {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* font;
    int player_x;
    int player_y;
};

struct global g;

enum tile_type {
    TILE_TYPE_NOTHING,
    TILE_TYPE_WALL,
    TILE_TYPE_FLOOR
};

struct color {
    uint8_t red, green, blue;
};

struct map_tile {
    enum tile_type type;
};

static struct map_tile map[ROWS][COLS];

const struct color WALL_TOP_COLOR = { 192, 192, 168 };
const struct color WALL_SIDE_COLOR = { 104, 104, 72 };
const struct color WALL_SIDE_SHADOW_COLOR = { 32, 16, 0 };
const struct color FLOOR_COLOR = { 208, 48, 120 };
const struct color FLOOR_SHADOW_COLOR = { 152, 0, 80 };

_Noreturn void fatal(SDL_PRINTF_FORMAT_STRING const char* format, ...)
{
    va_list argList;
    va_start(argList, format);
    char buffer[1024];
    SDL_vsnprintf(buffer, sizeof(buffer), format, argList);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Fatal Error", buffer, 0);
    SDL_Quit();
    exit(1);
}

void* load_file(const char* path, uint32_t* size)
{
    SDL_assert(path && path[0]);

    SDL_RWops* rw = SDL_RWFromFile(path, "rb");
    if (!rw) fatal("could not open file '%s': %s", path, SDL_GetError());

    // only max size of 4gb
    Sint64 tsize = SDL_RWsize(rw);
    if (tsize < 0 || tsize > SDL_MAX_UINT32) fatal("invalid file size (%I64d)", tsize);
    uint32_t fsize = (uint32_t)tsize;

    unsigned char* data = malloc(fsize + 1);
    size_t read = SDL_RWread(rw, data, 1, fsize);
    if (read != fsize) fatal("could not read entire file '%s'", path);

    data[fsize] = '\0';
    SDL_RWclose(rw);

    if (size)
        *size = fsize;

    return data;
}

SDL_Texture* load_image(const char* file, int* w, int* h)
{
    SDL_assert(file && file[0] && w && h);

    char path[_MAX_PATH + 1];
    SDL_snprintf(path, sizeof(path), "%s%s\\%s", SDL_GetBasePath(), "res", file);

    uint32_t fsize;
    void* data = load_file(path, &fsize);
    int comp;
    uint8_t* image = stbi_load_from_memory(data, fsize, w, h, &comp, 4);
    if (!image) fatal("could not load image '%s'", file);
    free(data);

    SDL_assert(w > 0 && *w % 16 == 0 && *h > 0 && *h % 16 == 0);

    for (int i = 0; i < *w * *h; i++) {
        uint8_t v = image[i * 4];
        SDL_assert(v == image[i * 4 + 1] && v == image[i * 4 + 2] && image[i * 4 + 3] == 0xff);
        image[i * 4 + 0] = image[i * 4 + 1] = image[i * 4 + 2] = 0xff;
        image[i * 4 + 3] = v;
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(image, *w, *h, 32, *w * 4, SDL_PIXELFORMAT_RGBA32);
    if (!surface) fatal("could not create sdl surface from image '%s'", file);

    SDL_Texture* texture = SDL_CreateTextureFromSurface(g.renderer, surface);
    if (!texture) fatal("could not create texture from image '%s'", file);

    return texture;
}

void render_tile(int x, int y, int ch, struct color color)
{
    ch &= 0xff;
    int srcx = (ch % 16) * TILE_WIDTH;
    int srcy = (ch / 16) * TILE_HEIGHT;
    int dstx = x * TILE_WIDTH * ZOOMX;
    int dsty = y * TILE_HEIGHT * ZOOMY;

    SDL_SetTextureColorMod(g.font, color.red, color.green, color.blue);
    SDL_RenderCopy(g.renderer, g.font, &(SDL_Rect) { srcx, srcy, TILE_WIDTH, TILE_HEIGHT}, & (SDL_Rect) { dstx, dsty, TILE_WIDTH* ZOOMX, TILE_HEIGHT* ZOOMY });
}

void render_tile_with_bg(int x, int y, int ch, struct color fg, struct color bg)
{

    render_tile(x, y, 0xdb, fg);
    render_tile(x, y, ch, bg);
}

void render()
{
    int sx = (WINDOW_WIDTH / (TILE_WIDTH * ZOOMX) - COLS) / 2;
    int sy = (WINDOW_HEIGHT / (TILE_HEIGHT * ZOOMY) - ROWS) / 2;

    // simple blocked map
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            switch (map[y][x].type) {
                case TILE_TYPE_WALL:
                    if (y < ROWS - 1 && map[y + 1][x].type == TILE_TYPE_WALL) {
                        render_tile(sx + x, sy + y, 0xdb, WALL_TOP_COLOR);
                    } else {
                        if (x > 0 && map[y][x - 1].type == TILE_TYPE_WALL && y < ROWS - 1 && map[y + 1][x - 1].type == TILE_TYPE_WALL) {
                            render_tile_with_bg(sx + x, sy + y, 0xdf, WALL_SIDE_SHADOW_COLOR, WALL_TOP_COLOR);
                        } else {
                            render_tile_with_bg(sx + x, sy + y, 0xdf, WALL_SIDE_COLOR, WALL_TOP_COLOR);
                        }
                    }
                    break;
                case TILE_TYPE_FLOOR:
                    if (x > 0 && map[y][x - 1].type == TILE_TYPE_WALL) {
                        render_tile(sx + x, sy + y, 0xdb, FLOOR_SHADOW_COLOR);
                    } else {
                        render_tile(sx + x, sy + y, 0xdb, FLOOR_COLOR);
                    }
                    break;
                case TILE_TYPE_NOTHING:
                    render_tile(sx + x, sy + y, 0xdb, (struct color) { 0, 0, 0 });
                    break;
                default:
                    // something wrong!
                    render_tile(sx + x, sy + y, '?', (struct color) { 255, 0, 0 });
                    break;

            }
        }
    }

    // player
    render_tile(sx + g.player_x, sy + g.player_y, '@', (struct color) { 255, 255, 255 });
}

void create_map()
{
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            map[y][x].type = TILE_TYPE_NOTHING;
        }
    }
    // border
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            if (x == 0 || y == 0 || x == COLS - 1 || y == ROWS - 1) {
                map[y][x].type = TILE_TYPE_WALL;
            } else {
                map[y][x].type = TILE_TYPE_FLOOR;
            }
        }
    }
}

int main(int argc, char* argv[])
{
    SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "1");

    if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_VIDEO) != 0)
        fatal("SDL_Init failed: %s\n", SDL_GetError());

    g.window = SDL_CreateWindow("roquest", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
    if (!g.window) fatal("Could not create window: %s\n", SDL_GetError());

    g.renderer = SDL_CreateRenderer(g.window, -1, 0);
    if (!g.renderer) fatal("could not create sdl renderer: %s", SDL_GetError());

    SDL_SetRenderDrawColor(g.renderer, 0, 0, 0, 255);
    SDL_RenderClear(g.renderer);

    int fw, fh;
    g.font = load_image("default-font.png", &fw, &fh);
    SDL_assert(fw == TILE_WIDTH * 16 && fh == TILE_HEIGHT * 16);

    create_map();
    g.player_x = COLS / 2;
    g.player_y = ROWS / 2;

    SDL_Event event;
    bool quit_requested = false;
    while (!quit_requested)
    {
        if (SDL_PollEvent(&event))
        {
            switch (event.type) {
                case SDL_QUIT:
                    quit_requested = true;
                    break;
                case SDL_KEYDOWN:
                    switch (event.key.keysym.scancode) {
                        case SDL_SCANCODE_UP:
                            if (g.player_y > 0) g.player_y--;
                            break;
                        case SDL_SCANCODE_DOWN:
                            if (g.player_y < ROWS - 1) g.player_y++;
                            break;
                        case SDL_SCANCODE_LEFT:
                            if (g.player_x > 0) g.player_x--;
                            break;
                        case SDL_SCANCODE_RIGHT:
                            if (g.player_x < COLS - 1) g.player_x++;
                        default:
                            break;
                    }
            }
        }

        render();
        SDL_RenderPresent(g.renderer);
    }

    SDL_DestroyWindow(g.window);

    SDL_Quit();
    return 0;
}