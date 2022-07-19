#include <stdio.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// TODO tile-size should be variable
const int TILE_WIDTH = 16;
const int TILE_HEIGHT = 16;

const int ZOOMX = 1;
const int ZOOMY = 1;

#define COLS  80
#define ROWS  45

#define SCREEN_COLS ((COLS)+20)
#define SCREEN_ROWS ((ROWS)+5)

const int WINDOW_WIDTH = TILE_WIDTH * ZOOMX * SCREEN_COLS;
const int WINDOW_HEIGHT = TILE_HEIGHT * ZOOMY * SCREEN_ROWS;

const int MAX_ROOMS_PER_MAP = 30;
const int VIEW_RADIUS = 10;

#define MAX_ACTORS 128
#define MAX_CORPSES MAX_ACTORS

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

struct color {
    uint8_t red, green, blue;
};

struct tile_graphic {
    char ch;
    struct color fg;
    struct color bg;
};

struct tile_info {
    struct {
        int walkable : 1;
        int transparent : 1;
    };
    struct tile_graphic dark;
    struct tile_graphic light;
};

struct tile_defs {
    struct tile_info floor, wall, shroud;
};

// order must be same as tile_type!
static struct tile_info tiles[] = {
    /* shroud */ {0, 0, {' ', {255, 255, 255}, {50, 50, 150}}, {' ', {255, 255, 255}, { 50,  50, 150}}},
    /* floor  */ {1, 1, {' ', {255, 255, 255}, {50, 50, 150}}, {' ', {255, 255, 255}, {200, 180,  50}}},
    /* wall   */ {0, 0, {' ', {255, 255, 255}, { 0,  0, 100}}, {' ', {255, 255, 255}, {130, 110,  50}}}
};

enum game_state {
    GAME_STATE_NONE,
    GAME_STATE_RUN,
    GAME_STATE_DEAD
};

struct global {
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* font;
    enum game_state state;
    bool quit_requested;
};

struct global g;

enum tile_type {
    TILE_TYPE_SHROUD,
    TILE_TYPE_FLOOR,
    TILE_TYPE_WALL
};

struct map_tile {
    struct {
        int visible : 1;
        int explored : 1;
    };
    enum tile_type type;
};

static struct map_tile map[ROWS][COLS];

enum render_order {
    RENDER_ORDER_CORPSE,
    RENDER_ORDER_ITEM,
    RENDER_ORDER_ACTOR
};

// static actor data infos
enum actor_type {
    ACTOR_TYPE_PLAYER,
    ACTOR_TYPE_ORC,
    ACTOR_TYPE_TROLL,
    NUM_ACTOR_TYPES
};

struct actor_info {
    char character;
    struct color color;
    const char* name;
    int max_hp;
    int defense;
    int power;
};

struct actor_info actor_catalog[NUM_ACTOR_TYPES] = {
    { '@', { 255, 255, 255}, "player", 30, 2, 5 },
    { 'o', {  63, 127,  63}, "Orc", 10, 0, 3 },
    { 'T', {   0, 127,   0}, "Troll", 16, 1, 4 }
};

struct actor {
    enum actor_type type;
    int x, y;
    bool alive;
    int hp;
};

// ent #0 is player
static struct actor actors[MAX_ACTORS];
static int num_actors;

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
                case TILE_TYPE_SHROUD: s[x] = '~'; break;
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
    // center map in window
    int sx = (WINDOW_WIDTH / (TILE_WIDTH * ZOOMX) - COLS) / 2;
    int sy = (WINDOW_HEIGHT / (TILE_HEIGHT * ZOOMY) - ROWS) / 2;

    // map
    for (int y = 0; y < ROWS; y++) {
        for (int x = 0; x < COLS; x++) {
            struct tile_graphic* tg;
            if (!map[y][x].explored) {
                tg = &tiles[0].light;
            } else {
                struct tile_info* ti = &tiles[map[y][x].type];
                tg = map[y][x].visible ? &ti->light : &ti->dark;
            }
            render_tile_with_bg(sx + x, sy + y, tg->ch, tg->fg, tg->bg, true);
        }
    }

    // entities: player + monsters
    for (int i = 0; i < 2; i++) {
        for (int k = 0; k < num_actors; k++) {
            struct actor* a = &actors[k];
            if ((i == 0) != a->alive) {
                if (map[a->y][a->x].visible) {
                    struct actor_info* e = &actor_catalog[a->type];
                    if (!a->alive)
                        render_tile(sx + a->x, sy + a->y, '%', (struct color) { 191, 0, 0 }, 1);
                    else
                        render_tile(sx + a->x, sy + a->y, e->character, e->color, 1);
                }
            }
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

void spawn_actor(enum actor_type type, int x, int y)
{
    if (num_actors < SDL_arraysize(actors)) {
        actors[num_actors++] = (struct actor){ .type = type, .x = x, .y = y, .hp = actor_catalog[type].max_hp, .alive = 1 };
        SDL_Log("  Actor #%d : %s (%d/%d)", num_actors, actor_catalog[type].name, x, y);
    }
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

    struct room rooms[MAX_ROOMS_PER_MAP];
    int num_rooms = 0;
    num_actors = 0;

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
                spawn_actor(ACTOR_TYPE_PLAYER, x + w / 2, y + h / 2);
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
                spawn_actor(random(100) < 80 ? ACTOR_TYPE_ORC : ACTOR_TYPE_TROLL, ex, ey);
            }
        }
    }
}

