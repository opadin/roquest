#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>
#include <SDL.h>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// forward decl
void handle_game_over_state(const SDL_Event* ev);

// TODO tile-size should be variable
const int TILE_WIDTH = 10;
const int TILE_HEIGHT = 10;

const int ZOOMX = 1;
const int ZOOMY = 1;

#define COLS  80
#define ROWS  45

#define SCREEN_COLS ((COLS)+0)
#define SCREEN_ROWS ((ROWS)+5)

#define WINDOW_WIDTH    ((TILE_WIDTH) * (ZOOMX) * (SCREEN_COLS))
#define WINDOW_HEIGHT   ((TILE_HEIGHT) * (ZOOMY) * (SCREEN_ROWS))

#define MAX_ROOMS_PER_MAP   30
#define VIEW_RADIUS         10

#define MAX_ACTORS 128
#define MAX_CORPSES MAX_ACTORS

#define MAX_MESSAGE_LEN 256
#define MAX_MESSAGES_IN_LOG 128

struct action {
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

#define COL2SDL(c) ((SDL_Color){.r = (c).red, .g = (c).green, .b = (c).blue, .a = 255})

struct point {
	int32_t x, y;
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
	/* shroud */ {0, 0, {' ', {255, 255, 255}, { 0,  0,   0}}, {' ', {255, 255, 255}, {  0,   0,   0}}},
	/* floor  */ {1, 1, {' ', {255, 255, 255}, {50, 50, 150}}, {' ', {255, 255, 255}, {200, 180,  50}}},
	/* wall   */ {0, 0, {' ', {255, 255, 255}, { 0,  0, 100}}, {' ', {255, 255, 255}, {130, 110,  50}}}
};

enum game_state {
	GAME_STATE_NONE,
	GAME_STATE_RUN,
	GAME_STATE_DEAD,
	GAME_STATE_HISTORY_VIEWER
};

struct global {
	SDL_Window* window;
	SDL_Renderer* renderer;
	SDL_Texture* font;
	enum game_state state;
	bool quit_requested;
	bool focus;
	int mouse_x;
	int mouse_y;
	Uint64 start_ticks;
	Uint64 last_ticks;
};

static int32_t SDL_USEREVENT_NOTHING, SDL_USEREVENT_RENDER;

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

struct message {
	char text[MAX_MESSAGE_LEN];
	struct color fg;
	int count;
};

static struct message messages[MAX_MESSAGES_IN_LOG];
static int last_message;
static int num_messages;

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

struct color white = { 0xFF, 0xFF, 0xFF };
struct color black = { 0x0, 0x0, 0x0 };
struct color bar_text = { 255, 255, 255 };
struct color bar_filled = { 0x0, 0x60, 0x0 };
struct color bar_empty = { 0x40, 0x10, 0x10 };
struct color welcome_text = { 0x20, 0xA0, 0xFF };
struct color player_atk = { 0xE0, 0xE0, 0xE0 };
struct color enemy_atk = { 0xFF, 0xC0, 0xC0 };
struct color player_die = { 0xFF, 0x30, 0x30 };
struct color enemy_die = { 0xFF, 0xA0, 0x30 };

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

// in bounds
bool map_valid(int x, int y)
{
	return x >= 0 && x < COLS&& y >= 0 && y < ROWS;
}

bool map_walkable(int x, int y)
{
	return map_valid(x, y) && tiles[map[y][x].type].walkable;
}

bool map_visible(int x, int y)
{
	return map_valid(x, y) && map[y][x].visible;
}

void get_actor_name(struct actor* a, char* res, int max)
{
	if (a->alive) {
		snprintf(res, max, "%s", actor_catalog[a->type].name);
	}
	else {
		snprintf(res, max, "remains of %s", actor_catalog[a->type].name);
	}
}

void add_message(struct color color, bool check_stack, const char* fmt, ...)
{
	char buffer[MAX_MESSAGE_LEN];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	if (check_stack && num_messages > 0 && !strcmp(messages[last_message].text, buffer)) {
		messages[last_message].count++;
	}
	else {
		if (num_messages < SDL_arraysize(messages))
			num_messages++;
		last_message = (last_message + 1) % SDL_arraysize(messages);
		strcpy(messages[last_message].text, buffer);
		messages[last_message].count = 1;
		messages[last_message].fg = color;
	}
}

static uint8_t g_alpha = 255;

void set_render_alpha(uint8_t alpha)
{
	SDL_SetTextureAlphaMod(g.font, g_alpha = alpha);
}

#define MAX_SPRITES 0x4000
static struct SDL_Vertex g_verts[MAX_SPRITES * 4];
static uint16_t g_inds[MAX_SPRITES * 6];
static uint32_t g_num_sprites;

void render_tile(int x, int y, int ch, struct color color)
{
	ch &= 0xff;

	float inv = 1.0f / 16.0f;

	float sx0 = (float)(ch % 16) * inv;
	float sx1 = sx0 + inv;
	float sy0 = (float)(ch / 16) * inv;
	float sy1 = sy0 + inv;

	float mulx = (float)(TILE_WIDTH * ZOOMX);
	float muly = (float)(TILE_HEIGHT * ZOOMY);

	float dx0 = x * mulx;
	float dx1 = dx0 + mulx;
	float dy0 = y * muly;
	float dy1 = dy0 + muly;

	SDL_Color col = { .r = color.red, .g = color.green, .b = color.blue, .a = g_alpha };

	if (g_num_sprites == MAX_SPRITES) fatal("too many quads");

	uint16_t idx = g_num_sprites * 4;

	uint16_t* pi = &g_inds[g_num_sprites * 6];
	*pi++ = idx; *pi++ = idx + 1; *pi++ = idx + 2;
	*pi++ = idx; *pi++ = idx + 2; *pi = idx + 3;

	SDL_Vertex* pv = &g_verts[idx];

	pv[0] = (SDL_Vertex){ .position.x = dx0, .position.y = dy0, .color = col, .tex_coord.x = sx0, .tex_coord.y = sy0 };
	pv[1] = (SDL_Vertex){ .position.x = dx1, .position.y = dy0, .color = col, .tex_coord.x = sx1, .tex_coord.y = sy0 };
	pv[2] = (SDL_Vertex){ .position.x = dx1, .position.y = dy1, .color = col, .tex_coord.x = sx1, .tex_coord.y = sy1 };
	pv[3] = (SDL_Vertex){ .position.x = dx0, .position.y = dy1, .color = col, .tex_coord.x = sx0, .tex_coord.y = sy1 };
	g_num_sprites += 1;
}

void render_tile2(int x, int y, int ch, SDL_Color color)
{
	ch &= 0xff;

	float inv = 1.0f / 16.0f;

	float sx0 = (float)(ch % 16) * inv;
	float sx1 = sx0 + inv;
	float sy0 = (float)(ch / 16) * inv;
	float sy1 = sy0 + inv;

	float mulx = (float)(TILE_WIDTH * ZOOMX);
	float muly = (float)(TILE_HEIGHT * ZOOMY);

	float dx0 = x * mulx;
	float dx1 = dx0 + mulx;
	float dy0 = y * muly;
	float dy1 = dy0 + muly;

	if (g_num_sprites == MAX_SPRITES) fatal("too many quads");

	uint16_t idx = g_num_sprites * 4;

	uint16_t* pi = &g_inds[g_num_sprites * 6];
	*pi++ = idx; *pi++ = idx + 1; *pi++ = idx + 2;
	*pi++ = idx; *pi++ = idx + 2; *pi = idx + 3;

	SDL_Vertex* pv = &g_verts[idx];

	pv[0] = (SDL_Vertex){ .position.x = dx0, .position.y = dy0, .color = color, .tex_coord.x = sx0, .tex_coord.y = sy0 };
	pv[1] = (SDL_Vertex){ .position.x = dx1, .position.y = dy0, .color = color, .tex_coord.x = sx1, .tex_coord.y = sy0 };
	pv[2] = (SDL_Vertex){ .position.x = dx1, .position.y = dy1, .color = color, .tex_coord.x = sx1, .tex_coord.y = sy1 };
	pv[3] = (SDL_Vertex){ .position.x = dx0, .position.y = dy1, .color = color, .tex_coord.x = sx0, .tex_coord.y = sy1 };
	g_num_sprites += 1;
}

void render_tile_with_bg(int x, int y, int ch, struct color fg, struct color bg)
{
	render_tile(x, y, 0xdb, bg);
	render_tile(x, y, ch, fg);
}

void draw_background(int x, int y, int w, int h, char ch, struct color color)
{
	for (int j = 0; j < h; j++) {
		for (int i = 0; i < w; i++) {
			render_tile(x + i, y + j, ch, color);
		}
	}
}

struct dbuf_char {
	uint8_t ch;
	SDL_Color fg, bg;
};

typedef struct dbuf_char draw_buffer[SCREEN_COLS];

void write(int x, int y, int len, struct dbuf_char* dbuf)
{
	for (; len-- > 0; x++, dbuf++) {
		if (dbuf->bg.a > 0)
			render_tile2(x, y, 0xdb, dbuf->bg);
		if (dbuf->fg.a > 0)
			render_tile2(x, y, dbuf->ch, dbuf->fg);
	}
}

void write_char(int x, int y, char ch, SDL_Color fg, SDL_Color bg)
{
	if (bg.a > 0)
		render_tile2(x, y, 0xdb, bg);
	if (fg.a > 0)
		render_tile2(x, y, ch, fg);
}

void write_line(int x, int y, int w, int h, struct dbuf_char* dbuf)
{
	for (; h-- > 0; y++)
		write(x, y, w, dbuf);
}

void dbuf_move_char(draw_buffer dbuf, int pos, char ch, SDL_Color fg, SDL_Color bg, int count)
{
	if (count <= 0)
		return;

	for (int n = 0; n < count; n++) {
		dbuf[pos + n].ch = ch;
		dbuf[pos + n].fg = fg;
		dbuf[pos + n].bg = bg;
	}
}

void dbuf_move_buf(struct dbuf_char* dbuf, const char* src, int count)
{
	while (count--) {
		dbuf++->ch = *src++;
	}
}

void dbuf_move_buf_col(struct dbuf_char* dbuf, char* src, SDL_Color fg, SDL_Color bg, int count)
{
	while (count-- > 0) {
		*dbuf++ = (struct dbuf_char){ .fg = fg, .bg = bg, .ch = *src++ };
	}
}


void dbuf_put_char(draw_buffer dbuf, int pos, char ch)
{
	dbuf[pos].ch = ch;
}

void draw_frame_line(int x, int y, int len, const char* frame_chars, struct color color)
{
	render_tile(x, y, frame_chars[0], color);
	for (int n = 1; n < len - 1; n++)
		render_tile(x + n, y, frame_chars[1], color);
	render_tile(x + len - 1, y, frame_chars[2], color);
}

void frame_line(draw_buffer dbuf, int len, const char* frame_chars, SDL_Color fg, SDL_Color bg)
{
	dbuf_move_char(dbuf, 0, frame_chars[0], fg, bg, 1);
	dbuf_move_char(dbuf, 1, frame_chars[1], fg, bg, len - 2);
	dbuf_move_char(dbuf, len - 1, frame_chars[2], fg, bg, 1);
}

void draw_frame(int x, int y, int w, int h, SDL_Color fg, SDL_Color bg, const char* title)
{
	if (w <= 0 || h <= 0)
		return;

	draw_buffer dbuf;

	/*l = min(strlen(title), w - 10);
	l = max(l, 0);*/
	frame_line(dbuf, w, "\xda\xc4\xbf", fg, bg);
	if (title) {
		int len = maxi(mini((int)strlen(title), w - 10), 0);
		int pos = (w - len) >> 1;
		dbuf[pos - 1].ch = ' ';
		dbuf_move_buf(dbuf + pos, title, len);
		dbuf[pos + len].ch = ' ';

	}
	write_line(x, y, w, 1, dbuf);


	frame_line(dbuf, w, "\xb3 \xb3", fg, bg);
	write_line(x, y + 1, w, h - 2, dbuf);

	frame_line(dbuf, w, "\xc0\xc4\xd9", fg, bg);
	write_line(x, y + h - 1, w, 1, dbuf);
}

void draw_gauge(int x, int y, int len, float rate, struct color fill, struct color empty)
{
	int fw = (int)(len * rate);
	for (int n = 0; n < len; n++)
		render_tile(x + n, y, 0xdb, n < fw ? fill : empty);
}

void draw_text(int x, int y, struct color color, const char* fmt, ...)
{
	char buffer[256];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	const char* p = buffer;
	while (*p)
		render_tile(x++, y, *p++, color);
	va_end(args);
}

bool is_blank(char ch)
{
	return ch == ' ' || ch == '\n' || ch == '\r';
}

const char* wrap_text(char* res, int max_len, const char* text)
{
	SDL_assert(*text && max_len > 0);

	const char* p = text;
	while (*p) {
		if (*p++ == '\n')
			break;
	}

	intptr_t c = p - text;
	SDL_assert(c > 0);
	if (c >= max_len) {
		c = max_len;
		while (c > 0 && !is_blank(text[c]))
			c--;
		if (c == 0)
			c = max_len;
		else
			c++;
	}

	if (res) {
		memcpy(res, text, c);
		if (res[c - 1] == '\n') {
			c--;
		}
		res[c] = '\0';
	}

	return text + c;
}

int calc_lines(const char* text, int max_width)
{
	int lines = 0;
	while (*text) {
		text = wrap_text(NULL, max_width, text);
		lines++;
	}
	return lines;
}

void render_message_log(int x, int y, int width, int height, int start)
{
	if (num_messages == 0)
		return;

	start = mini(num_messages - 1, maxi(start, 0));

	int yofs = y + height;
	int cur = last_message - start;
	if (cur < 0) cur += SDL_arraysize(messages);
	int num = num_messages - start;
	char line[COLS];
	int vspace = height;
	char text[MAX_MESSAGE_LEN];
	while (vspace > 0 && num-- > 0) {

		const char* p = messages[cur].text;
		if (messages[cur].count > 1) {
			snprintf(text, sizeof(text), "%s  (x%d)", p, messages[cur].count);
			p = text;
		}

		int lines = calc_lines(p, width);
		while (lines > vspace) {
			p = wrap_text(NULL, width, p);
			lines--;
		}

		yofs -= lines;
		vspace -= lines;
		int n = 0;
		while (*p) {
			p = wrap_text(line, width, p);
			draw_text(x, yofs + n++, messages[cur].fg, line);
		}

		cur = cur - 1;
		if (cur < 0) cur += num_messages;
	}
}

void render_hp_bar()
{
	int hp = actors[0].hp;
	int max_hp = actor_catalog[ACTOR_TYPE_PLAYER].max_hp;
	float rate = (float)hp / max_hp;
	draw_gauge(0, 45, 20, rate, bar_filled, bar_empty);
	draw_text(1, 45, bar_text, "HP: %d/%d", hp, max_hp);
}

void render_map_set()
{
	// center map in window
	// int sx = (WINDOW_WIDTH / (TILE_WIDTH * ZOOMX) - COLS) / 2;
	// int sy = (WINDOW_HEIGHT / (TILE_HEIGHT * ZOOMY) - ROWS) / 2;
	int sx = 0;
	int sy = 0;

	// map
	for (int y = 0; y < ROWS; y++) {
		for (int x = 0; x < COLS; x++) {
			struct tile_graphic* tg;
			if (!map[y][x].explored) {
				tg = &tiles[0].light;
			}
			else {
				struct tile_info* ti = &tiles[map[y][x].type];
				tg = map[y][x].visible ? &ti->light : &ti->dark;
			}
			write_char(sx + x, sy + y, tg->ch, (SDL_Color){tg->fg.red, tg->fg.green, tg->fg.blue, 255}, (SDL_Color){tg->bg.red, tg->bg.green, tg->bg.blue, 255});
			// render_tile_with_bg(sx + x, sy + y, tg->ch, tg->fg, tg->bg);
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
						render_tile2(sx + a->x, sy + a->y, '%', (SDL_Color) { 191, 0, 0, 255 });
					else
						render_tile2(sx + a->x, sy + a->y, e->character, COL2SDL(e->color));
				}
			}
		}
	}

	render_message_log(21, 45, 40, 5, 0);

	render_hp_bar();

	if (map_visible(g.mouse_x, g.mouse_y)) {
		char buffer[256], name[48];
		buffer[0] = '\0';
		for (int i = 0; i < num_actors; i++) {
			if (actors[i].x == g.mouse_x && actors[i].y == g.mouse_y) {
				if (buffer[0])
					strcat(buffer, ", ");
				get_actor_name(&actors[i], name, sizeof(name));
				strcat(buffer, name);
			}
		}
		char* p = buffer;
		while (*p) {
			*p++ = toupper(*p);
		}
		draw_text(21, 44, white, buffer);
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
			}
			else {
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
	num_messages = 0;

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
			}
			else {
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
				}
				else {
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

void update_fov()
{
	for (int y = 0; y < ROWS; y++) {
		for (int x = 0; x < COLS; x++) {
			map[y][x].visible = 0;
		}
	}

	for (int i = 0; i < 360 * 8; i++) {
		float x = cosf((float)i * 0.01745f);
		float y = sinf((float)i * 0.01745f);

		float ox = (float)actors[0].x + 0.5f;
		float oy = (float)actors[0].y + 0.5f;
		for (int j = 0; j < VIEW_RADIUS; j++) {
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
	struct path_node* list = 0, ** q;

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
		struct color color;
		if (a->type == ACTOR_TYPE_PLAYER) {
			snprintf(death_message, sizeof(death_message), "You died!");
			color = player_die;
			g.state = GAME_STATE_DEAD;
		}
		else {
			snprintf(death_message, sizeof(death_message), "%s is dead!", actor_catalog[a->type].name);
			color = enemy_die;
		}
		add_message(color, 1, death_message);
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

	struct color attack_color = source->type == ACTOR_TYPE_PLAYER ? player_atk : enemy_atk;

	snprintf(attack_desc, sizeof(attack_desc), "%s attacks %s", name, target_info->name);
	if (damage > 0) {
		add_message(attack_color, 1, "%s for %d hit points.", attack_desc, damage);
		actor_set_hp(target, target->hp - damage);
	}
	else {
		add_message(attack_color, 1, "%s but does no damage.", attack_desc);
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

void start_game()
{
	create_map();
	update_fov();
	add_message(welcome_text, 0, "Hello and welcome, adventurer, to yet another dungeon!");
}

void execute_action(bool (*action)(void* udata), void* udata)
{
	if (!action(udata))
		return;

	// handle enemies
	struct actor* player = &actors[0];
	for (int n = 1; n < num_actors; n++) {

		struct actor* a = &actors[n];

		if (a->alive && map[a->y][a->x].visible) {
			int distance = abs(player->x - a->x) + abs(player->y - a->y);
			if (distance == 1) {
				execute_melee(a, player);
			}
			else {
				int dx, dy;
				if (find_path(a->x, a->y, player->x, player->y, &dx, &dy)) {
					move_actor(a, dx, dy);
				}
			}
		}
	}

	update_fov();
}

bool action_bump(void* p)
{
    struct point* dir = p;
    struct actor* player = &actors[0];
	int32_t nx = dir->x, ny = dir->y;

	if (map_valid(nx, ny) && map_walkable(nx, ny)) {
		struct actor* target = get_alive_actor_at(nx, ny);
		if (target) {
			execute_melee(player, target);
			return true;
		}
		player->x = nx;
		player->y = ny;
	}

	return true;
}

bool action_wait(void* p)
{
	// do nothing
	return true;
}

void process_movement(const SDL_Event* ev)
{
	struct actor* player = &actors[0];
	int32_t nx = player->x, ny = player->y;

	if (ev->type == SDL_KEYDOWN) {
		switch (ev->key.keysym.scancode) {
			case SDL_SCANCODE_UP:		ny -= 1; break;
			case SDL_SCANCODE_DOWN:		ny += 1; break;
			case SDL_SCANCODE_LEFT:		nx -= 1; break;
			case SDL_SCANCODE_RIGHT:	nx += 1; break;
		}
	}

	if (nx != player->x || ny != player->y) {
		struct point dir = { .x = nx, .y = ny };
		execute_action(action_bump, &dir);
	}
}

void process_quit(const SDL_Event* ev)
{
	if (ev->type == SDL_QUIT || (ev->type == SDL_KEYDOWN && ev->key.keysym.scancode == SDL_SCANCODE_ESCAPE)) {
		g.quit_requested = true;
	}
}

void process_wait_key(const SDL_Event* ev)
{
	if (ev->type == SDL_KEYDOWN) {
		SDL_Scancode sc = ev->key.keysym.scancode;
		if (sc == SDL_SCANCODE_KP_5 || sc == SDL_SCANCODE_PERIOD) {
			execute_action(action_wait, 0);
		}
	}
}

void process_commands(const SDL_Event* ev)
{
	if (ev->type == SDL_KEYDOWN) {
		switch (ev->key.keysym.sym) {
			case 'c':
				start_game();
				break;
			case 'v':
				g.state = GAME_STATE_HISTORY_VIEWER;
				break;
		}
	}
}

void process_mouse(const SDL_Event* ev)
{
	switch (ev->type) {
		case SDL_WINDOWEVENT:
			switch (ev->window.event) {
				case SDL_WINDOWEVENT_ENTER:
					g.focus = true;
					SDL_GetMouseState(&g.mouse_x, &g.mouse_y);
					break;
				case SDL_WINDOWEVENT_LEAVE:
					g.focus = false;
					g.mouse_x = g.mouse_y = -1;
					break;
			}
			break;
		case SDL_MOUSEMOTION:
			if (g.focus) {
				if (ev->motion.x < 0 || ev->motion.y < 0 || ev->motion.x >= WINDOW_WIDTH || ev->motion.y >= WINDOW_HEIGHT) {
					g.mouse_x = g.mouse_y = -1;
				}
				else {
					g.mouse_x = ev->motion.x / TILE_WIDTH;
					g.mouse_y = ev->motion.y / TILE_HEIGHT;
				}
			}
			break;
	}
}

void handle_game_running_state(const SDL_Event* ev)
{
	process_quit(ev);
	process_wait_key(ev);
	process_movement(ev);
	process_commands(ev);
	process_mouse(ev);
	if (ev->type == SDL_USEREVENT_RENDER) {
		render_map_set();
	}
}

void handle_game_over_state(const SDL_Event* ev)
{
	process_quit(ev);
	if (ev->type == SDL_USEREVENT_RENDER) {
		render_map_set();
	}
}

void handle_hist_viewer_state(const SDL_Event* ev)
{
	static int cursor = 0;
	if (ev->type == SDL_KEYDOWN) {
		switch (ev->key.keysym.scancode) {
			case SDL_SCANCODE_ESCAPE:
				g.state = GAME_STATE_RUN;
				break;
			case SDL_SCANCODE_UP:
				if (cursor < num_messages - 1) {
					cursor++;
				}
				break;
			case SDL_SCANCODE_DOWN:
				if (cursor > 0)
					cursor--;
				break;
			case SDL_SCANCODE_PAGEDOWN:
				cursor -= 10;
				if (cursor < 0)
					cursor = 0;
				break;
			case SDL_SCANCODE_PAGEUP:
				cursor += 10;
				if (cursor >= num_messages)
					cursor = num_messages - 1;
				break;
			case SDL_SCANCODE_HOME:
				cursor = num_messages - 1;
				break;
			case SDL_SCANCODE_END:
				cursor = 0;
				break;
		}
	}
	if (ev->type == SDL_USEREVENT_RENDER) {
		set_render_alpha(127);
		render_map_set();
		set_render_alpha(255);
		draw_frame(3, 3, COLS - 6, ROWS - 6, (SDL_Color) { 255, 255, 255, 255 }, (SDL_Color) { 127, 127, 127, 127 }, "Message history");
		render_message_log(5, 5, COLS - 10, ROWS - 10, cursor);
	}

}

void (*event_handlers[])(const SDL_Event*) = {
	0,
	handle_game_running_state,
	handle_game_over_state,
	handle_hist_viewer_state
};

//void (*event_handlers)(const SDL_Event* ev)[] = {
//
//}

int main(int argc, char* argv[])
{
	SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "1");

	if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_VIDEO) != 0)
		fatal("SDL_Init failed: %s\n", SDL_GetError());

	g.window = SDL_CreateWindow("roquest", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_SHOWN);
	if (!g.window) fatal("Could not create window: %s\n", SDL_GetError());

	g.renderer = SDL_CreateRenderer(g.window, -1, 0);
	if (!g.renderer) fatal("could not create sdl renderer: %s", SDL_GetError());

	SDL_USEREVENT_NOTHING = SDL_RegisterEvents(2);
	if (SDL_USEREVENT_NOTHING == -1) fatal("could not create render event");
	SDL_USEREVENT_RENDER = SDL_USEREVENT_NOTHING + 1;

	int fw, fh;
	g.font = load_image("Bm437_Rainbow100_re_40.png", &fw, &fh);
	SDL_assert(fw == TILE_WIDTH * 16 && fh == TILE_HEIGHT * 16);

	random_seed = 1;
	start_game();

	g.state = GAME_STATE_RUN;
	g.quit_requested = false;
	g.mouse_x = g.mouse_y = -1;
	g.start_ticks = g.last_ticks = SDL_GetTicks64();

	Uint64 freq = SDL_GetPerformanceFrequency();
	while (!g.quit_requested) {

		void (*eh)(const SDL_Event * ev) = event_handlers[g.state];
		SDL_assert(eh);

		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			eh(&ev);
		}

		static float krms;
		Uint64 rs = SDL_GetPerformanceCounter();
		g_num_sprites = 0;
		SDL_SetRenderDrawColor(g.renderer, 0, 0, 0, 255);
		SDL_RenderClear(g.renderer);
		ev.type = SDL_USEREVENT_RENDER;
		eh(&ev);
		draw_text(0, 0, white, "%.2f (Quads: %d)", krms, g_num_sprites);
		SDL_RenderGeometryRaw(g.renderer, g.font, &g_verts[0].position.x, sizeof(g_verts[0]),
			&g_verts[0].color, sizeof(g_verts[0]), &g_verts[0].tex_coord.x, sizeof(g_verts[0]),
			g_num_sprites * 4, g_inds, g_num_sprites * 6, sizeof(g_inds[0]));
		SDL_RenderPresent(g.renderer);
		Uint64 re = SDL_GetPerformanceCounter();

		float rdiff = ((re - rs) * 1000.0f) / freq;
		static float rms = 0;
		static float interval;
		rms += (rdiff - rms) / 10; // smooth out
		interval += rms;
		if (interval >= 330.0f) {
			krms = rms;
			interval = 0.0f;
		}

		//// save some processor power
		//Uint64 ticks = SDL_GetTicks64();
		//if (ticks - g.last_ticks <= 10) {
		//	SDL_Delay(1);
		//}
		//g.last_ticks = ticks;
	}

	SDL_DestroyWindow(g.window);

	SDL_Quit();
	return 0;
}