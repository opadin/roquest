#include <stdio.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// TODO tile-size should be variable
const int TILE_WIDTH = 8;
const int TILE_HEIGHT = 16;

#define GW 18
#define GH 12
#define GFW ((GW) * 2 + 1)
#define GFH ((GH) * 2 + 1)

int gfield[GFH][GFW];

const int ZOOMX = 1;
const int ZOOMY = 1;

#define NUM_COL_PATTERN 10
#define NUM_ROW_PATTERN 5

#define COLS  ((GW) * 6 + 1)
#define ROWS  ((GH) * 4 + 1)

const int WINDOW_WIDTH = (TILE_WIDTH) * ((COLS)+4);
const int WINDOW_HEIGHT = (TILE_HEIGHT) * ((ROWS)+4);

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

void dump_map()
{
    char s[COLS + 1];
    SDL_Log("");
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            switch (map[y][x].type) {
                case TILE_TYPE_WALL: s[x] = 'x'; break;
                case TILE_TYPE_FLOOR: s[x] = '.'; break;
                case TILE_TYPE_NOTHING: s[x] = '~'; break;
                default: s[x] = '?'; break;

            }
        }
        s[COLS] = '\0';
        SDL_Log(s);
    }
}

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

static uint32_t random_seed = 0x17041971;

static uint32_t next_rand()
{
    return random_seed = random_seed * 134775813 + 1;
}

uint32_t random(uint32_t not_included_max)
{
    uint64_t result = (uint64_t)not_included_max * (uint64_t)next_rand();
    return result >> 32;
}

int random_range(int included_min, int included_max)
{
    SDL_assert(included_min <= included_max);
    return included_min + random(included_max - included_min + 1);
}

struct room {
    int x, y, w, h;
};

void split_room(const struct room* room, bool hor)
{
    if (room->w < 7 && room->h < 7)
        return;

    if (room->w < 15 && room->h < 15 && random(100) < 50)
        return;

    hor = room->w < 7 ? true : (room->h < 7 ? false : hor);

    int x, y, w, h;
    if (hor) {
        y = room->y + 3 + random(room->h - 6);
        split_room(&(struct room) { room->x, room->y, room->w, y - room->y }, false);
        split_room(&(struct room) { room->x, y + 1, room->w, room->y + room->h - y - 1 }, false);
        for (int n = 0; n < room->w; n++) {
            map[y][room->x + n].type = TILE_TYPE_WALL;
        }
    } else {
        x = room->x + 3 + random(room->w - 6);
        split_room(&(struct room) { room->x, room->y, x - room->x, room->h }, true);
        split_room(&(struct room) { x + 1, room->y, room->x + room->w - x - 1, room->h }, true);
        for (int n = 0; n < room->h; n++)
            map[room->y + n][x].type = TILE_TYPE_WALL;
    }
}

void split_room2(const struct room* room)
{
    if (room->w < 11 && room->h < 5)
        return;

    int area = room->w * room->h;

    if (area < 50)
        return;

    if (area < 225 && random(100) < 50)
        return;

    bool hor;
    if (room->w < 11)
        hor = true;
    else if (room->h < 5) {
        hor = false;
    } else {
        if (room->w >= room->h * 3)
            hor = false;
        else if (room->h * 4 >= room->w * 3)
            hor = true;
        else {
            int r = random(room->w + room->h * 2);
            hor = r >= room->w;
        }
    }

    if (hor) {
        int y = room->y + room->h / 4 + random(room->h / 2);
        split_room2(&(struct room) { room->x, room->y, room->w, y - room->y });
        split_room2(&(struct room) { room->x, y + 1, room->w, room->y + room->h - y - 1 });
        for (int n = 0; n < room->w; n++) {
            map[y][room->x + n].type = TILE_TYPE_WALL;
        }
    } else {
        int x = room->x + room->w / 4 + random(room->w / 2);
        split_room2(&(struct room) { room->x, room->y, x - room->x, room->h });
        split_room2(&(struct room) { x + 1, room->y, room->x + room->w - x - 1, room->h });
        for (int n = 0; n < room->h; n++)
            map[room->y + n][x].type = TILE_TYPE_WALL;
    }
}

