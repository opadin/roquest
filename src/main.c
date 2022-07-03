#include <stdio.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// TODO tile-size should be variable
const int TILE_WIDTH = 8;
const int TILE_HEIGHT = 16;

const int WINDOW_WIDTH = TILE_WIDTH * 100;
const int WINDOW_HEIGHT = TILE_HEIGHT * 34;

const int ZOOMX = 1;
const int ZOOMY = 1;

#define NUM_COL_PATTERN 10
#define NUM_ROW_PATTERN 5

#define COLS  (7 + (NUM_COL_PATTERN-1) * 6)
#define ROWS  (7 + (NUM_ROW_PATTERN-1) * 6)

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

void create_map()
{
    if (!xpatterns) {

        num_xpat = 0;
        for (int j = 0; j < SDL_arraysize(rot_patterns); j++) {
            num_xpat += rot_patterns[j].rot;
        }

        xpatterns = malloc(7 * 7 * num_xpat);

        char(*p)[7][7] = xpatterns;
        for (int j = 0; j < SDL_arraysize(rot_patterns); j++) {

            int rot = rot_patterns[j].rot;

            if (rot == 1) {
                for (int i = 0; i < 49; i++) {
                    int x = i % 7;
                    int y = i / 7;
                    p[0][y][x] = rot_patterns[j].t[y][x];
                }
                p++;
            } else if (rot == 2) {
                for (int i = 0; i < 49; i++) {
                    int x = i % 7;
                    int y = i / 7;
                    p[0][y][x] = rot_patterns[j].t[y][x];
                }
                p++;
                for (int i = 0; i < 49; i++) {
                    int x = i % 7;
                    int y = i / 7;
                    p[0][y][x] = rot_patterns[j].t[6 - x][y];
                }
                p++;
            } else {
                SDL_assert(rot == 4);
                for (int k = 0; k < 4; k++) {
                    for (int i = 0; i < 49; i++) {
                        int x = i % 7;
                        int y = i / 7;
                        switch (k) {
                            case 0: p[0][y][x] = rot_patterns[j].t[y][x]; break;
                            case 1: p[0][x][6-y] = rot_patterns[j].t[y][x]; break;
                            case 2: p[0][6-y][6-x] = rot_patterns[j].t[y][x]; break;
                            case 3: p[0][6-x][y] = rot_patterns[j].t[y][x]; break;
                        }
                    }
                    p++;
                }
            }

        }

        SDL_assert(xpatterns + num_xpat == p);
    }

    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            if (x == 0 || y == 0 || x == COLS - 1 || y == ROWS - 1) {
                map[y][x].type = TILE_TYPE_WALL;
            } else {
                map[y][x].type = TILE_TYPE_NOTHING;
            }
        }
    }

    unsigned int num_patterns = SDL_arraysize(patterns) / 7;

    for (int row = 0; row < NUM_ROW_PATTERN; row++) {
        for (int col = 0; col < NUM_COL_PATTERN; col++) {

            for (;;) {

                int my = row * 6;
                int mx = col * 6;

                // int pattern = random(num_patterns);
                // char** p = &patterns[pattern * 7];
                int pattern = random(num_xpat);
                char(*p)[7][7] = &xpatterns[pattern];

                bool ok = true;
                for (int j = 0; j < 7; j++) {
                    for (int i = 0; i < 7; i++) {

                        if (i > 0 && j > 0 && i < 6 && j < 6)
                            continue;

                        // if (j > 0 && col < NUM_COL_PATTERN - 1 && i == 6)
                        //     continue;
                        // if (i > 0 && row < NUM_ROW_PATTERN - 1 && j == 6)
                        //     continue;

                        enum tile_type t = map[my + j][mx + i].type;

                        // if (col == 0 && i == 0)
                        //     t = TILE_TYPE_WALL;
                        // else if (col == NUM_COL_PATTERN - 1 && i == 7 - 1)
                        //     t = TILE_TYPE_WALL;

                        // if (row == 0 && j == 0)
                        //     t = TILE_TYPE_WALL;
                        // else if (row == NUM_ROW_PATTERN - 1 && j == 7 - 1)
                        //     t = TILE_TYPE_WALL;

                        switch (p[0][j][i]) {
                            case 'x':
                                ok = t == TILE_TYPE_NOTHING || t == TILE_TYPE_WALL;
                                break;
                            case ' ':
                                ok = t == TILE_TYPE_NOTHING || t == TILE_TYPE_FLOOR;
                                break;
                            default:
                                SDL_assert(false);
                                break;
                        }

                        if (!ok)
                            break;

                    }

                    if (!ok)
                        break;
                }

                if (ok) {

                    for (int j = 0; j < 7; j++) {
                        for (int i = 0; i < 7; i++) {
                            map[my + j][mx + i].type = p[0][j][i] == 'x' ? TILE_TYPE_WALL : (p[0][j][i] == ' ' ? TILE_TYPE_FLOOR : TILE_TYPE_NOTHING);
                        }
                    }

                    #if 0
                    dump_map();
                    #endif

                    break;
                }
            }

        }

    }
}

// // border
// for (int y = 0; y < ROWS; y++) {
//     for (int x = 0; x < COLS; x++) {
//         if (x == 0 || y == 0 || x == COLS - 1 || y == ROWS - 1) {
//             map[y][x].type = TILE_TYPE_WALL;
//         } else {
//             map[y][x].type = TILE_TYPE_FLOOR;
//         }
//     }
// }

// int coords[COLS * ROWS];
// for (int n = 0; n < COLS * ROWS; n++) coords[n] = n;
// for (int n = 0; n < COLS * ROWS; n++) {
//     int k = random_range(0, COLS * ROWS - 1);
//     if (n != k) {
//         int tmp = coords[n];
//         coords[n] = coords[k];
//         coords[k] = tmp;
//     }
// }

// for (int n = 0; n < 100; n++) {
//     int w = random_range(8, 20);
//     int h = random_range(5, 11);

//     for (int k = 0; k < COLS * ROWS; k++) {

//         int x = coords[k] % COLS;
//         int y = coords[k] / COLS;

//         if (x + w > COLS || y + h > ROWS)
//             continue;

//         if (can_room_placed(&(struct room) { x, y, w, h }))
//         {
//             // dig it
//             for (int j = 0; j < h; j++) {
//                 for (int i = 0; i < w; i++) {
//                     map[y + j][x + i].type = (i == 0 || j == 0 || i == w - 1 || j == h - 1) ? TILE_TYPE_WALL : TILE_TYPE_FLOOR;
//                 }
//             }
//             break;
//         }
//     }

// }


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