// in bounds
bool map_valid(int x, int y)
{
    return x >= 0 && x < COLS&& y >= 0 && y < ROWS;
}

bool map_walkable(int x, int y)
{
    return map_valid(x, y) && tiles[map[y][x].type].walkable;
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

        float ox = (float)actors[0].x + 0.5f;
        float oy = (float)actors[0].y + 0.5f;
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

int heuristics(int ax, int ay, int bx, int by)
{
    return abs(bx - ax) + abs(ay - by);
}

struct path_node {
    bool visited;
    int distance;
    int costs;
    int prevx, prevy;
    struct path_node* lnext;
    int x;
    int y;
};

struct actor* get_alive_actor_at(int x, int y)
{
    for (int n = 0; n < num_actors; n++) {
        struct actor* a = &actors[n];
        if (a->alive && a->x == x && a->y == y)
            return a;
    }
    return NULL;
}

void path_udpate_node(struct path_node* v, struct path_node* u, int x, int y)
{
    if (!v->visited && v->costs > 0) {
        int c = u->distance + v->costs;
        if (c < v->distance) {
            v->distance = c;
            v->prevx = x;
            v->prevy = y;
        }
    }
}

bool find_path(int from_x, int from_y, int to_x, int to_y, int* first_x, int* first_y)
{

    struct path_node* v, * u, nodes[ROWS][COLS];
    struct path_node* list = 0, * prev, * p, ** q;

    for (int n = 0; n < ROWS * COLS; n++) {
        int y = n / COLS;
        int x = n % COLS;
        nodes[y][x].visited = 0;
        nodes[y][x].distance = INT_MAX;
        nodes[y][x].costs = map_walkable(x, y) ? 1 : 0;
        nodes[y][x].prevx = -1;
        nodes[y][x].prevy = -1;
        nodes[y][x].lnext = 0;
        nodes[y][x].x = x;
        nodes[y][x].y = y;
    }

    for (int n = 0; n < num_actors; n++) {
        struct actor* a = &actors[n];
        if (a->alive)
            nodes[a->y][a->x].costs += 10;
    }

    int x = from_x;
    int y = from_y;
    nodes[y][x].distance = 0;
    nodes[to_y][to_x].costs = 1;

    u = &nodes[y][x];
    list = &nodes[y][x];

    for (;;) {

        if (!list) {
            SDL_Log("no path found");
            return false;
        }

        u = list;
        list = list->lnext;

        x = u->x;
        y = u->y;

        if (x == to_x && y == to_y)
            break;

        for (int n = 0; n < 4; n++) {
            v = 0;
            switch (n) {
                case 0: if (x > 0) v = &nodes[y][x - 1]; break;
                case 1: if (x < COLS - 1); v = &nodes[y][x + 1]; break;
                case 2: if (y > 0) v = &nodes[y - 1][x]; break;
                case 3: if (y < ROWS - 1) v = &nodes[y + 1][x]; break;
            }

            if (!v->visited && v->costs > 0) {
                int c = u->distance + v->costs;
                if (c < v->distance) {
                    v->distance = c;
                    v->prevx = x;
                    v->prevy = y;

                    q = &list;
                    while (*q && (*q)->distance <= v->distance) {
                        q = &(*q)->lnext;
                    }
                    v->lnext = *q;
                    *q = v;
                }
            }
        }

        nodes[y][x].visited = 1;
    }

    // dump
    // SDL_Log("\n\nDIJKSTRA\n");
    // char s[COLS + 1];
    // for (int n = 0; n < ROWS * COLS; n++) {
    //     int y = n / COLS;
    //     int x = n % COLS;
    //     if (nodes[y][x].costs == 0) {
    //         s[x] = '#';
    //     } else if (nodes[y][x].distance == INT_MAX) {
    //         s[x] = '?';
    //     } else if (nodes[y][x].distance >= 10) {
    //         s[x] = 'V';
    //     } else {
    //         s[x] = '0' + nodes[y][x].distance;
    //     }
    //     if (x == COLS - 1) {
    //         s[COLS] = '\0';
    //         SDL_Log(s);
    //     }
    // }

    // get first entry
    while (u->prevx != from_x || u->prevy != from_y)
        u = &nodes[u->prevy][u->prevx];

    *first_x = u->x;
    *first_y = u->y;

    return true;
}

void actor_set_hp(struct actor* a, int hp)
{
    a->hp = maxi(mini(hp, actor_catalog[a->type].max_hp), 0);
    if (a->hp == 0) {
        a->alive = false;
        char death_message[128];
        if (a->type == ACTOR_TYPE_PLAYER) {
            snprintf(death_message, sizeof(death_message), "You died!");
            g.state = GAME_STATE_DEAD;
        } else {
            snprintf(death_message, sizeof(death_message), "%s is dead!", actor_catalog[a->type].name);
        }
        SDL_Log(death_message);
    }
}

void execute_melee(struct actor* source, struct actor* target)
{
    struct actor_info* source_info = &actor_catalog[source->type], * target_info = &actor_catalog[target->type];
    int damage = source_info->power - target_info->defense;
    char name[32], attack_desc[128];
    const char* p = source_info->name;
    int n;
    for (n = 0; n < sizeof(name) - 1; n++)
        name[n] = toupper(*p++);
    name[n] = '\0';

    snprintf(attack_desc, sizeof(attack_desc), "%s attacks %s", name, target_info->name);
    if (damage > 0) {
        SDL_Log("%s for %d hit points.", attack_desc, damage);
        actor_set_hp(target, target->hp - damage);
    } else {
        SDL_Log("%s but does no damage.", attack_desc);
    }
}

void move_actor(struct actor* a, int nx, int ny)
{
    if (map_valid(nx, ny) && map_walkable(nx, ny)) {
        struct actor* target = get_alive_actor_at(nx, ny);
        if (!target) {
            a->x = nx;
            a->y = ny;
        }
    }
}

void bump_player(enum direction dir)
{
    struct actor* player = &actors[0];
    int nx = player->x + (dir == DIR_RIGHT ? 1 : (dir == DIR_LEFT ? -1 : 0));
    int ny = player->y + (dir == DIR_DOWN ? 1 : (dir == DIR_UP ? -1 : 0));
    if (map_valid(nx, ny) && map_walkable(nx, ny)) {
        struct actor* target = get_alive_actor_at(nx, ny);
        if (target) {
            execute_melee(player, target);
            return;
        }
        player->x = nx;
        player->y = ny;
    }
}

void handle_game_running_state()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        struct action action = { .type = ACTION_TYPE_NONE };
        switch (event.type) {
            case SDL_QUIT:
                g.quit_requested = true;
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
                                action = (struct action){ .type = ACTION_TYPE_BUMP, .param1 = DIR_UP };
                                break;
                            case SDL_SCANCODE_DOWN:
                                action = (struct action){ .type = ACTION_TYPE_BUMP, .param1 = DIR_DOWN };
                                break;
                            case SDL_SCANCODE_LEFT:
                                action = (struct action){ .type = ACTION_TYPE_BUMP, .param1 = DIR_LEFT };
                                break;
                            case SDL_SCANCODE_RIGHT:
                                action = (struct action){ .type = ACTION_TYPE_BUMP, .param1 = DIR_RIGHT };
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
                    bump_player(action.param1);
                    break;
                case ACTION_TYPE_WAIT:
                    // do nothing
                    break;
                case ACTION_TYPE_ESCAPE:
                    g.quit_requested = true;
                    break;
            }

            // handle enemies
            struct actor* player = &actors[0];
            for (int n = 1; n < num_actors; n++) {

                struct actor* a = &actors[n];

                if (a->alive && map[a->y][a->x].visible) {
                    int distance = abs(player->x - a->x) + abs(player->y - a->y);
                    if (distance == 1) {
                        execute_melee(a, player);
                    } else {
                        int dx, dy;
                        if (find_path(a->x, a->y, player->x, player->y, &dx, &dy)) {
                            move_actor(a, dx, dy);
                        }
                    }
                }

            }
            update_fov();
        }
    }
}

void handle_game_over_state()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        struct action action = { .type = ACTION_TYPE_NONE };
        switch (event.type) {
            case SDL_QUIT:
                g.quit_requested = true;
                break;
            case SDL_KEYDOWN:
                switch (event.key.keysym.scancode) {
                    case SDL_SCANCODE_ESCAPE:
                        g.quit_requested = true;
                        break;
                    default:
                        break;
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
    g.font = load_image("ex-font.png", &fw, &fh);
    SDL_assert(fw == TILE_WIDTH * 16 && fh == TILE_HEIGHT * 16);

    random_seed = 1;
    create_map();
    update_fov();

    g.state = GAME_STATE_RUN;
    g.quit_requested = false;
    while (!g.quit_requested)
    {
        switch (g.state) {
            case GAME_STATE_RUN:
                handle_game_running_state();
                break;
            case GAME_STATE_DEAD:
                handle_game_over_state();
                break;
        }

        render();
        SDL_RenderPresent(g.renderer);
    }

    SDL_DestroyWindow(g.window);

    SDL_Quit();
    return 0;
}