bool is_wall_or_outside(int x, int y)
{
    return x < 0 || y < 0 || x >= COLS || y >= ROWS || map[y][x].type == TILE_TYPE_WALL;
}

bool is_tile_valid(int x, int y)
{
    return x >= 0 && y >= 0 && x < COLS&& y < ROWS;
}

bool is_tile_of_type(int x, int y, enum tile_type type)
{
    return is_tile_valid(x, y) ? map[y][x].type == type : type == TILE_TYPE_NOTHING;
}

bool is_area_empty(int x, int y, int w, int h)
{
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            if (is_wall_or_outside(x + i, y + j))
                return false;
        }
    }
    return true;
}

bool is_area_of_type(int x, int y, int w, int h, enum tile_type type)
{
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            if (!is_tile_of_type(x + i, y + j, type))
                return false;
        }
    }
    return true;
}

bool can_room_placed(const struct room* room)
{
    int x1 = room->x;
    int x2 = room->x + room->w;
    int y1 = room->y;
    int y2 = room->y + room->h;

    if (x1 == 0 && y1 == 0) {
        int m = 7;
    }

    if (x1 > 0 && x1 < 7) return false;
    if (y1 > 0 && y1 < 4) return false;
    if (x2 < COLS && x2 > COLS - 7)return false;
    if (y2 < ROWS && y2 > ROWS - 4) return false;

    if (x1 > 0) {

        // top-left area
        if (y1 > 0) {
            if (!is_area_of_type(x1 - 3, y1 - 3, 3, 3, TILE_TYPE_FLOOR))
                return false;
        }

        // left area
        // top - can be complete wall or complete not wall
        if (!is_area_of_type(x1 - 3, y1, 3, 1, TILE_TYPE_WALL) && !is_area_of_type(x1 - 1, y1, 3, 1, TILE_TYPE_FLOOR))
            return false;
        if (!is_area_of_type(x1 - 3, y1 + 1, 3, 3, TILE_TYPE_FLOOR))
            return false;
        if (y1 + 4 < y2 - 4)

            if (!is_area_empty(x1 - 3, y2 - 4, 3, 4))
                return false;

        //bot-left area
        if (y2 < ROWS) {
            if (!is_area_empty(x1 - 3, y2, 3, 3))
                return false;
        }
    }

    if (y1 > 0) {
        // top area
        if (!is_area_empty(x1, y1 - 3, 4, 3))
            return false;
        if (!is_area_empty(x2 - 4, y1 - 3, 4, 3))
            return false;
    }

    if (y2 < ROWS) {
        // bot area
        if (!is_area_empty(x1, y2, 4, 3))
            return false;
        if (!is_area_empty(x2 - 4, y2, 4, 3))
            return false;
    }

    if (x2 < COLS) {
        if (y1 > 0) {
            // top-right
            if (!is_area_empty(x2, y1 - 3, 3, 3))
                return false;
        }
        // right
        if (!is_area_empty(x2, y1, 3, 4))
            return false;
        if (!is_area_empty(x2, y2 - 4, 3, 4))
            return false;

        if (y2 < ROWS) {
            // bot-right
            if (!is_area_empty(x2, y2, 3, 3))
                return false;
        }
    }

    return true;
}

struct pattern {
    const char* t[7];
    int rot;
};

struct pattern rot_patterns[] = {

    {
        {
            "xxxxxxx",
            "x      ",
            "x      ",
            "x      ",
            "x      ",
            "x      ",
            "x      "
        }, 4
    },

    {
        {
            "xxxxxxx",
            "       ",
            "       ",
            "       ",
            "       ",
            "       ",
            "       "
        }, 4
    },

    {
        {
            "xxxxxxx",
            "x     x",
            "x     x",
            "x     x",
            "x     x",
            "x     x",
            "x     x",
        }, 4
    },

    {
        {
            "xxxxxxx",
            "       ",
            "       ",
            "       ",
            "       ",
            "       ",
            "xxxxxxx"
        }, 2
    },

