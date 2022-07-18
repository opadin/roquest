#include <stdio.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// TODO tile-size should be variable
const int TILE_WIDTH = 8;
const int TILE_HEIGHT = 16;

const int ZOOMX = 1;
const int ZOOMY = 1;

#define COLS  80
#define ROWS  45

#define SCREEN_COLS ((COLS)+20)
#define SCREEN_ROWS ((ROWS)+5)

const int WINDOW_WIDTH = TILE_WIDTH * SCREEN_COLS;
const int WINDOW_HEIGHT = TILE_HEIGHT * SCREEN_ROWS;

const int MAX_ROOMS_PER_MAP = 30;
const int VIEW_RADIUS = 10;

#define MAX_ENTS 128

enum action_type {
    ACTION_TYPE_NONE = 0,
    ACTION_TYPE_ESCAPE,
    ACTION_TYPE_BUMP,
    ACTION_TYPE_WAIT,
    ACTION_TYPE_MOVE,
    ACTION_TYPE_MELEE
};

struct action {
    enum action_type type;
    int param1;
    int param2;
    struct ent* actor;
};

enum direction {
    DIR_NOTHING = 0,
    DIR_DOWN = 1,
    DIR_LEFT = 2,
    DIR_RIGHT = 3,
    DIR_UP = 4
};

struct global {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* font;
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
    struct {
        int visible : 1;
        int explored : 1;
    };
    enum tile_type type;
};

static struct map_tile map[ROWS][COLS];

struct ent {
    bool used;
    int ch;
    int x, y;
};

// ent #0 is player
static struct ent ents[MAX_ENTS];

int maxi(int a, int b) { return a >= b ? a : b; }
int mini(int a, int b) { return a <= b ? a : b; }

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

void render_tile(int x, int y, int ch, struct color color, bool visible)
{
    ch &= 0xff;
    int srcx = (ch % 16) * TILE_WIDTH;
    int srcy = (ch / 16) * TILE_HEIGHT;
    int dstx = x * TILE_WIDTH * ZOOMX;
    int dsty = y * TILE_HEIGHT * ZOOMY;

    if (!visible) {
        color.red = maxi(0, color.red - 70);
        color.blue = maxi(0, color.blue - 70);
        color.green = maxi(0, color.green - 70);
    }

    SDL_SetTextureColorMod(g.font, color.red, color.green, color.blue);
    SDL_RenderCopy(g.renderer, g.font, &(SDL_Rect) { srcx, srcy, TILE_WIDTH, TILE_HEIGHT}, & (SDL_Rect) { dstx, dsty, TILE_WIDTH* ZOOMX, TILE_HEIGHT* ZOOMY });
}

void render_tile_with_bg(int x, int y, int ch, struct color fg, struct color bg, bool visible)
{

    render_tile(x, y, 0xdb, bg, visible);
    render_tile(x, y, ch, fg, visible);
}

void render()
{
    int sx = (WINDOW_WIDTH / (TILE_WIDTH * ZOOMX) - COLS) / 2;
    int sy = (WINDOW_HEIGHT / (TILE_HEIGHT * ZOOMY) - ROWS) / 2;

    // simple blocked map
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            if (!map[y][x].explored) {
                render_tile(sx + x, sy + y, 0xdb, (struct color) { 0, 0, 0 }, false);
            } else {
                bool vis = map[y][x].visible;
                switch (map[y][x].type) {
                    case TILE_TYPE_WALL:
                        if (y < ROWS - 1 && map[y + 1][x].type == TILE_TYPE_WALL) {
                            render_tile(sx + x, sy + y, 0xdb, WALL_TOP_COLOR, vis);
                        } else {
                            if (x > 0 && map[y][x - 1].type == TILE_TYPE_WALL && y < ROWS - 1 && map[y + 1][x - 1].type == TILE_TYPE_WALL) {
                                render_tile_with_bg(sx + x, sy + y, 0xdf, WALL_TOP_COLOR, WALL_SIDE_SHADOW_COLOR, vis);
                            } else {
                                render_tile_with_bg(sx + x, sy + y, 0xdf, WALL_TOP_COLOR, WALL_SIDE_COLOR, vis);
                            }
                        }
                        break;
                    case TILE_TYPE_FLOOR:
                        if (x > 0 && map[y][x - 1].type == TILE_TYPE_WALL) {
                            render_tile_with_bg(sx + x, sy + y, 0xdd, FLOOR_SHADOW_COLOR, FLOOR_COLOR, vis);
                        } else {
                            render_tile(sx + x, sy + y, 0xdb, FLOOR_COLOR, vis);
                        }
                        break;
                    case TILE_TYPE_NOTHING:
                        render_tile(sx + x, sy + y, 0xdb, (struct color) { 31, 31, 31 }, vis);
                        break;
                    default:
                        // something wrong!
                        render_tile(sx + x, sy + y, '?', (struct color) { 255, 0, 0 }, vis);
                        break;

                }
            }
        }
    }

    // player + monsters
    for (int k = 0; k < MAX_ENTS; k++) {
        if (ents[k].used && map[ents[k].y][ents[k].x].visible) {
            struct color c;
            switch (ents[k].ch) {
                case '@': c = (struct color){ 255, 255, 255 }; break;
                case 'o': c = (struct color){ 191, 191, 0 }; break;
                case 'T': c = (struct color){ 255, 255, 0 }; break;
                default: c = (struct color){ 255, 0, 0 }; break;
            }
            render_tile(sx + ents[k].x, sy + ents[k].y, ents[k].ch, c, 1);
        }
    }
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
    int ax, ay, bx, by;
};