    {
        {
            "xxxxxxx",
            "x     x",
            "x     x",
            "x     x",
            "x     x",
            "x     x",
            "xxxxxxx",
        }, 1
    },

    {
        {
            "       ",
            "       ",
            "       ",
            "       ",
            "       ",
            "       ",
            "       ",
        }, 1
    },

    {
        {
            "x      ",
            "       ",
            "       ",
            "       ",
            "       ",
            "       ",
            "       ",
        }, 4
    },

    {
        {
            "x     x",
            "       ",
            "       ",
            "       ",
            "       ",
            "       ",
            "       ",
        }, 4
    },

    {
        {
            "x      ",
            "       ",
            "       ",
            "       ",
            "       ",
            "       ",
            "      x",
        }, 2
    },

    {
        {
            "x     x",
            "       ",
            "       ",
            "       ",
            "       ",
            "       ",
            "x      ",
        }, 4
    },

    {
        {
            "x     x",
            "       ",
            "       ",
            "       ",
            "       ",
            "       ",
            "x     x",
        }, 1
    },

    {
        {
            "x      ",
            "       ",
            "       ",
            "       ",
            "       ",
            "       ",
            "xxxxxxx",
        }, 4
    },

    {
        {
            "x     x",
            "      x",
            "      x",
            "      x",
            "      x",
            "      x",
            "      x",
        }, 4
    },

    {
        {
            "x     x",
            "       ",
            "       ",
            "       ",
            "       ",
            "       ",
            "xxxxxxx",
        }, 4
    },

    {
        {
            "x     x",
            "      x",
            "      x",
            "      x",
            "      x",
            "      x",
            "xxxxxxx",
        }, 4
    }
};

char* patterns[] = {

    "xxxxxxx",
    "x      ",
    "x      ",
    "x      ",
    "x      ",
    "x      ",
    "x      ",

    "xxxxxxx",
    "      x",
    "      x",
    "      x",
    "      x",
    "      x",
    "      x",

    "      x",
    "      x",
    "      x",
    "      x",
    "      x",
    "      x",
    "xxxxxxx",

    "x      ",
    "x      ",
    "x      ",
    "x      ",
    "x      ",
    "x      ",
    "xxxxxxx",

    "xxxxxxx",
    "       ",
    "       ",
    "       ",
    "       ",
    "       ",
    "       ",

    "      x",
    "      x",
    "      x",
    "      x",
    "      x",
    "      x",
    "      x",

    "       ",
    "       ",
    "       ",
    "       ",
    "       ",
    "       ",
    "xxxxxxx",

    "x      ",
    "x      ",
    "x      ",
    "x      ",
    "x      ",
    "x      ",
    "x      ",

    "xxxxxxx",
    "x     x",
    "x     x",
    "x     x",
    "x     x",
    "x     x",
    "x     x",

    "xxxxxxx",
    "      x",
    "      x",
    "      x",
    "      x",
    "      x",
    "xxxxxxx",

    "x     x",
    "x     x",
    "x     x",
    "x     x",
    "x     x",
    "x     x",
    "xxxxxxx",

    "xxxxxxx",
    "x      ",
    "x      ",
    "x      ",
    "x      ",
    "x      ",
    "xxxxxxx",

    "xxxxxxx",
    "x     x",
    "x     x",
    "x     x",
    "x     x",
    "x     x",
    "xxxxxxx",

    "       ",
    "       ",
    "       ",
    "       ",
    "       ",
    "       ",
    "       ",

    "x      ",
    "       ",
    "       ",
    "       ",
    "       ",
    "       ",
    "       ",

};

static char(*xpatterns)[7][7];
static int num_xpat;

int field_val(int col, int row)
{
    return
        128 * gfield[row - 1][col - 1]
        + 64 * gfield[row][col - 1]
        + 32 * gfield[row + 1][col - 1]
        + 16 * gfield[row + 1][col]
        + 8 * gfield[row + 1][col + 1]
        + 4 * gfield[row][col + 1]
        + 2 * gfield[row - 1][col + 1]
        + gfield[row - 1][col];
}