void hline(int x, int y, int len)
{
    for (int j = 0; j < len; j++)
        map[y][x + j].type = TILE_TYPE_FLOOR;
}

void create_map()
{
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            if (x == 0 || y == 0 || x == COLS - 1 || y == ROWS - 1) {
                map[y][x].type = TILE_TYPE_WALL;
            } else {
                map[y][x].type = TILE_TYPE_FLOOR;
            }
        }
    }

    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            map[y][x].type = TILE_TYPE_WALL;
            map[y][x].visible = 0;
            map[y][x].explored = 0;
        }
    }

    for (int n = 0; n < MAX_ENTS; n++)
        ents[n].used = 0;

    struct room rooms[MAX_ROOMS_PER_MAP];
    int num_rooms = 0;

    for (int n = 0; n < MAX_ROOMS_PER_MAP; n++) {

        int w = random_range(6, 10);
        int h = random_range(4, 6);
        int x = random_range(1, COLS - w - 1);
        int y = random_range(1, ROWS - h - 1);

        bool intersects = false;
        for (int i = 0; i < num_rooms; i++) {
            if (rooms[i].bx >= x && rooms[i].ax <= x + w && rooms[i].by >= y && rooms[i].ay <= y + h) {
                intersects = true;
                break;
            }
        }

        if (!intersects) {

            for (int j = 0; j < h; j++) {
                for (int i = 0; i < w; i++) {
                    // if (j == 0) {
                    //     map[y + j - 1][x + i].type = TILE_TYPE_WALL;
                    // }
                    map[y + j][x + i].type = TILE_TYPE_FLOOR;
                }
            }

            if (num_rooms == 0) {
                ents[0] = (struct ent) {
                    .used = 1,
                    .x = x + w / 2,
                    .y = y + h / 2,
                    .ch = '@'
                };
            } else {
                struct room* prev_room = &rooms[num_rooms - 1];

                // prev center
                int pcx = (prev_room->ax + prev_room->bx) / 2;
                int pcy = (prev_room->ay + prev_room->by) / 2;

                // new room center
                int ncx = x + w / 2;
                int ncy = y + h / 2;

                // tunnel first hor or vert?
                bool horizontal = random(100) < 50;

                // coords
                int kx, ky;
                if (horizontal) {
                    kx = ncx;
                    ky = pcy;
                } else {
                    kx = pcx;
                    ky = ncy;
                }

                for (;;) {
                    map[ky][pcx].type = TILE_TYPE_FLOOR;
                    if (pcx == ncx)
                        break;
                    if (pcx < ncx) pcx++; else pcx--;
                };

                for (;;) {
                    map[pcy][kx].type = TILE_TYPE_FLOOR;
                    if (pcy == ncy)
                        break;
                    if (pcy < ncy) pcy++; else pcy--;
                }


            }

            rooms[num_rooms++] = (struct room){ .ax = x, .ay = y, .bx = x + w, .by = y + h };
            SDL_Log("Room %d : %d/%d-%d/%d", num_rooms, x, y, w, h);

            // spawn monsters
            int num_monsters = random(3);
            for (int i = 0; i < num_monsters; i++) {
                int ex = x + random(w);
                int ey = y + random(h);
                bool found = false;
                for (int k = 0; k < MAX_ENTS; k++) {
                    if (ents[k].used && ents[k].x == x && ents[k].y == y) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    int k = 0;
                    while (k < MAX_ENTS && ents[k].used)
                        k++;
                    if (k < MAX_ENTS) {
                        ents[k].used = 1;
                        ents[k].x = ex;
                        ents[k].y = ey;
                        ents[k].ch = random(100) < 80 ? 'o' : 'T';
                        SDL_Log("  Mon#%d : %c (%d/%d)", k, ents[k].ch, ents[k].x, ents[k].y);
                    }
                }
            }
        }
    }
}

bool map_valid(int x, int y)
{
    return x >= 0 && x < COLS&& y >= 0 && y < ROWS;
}

void update_fov()
{
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            map[y][x].visible = 0;
        }
    }

    for (int i = 0; i < 360 * 8; i++)
    {
        float x = cos((float)i * 0.01745f);
        float y = sin((float)i * 0.01745f);

        float ox = (float)ents[0].x + 0.5f;
        float oy = (float)ents[0].y + 0.5f;
        for (int j = 0; j < VIEW_RADIUS; j++)
        {
            int mx = (int)ox;
            int my = (int)oy;
            if (!map_valid(mx, my))
                break;
            map[my][mx].visible = true;
            map[my][mx].explored = true;
            if (map[my][mx].type != TILE_TYPE_FLOOR)
                break;
            ox += x;
            oy += y;
        }
    }
}

void move_player(enum direction dir)
{
    int nx = ents[0].x + (dir == DIR_RIGHT ? 1 : (dir == DIR_LEFT ? -1 : 0));
    int ny = ents[0].y + (dir == DIR_DOWN ? 1 : (dir == DIR_UP ? -1 : 0));
    if (map_valid(nx, ny) && map[ny][nx].type == TILE_TYPE_FLOOR) {
        for (int n = 1; n < MAX_ENTS; n++) {
            if (ents[n].used && ents[n].x == nx && ents[n].y == ny) {
                SDL_Log("You kick the %c, much to its annoyance!", ents[n].ch);
                return;
            }
        }
        ents[0].x = nx;
        ents[0].y = ny;
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

    random_seed = 1;
    create_map();
    update_fov();

    SDL_Event event;
    bool quit_requested = false;
    while (!quit_requested)
    {
        while (SDL_PollEvent(&event))
        {
            struct action action = { .type = ACTION_TYPE_NONE };
            switch (event.type) {
                case SDL_QUIT:
                    quit_requested = true;
                    break;
                case SDL_KEYDOWN:
                    switch (event.key.keysym.sym) {
                        case 'c':
                            create_map();
                            update_fov();
                            break;
                        default:
                            switch (event.key.keysym.scancode) {
                                case SDL_SCANCODE_UP:
                                    action = (struct action){ .type = ACTION_TYPE_BUMP, .param1 = DIR_UP, .actor = &ents[0] };
                                    break;
                                case SDL_SCANCODE_DOWN:
                                    action = (struct action){ .type = ACTION_TYPE_BUMP, .param1 = DIR_DOWN, .actor = &ents[0] };
                                    break;
                                case SDL_SCANCODE_LEFT:
                                    action = (struct action){ .type = ACTION_TYPE_BUMP, .param1 = DIR_LEFT, .actor = &ents[0] };
                                    break;
                                case SDL_SCANCODE_RIGHT:
                                    action = (struct action){ .type = ACTION_TYPE_BUMP, .param1 = DIR_RIGHT, .actor = &ents[0] };
                                    break;
                                case SDL_SCANCODE_KP_5:
                                case SDL_SCANCODE_PERIOD:
                                    action.type = ACTION_TYPE_WAIT;
                                    break;
                                case SDL_SCANCODE_ESCAPE:
                                    action.type = ACTION_TYPE_ESCAPE;
                                    break;
                                default:
                                    break;
                            }
                    }
            }
            
            if (action.type != ACTION_TYPE_NONE) {

                switch (action.type) {
                    case ACTION_TYPE_BUMP:
                        move_player(action.param1);
                        break;
                    case ACTION_TYPE_WAIT:
                        // do nothing
                        break;
                    case ACTION_TYPE_ESCAPE:
                        quit_requested = true;
                        break;
                }

                update_fov();
            }
        }

        render();
        SDL_RenderPresent(g.renderer);
    }

    SDL_DestroyWindow(g.window);

    SDL_Quit();
    return 0;
}