int fieldd(int col, int row)
{
    // may be outside field ... return value at col row
    col = col < 0 ? col + GFW : (col >= GFW ? col - GFW : col);
    row = row < 0 ? row + GFH : (row >= GFH ? row - GFH : row);
    return gfield[row][col];
}

struct cell_template {
    int val;
    const char* t;
} templates[] = {
    {
        .val = 1,
        .t =
        "xxx xxx"
        "x     x"
        "x     x"
        "x     x"
        "xxxxxxx"
    },
    {
        .val = 4,
        .t =
        "xxxxxxx"
        "x     x"
        "x      "
        "x     x"
        "xxxxxxx"
    },
    {
        .val = 5,
        .t =
        "xxx xxx"
        "x     x"
        "x      "
        "x     x"
        "xxxxxxx"
    },
    {
        .val = 7,
        .t =
        "x      "
        "x      "
        "x      "
        "x      "
        "xxxxxxx"
    },
    {
        .val = 17,
        .t =
        "xxx xxx"
        "x     x"
        "x     x"
        "x     x"
        "xxx xxx"
    },
    {
        .val = 20,
        .t =
        "xxxxxxx"
        "x     x"
        "x      "
        "x     x"
        "xxx xxx"
    },
    {
        .val = 28,
        .t =
        "xxxxxxx"
        "x      "
        "x      "
        "x      "
        "x      "
    },
    {
        .val = 71,
        .t =
        "x      "
        "x      "
        "       "
        "x      "
        "xxxxxxx"
    },
    {
        .val = 92,
        .t =
        "xxxxxxx"
        "x      "
        "       "
        "x      "
        "x      "
    },
    {
        .val = 112,
        .t =
        "xxxxxxx"
        "      x"
        "      x"
        "      x"
        "      x"
    }
};

void putcell(int col, int row)
{
    int val = field_val(col, row);
    gfield[row][col] = val;
    // update image ...
    int mx = (col / 2) * 6;
    int my = (row / 2) * 4;

    // char k[5 * 7];

    // if (val == 0) {
    //     for (int n = 0; n < 5 * 7; n++)
    //         k[n] = 'x';
    // } else if (val == 255) {
    //     for (int n = 0; n < 5 * 7; n++)
    //         k[n] = ' ';
    // } else {
    //     for (int y = 1; y < 4; y++) {
    //         for (int x = 1; x < 6; x++) map[my + y][mx + x].type = TILE_TYPE_FLOOR;
    //     }
    // }

    // const char* t = NULL;
    // for (int n = 0; n < SDL_arraysize(templates); n++) {
    //     if (templates[n].val == val) {
    //         t = templates[n].t;
    //     }
    // }

    // if (t) {
    //     for (int y = 0; y < 5; y++) {
    //         for (int x = 0; x < 7; x++) {
    //             map[my + y][mx + x].type = t[y * 7 + x] == 'x' ? TILE_TYPE_WALL : TILE_TYPE_FLOOR;
    //         }
    //     }
    // } else 
    {
        bool nFloor = (val & 1);
        bool neFloor = (val & 2);
        bool nwFloor = (val & 128);
        bool wFloor = (val & 64);
        bool swFloor = (val & 32);
        bool eFloor = (val & 4);
        bool seFloor = (val & 8);
        bool sFloor = (val & 16);

        bool way = !neFloor && !nwFloor && !swFloor && !seFloor;

        if (way) {
            for (int y = 0; y < 5; y++) {
                for (int x = 0; x < 7; x++) {
                    map[my + y][mx + x].type = TILE_TYPE_WALL;
                }
            }

            if (nFloor) {
                map[my + 0][mx + 3].type = map[my + 0][mx + 4].type = TILE_TYPE_FLOOR;
                map[my + 1][mx + 3].type = map[my + 1][mx + 4].type = TILE_TYPE_FLOOR;
                map[my + 2][mx + 3].type = map[my + 2][mx + 4].type = TILE_TYPE_FLOOR;
            }
            if (sFloor) {
                map[my + 2][mx + 3].type = map[my + 2][mx + 4].type = TILE_TYPE_FLOOR;
                map[my + 3][mx + 3].type = map[my + 3][mx + 4].type = TILE_TYPE_FLOOR;
                map[my + 4][mx + 3].type = map[my + 4][mx + 4].type = TILE_TYPE_FLOOR;
            }
            if (wFloor) {
                map[my + 2][mx + 0].type = TILE_TYPE_FLOOR;
                map[my + 2][mx + 1].type = TILE_TYPE_FLOOR;
                map[my + 2][mx + 2].type = TILE_TYPE_FLOOR;
                map[my + 2][mx + 3].type = TILE_TYPE_FLOOR;
            }
            if (eFloor) {
                map[my + 2][mx + 3].type = TILE_TYPE_FLOOR;
                map[my + 2][mx + 4].type = TILE_TYPE_FLOOR;
                map[my + 2][mx + 5].type = TILE_TYPE_FLOOR;
                map[my + 2][mx + 6].type = TILE_TYPE_FLOOR;
            }
        } else {

            map[my][mx + 0].type = nwFloor ? TILE_TYPE_FLOOR : TILE_TYPE_WALL;
            map[my][mx + 1].type = map[my][mx + 2].type = (!nFloor || (!neFloor && !nwFloor)) ? TILE_TYPE_WALL : TILE_TYPE_FLOOR;
            map[my][mx + 3].type = map[my][mx + 4].type = nFloor ? TILE_TYPE_FLOOR : TILE_TYPE_WALL;
            map[my][mx + 5].type = (!nFloor || (!neFloor && !nwFloor)) ? TILE_TYPE_WALL : TILE_TYPE_FLOOR;
            map[my][mx + 6].type = neFloor ? TILE_TYPE_FLOOR : TILE_TYPE_WALL;

            map[my + 1][mx + 0].type = (!wFloor || (!nwFloor && !swFloor)) ? TILE_TYPE_WALL : TILE_TYPE_FLOOR;
            map[my + 2][mx + 0].type = wFloor ? TILE_TYPE_FLOOR : TILE_TYPE_WALL;
            map[my + 3][mx + 0].type = (!wFloor || (!nwFloor && !swFloor)) ? TILE_TYPE_WALL : TILE_TYPE_FLOOR;
            
            map[my + 1][mx + 6].type = (!eFloor || (!neFloor && !seFloor)) ? TILE_TYPE_WALL : TILE_TYPE_FLOOR;
            map[my + 2][mx + 6].type = eFloor ? TILE_TYPE_FLOOR : TILE_TYPE_WALL;
            map[my + 3][mx + 6].type = (!eFloor || (!neFloor && !seFloor)) ? TILE_TYPE_WALL : TILE_TYPE_FLOOR;

            for (int y = 1; y < 4; y++) {
                for (int x = 1; x < 6; x++) map[my + y][mx + x].type = TILE_TYPE_FLOOR;
            }

            map[my + 4][mx + 0].type = swFloor ? TILE_TYPE_FLOOR : TILE_TYPE_WALL;
            map[my + 4][mx + 1].type = map[my + 4][mx + 2].type = (!sFloor || (!seFloor && !swFloor)) ? TILE_TYPE_WALL : TILE_TYPE_FLOOR;
            map[my + 4][mx + 3].type = map[my + 4][mx + 4].type = sFloor ? TILE_TYPE_FLOOR : TILE_TYPE_WALL;
            map[my + 4][mx + 5].type = (!sFloor || (!seFloor && !swFloor)) ? TILE_TYPE_WALL : TILE_TYPE_FLOOR;
            map[my + 4][mx + 6].type = seFloor ? TILE_TYPE_FLOOR : TILE_TYPE_WALL;
        }
    }
}

void set_blob_edge(int col, int row, int dx, int dy)
{
    gfield[row + dy][col + dx] = 1;
    // update 2 tiles
    putcell(col, row);
    putcell(col + 2 * dx, row + 2 * dy);
}

void set_blob_corn(int col, int row, int dx, int dy)
{
    gfield[row + dy][col + dx] = 1;
    gfield[row][col + dx] = 1;
    gfield[row + dy][col] = 1;
    gfield[row + dy][col + 2 * dx] = 1;
    gfield[row + 2 * dy][col + dx] = 1;
    // update 4 tiles
    putcell(col, row);
    putcell(col + 2 * dx, row);
    putcell(col, row + 2 * dy);
    putcell(col + 2 * dx, row + 2 * dy);
}

void create_map()
{
    memset(gfield, 0, sizeof(gfield));

    int rx = random(GW) * 2 + 1;
    int ry = random(GH) * 2 + 1;

    struct { int x, y; } path[GW * GH];
    int num = 0;

    path[num].x = rx;
    path[num].y = ry;
    num++;

    while (num > 0) {

        int idx = num - 1;
        int col = path[idx].x;
        int row = path[idx].y;

        struct { int x, y; } neighbors[4];
        int num_neighbors = 0;

        if (row > 2 && gfield[row - 2][col] == 0) { neighbors[num_neighbors].x = 0; neighbors[num_neighbors++].y = -1; }
        if (col < GFW - 3 && gfield[row][col + 2] == 0) { neighbors[num_neighbors].x = 1; neighbors[num_neighbors++].y = 0; }
        if (row < GFH - 3 && gfield[row + 2][col] == 0) { neighbors[num_neighbors].x = 0; neighbors[num_neighbors++].y = 1; }
        if (col > 2 && gfield[row][col - 2] == 0) { neighbors[num_neighbors].x = -1; neighbors[num_neighbors++].y = 0; }

        if (num_neighbors > 0) {
            int next = random(num_neighbors);
            int dx = neighbors[next].x;
            int dy = neighbors[next].y;

            set_blob_edge(col, row, dx, dy);

            int cnt, dxx, dyy;
            // add up the 3 possible edges to the left
            cnt = 0;
            if (gfield[row - dx][col + dy] == 1) { cnt += 1; }
            if (fieldd(col + dx + 2 * dy, row - 2 * dx + dy) == 1) { cnt += 1; }
            if (fieldd(col + 2 * dx + dy, row - dx + 2 * dy) == 1) { cnt += 1; }
            if (cnt == 2) {  // then 3 edges
                if (dx == 0) { dxx = dy; } else { dxx = dx; }
                if (dy == 0) { dyy = -dx; } else { dyy = dy; }
                set_blob_corn(col, row, dxx, dyy);
            }
            // add up the 3 possible edges to the rite
            cnt = 0;
            if (gfield[row + dx][col - dy] == 1) { cnt += 1; }
            if (fieldd(col + dx - 2 * dy, row + 2 * dx + dy) == 1) { cnt += 1; }
            if (fieldd(col + 2 * dx - dy, row + dx + 2 * dy) == 1) { cnt += 1; }
            if (cnt == 2) {  // then 3 edges
                if (dx == 0) { dxx = -dy; } else { dxx = dx; }
                if (dy == 0) { dyy = dx; } else { dyy = dy; }
                set_blob_corn(col, row, dxx, dyy);
            }

            path[num].x = col + 2 * dx;
            path[num++].y = row + 2 * dy;
        } else {  // no neighbors
            num--;
        }
    }

    int k = 7;

    // for (int y = 0; y < ROWS; y++) {
    //     for (int x = 0; x < COLS; x++) {
    //         if (x == 0 || y == 0 || x == COLS - 1 || y == ROWS - 1) {
    //             map[y][x].type = TILE_TYPE_WALL;
    //         } else {
    //             map[y][x].type = TILE_TYPE_FLOOR;
    //         }
    //     }
    // }


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

    random_seed = 1;
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
                    switch (event.key.keysym.sym) {
                        case 'c':
                            create_map();
                            break;
                        default:
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
        }

        render();
        SDL_RenderPresent(g.renderer);
    }

    SDL_DestroyWindow(g.window);

    SDL_Quit();
    return 0;
}