#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>
#include <stdio.h>
#include "imu.h"

// Accelerometer axis (0=X, 1=Y, 2=Z) and direction used for tilt control.
// Adjust if the ship moves the wrong way on your device.
#define TILT_AXIS 0
#define TILT_SIGN 1.0f
#define TILT_DEADZONE 0.05f
#define TILT_SPEED 10.0f

#define SCREEN_W 128
#define SCREEN_H 64
#define ROWS 4
#define COLS 8
#define INV_W 7
#define INV_H 5
#define INV_SX 11
#define INV_SY 8
#define PLAYER_W 9
#define PLAYER_H 4
#define PLAYER_Y (SCREEN_H - PLAYER_H - 1)
#define MAX_ENEMY_BULLETS 3

typedef enum {
	EventTick,
	EventInput,
} EventType;

typedef struct {
	EventType type;
	InputEvent input;
} AppEvent;

typedef struct {
	bool active;
	int x, y;
} Bullet;

typedef struct {
	FuriMutex* mutex;
	bool alive[ROWS][COLS];
	int alive_count;
	int ox, oy, dir;
	int move_timer;
	bool frame;
	int px;
	bool left, right;
	Bullet shot;
	Bullet ebullets[MAX_ENEMY_BULLETS];
	int score, lives, wave;
	bool over;
	bool imu_ok;
	float tilt_base;
} Game;

static float read_tilt(void) {
	float a[3];
	if(!imu_read_accel(a)) return 0.0f;
	return a[TILT_AXIS] * TILT_SIGN;
}

static void new_wave(Game* g) {
	for(int r = 0; r < ROWS; r++)
		for(int c = 0; c < COLS; c++) g->alive[r][c] = true;
	g->alive_count = ROWS * COLS;
	g->ox = 4;
	g->oy = 8 + (g->wave > 4 ? 8 : g->wave * 2);
	g->dir = 1;
	g->move_timer = 0;
	g->shot.active = false;
	for(int i = 0; i < MAX_ENEMY_BULLETS; i++) g->ebullets[i].active = false;
}

static void reset_game(Game* g) {
	g->score = 0;
	g->lives = 3;
	g->wave = 0;
	g->over = false;
	g->px = (SCREEN_W - PLAYER_W) / 2;
	new_wave(g);
}

static void move_invaders(Game* g) {
	int min_c = COLS, max_c = -1, max_r = -1;
	for(int r = 0; r < ROWS; r++)
		for(int c = 0; c < COLS; c++)
			if(g->alive[r][c]) {
				if(c < min_c) min_c = c;
				if(c > max_c) max_c = c;
				if(r > max_r) max_r = r;
			}
	if(max_c < 0) return;

	int left = g->ox + min_c * INV_SX;
	int right = g->ox + max_c * INV_SX + INV_W;
	if((g->dir > 0 && right + 2 > SCREEN_W) || (g->dir < 0 && left - 2 < 0)) {
		g->dir = -g->dir;
		g->oy += 3;
	} else {
		g->ox += g->dir * 2;
	}
	g->frame = !g->frame;

	if(g->oy + max_r * INV_SY + INV_H >= PLAYER_Y) g->over = true;
}

static void enemy_fire(Game* g) {
	if(rand() % 100 > 4 + g->wave * 2) return;
	int slot = -1;
	for(int i = 0; i < MAX_ENEMY_BULLETS; i++)
		if(!g->ebullets[i].active) {
			slot = i;
			break;
		}
	if(slot < 0) return;

	int c = rand() % COLS;
	for(int r = ROWS - 1; r >= 0; r--) {
		if(g->alive[r][c]) {
			g->ebullets[slot].active = true;
			g->ebullets[slot].x = g->ox + c * INV_SX + INV_W / 2;
			g->ebullets[slot].y = g->oy + r * INV_SY + INV_H;
			return;
		}
	}
}

static void tick(Game* g) {
	if(g->over) return;

	if(g->left) g->px -= 2;
	if(g->right) g->px += 2;
	if(g->imu_ok) {
		float d = read_tilt() - g->tilt_base;
		if(d > TILT_DEADZONE || d < -TILT_DEADZONE) g->px += (int)(d * TILT_SPEED);
	}
	if(g->px < 0) g->px = 0;
	if(g->px > SCREEN_W - PLAYER_W) g->px = SCREEN_W - PLAYER_W;

	if(g->shot.active) {
		g->shot.y -= 3;
		if(g->shot.y < 0) g->shot.active = false;
		for(int r = 0; r < ROWS && g->shot.active; r++) {
			for(int c = 0; c < COLS; c++) {
				if(!g->alive[r][c]) continue;
				int ix = g->ox + c * INV_SX;
				int iy = g->oy + r * INV_SY;
				if(g->shot.x >= ix && g->shot.x < ix + INV_W && g->shot.y >= iy &&
				   g->shot.y < iy + INV_H) {
					g->alive[r][c] = false;
					g->alive_count--;
					g->shot.active = false;
					g->score += (ROWS - r) * 10;
					break;
				}
			}
		}
	}

	for(int i = 0; i < MAX_ENEMY_BULLETS; i++) {
		Bullet* b = &g->ebullets[i];
		if(!b->active) continue;
		b->y += 2;
		if(b->y >= SCREEN_H) {
			b->active = false;
		} else if(
			b->y >= PLAYER_Y && b->y < PLAYER_Y + PLAYER_H && b->x >= g->px &&
			b->x < g->px + PLAYER_W) {
			b->active = false;
			g->lives--;
			if(g->lives <= 0) g->over = true;
		}
	}

	if(g->alive_count == 0) {
		g->wave++;
		new_wave(g);
		return;
	}

	int delay = 2 + g->alive_count / 2;
	if(++g->move_timer >= delay) {
		g->move_timer = 0;
		move_invaders(g);
	}
	enemy_fire(g);
}

static void draw_invader(Canvas* canvas, int x, int y, bool frame) {
	canvas_draw_box(canvas, x + 1, y, 5, 4);
	canvas_set_color(canvas, ColorWhite);
	canvas_draw_dot(canvas, x + 2, y + 1);
	canvas_draw_dot(canvas, x + 4, y + 1);
	canvas_set_color(canvas, ColorBlack);
	canvas_draw_dot(canvas, x, y + (frame ? 1 : 3));
	canvas_draw_dot(canvas, x + 6, y + (frame ? 1 : 3));
	canvas_draw_dot(canvas, x + (frame ? 1 : 2), y + 4);
	canvas_draw_dot(canvas, x + (frame ? 5 : 4), y + 4);
}

static void draw_cb(Canvas* canvas, void* ctx) {
	Game* g = ctx;
	furi_mutex_acquire(g->mutex, FuriWaitForever);

	canvas_clear(canvas);
	canvas_set_font(canvas, FontSecondary);
	char buf[24];
	snprintf(buf, sizeof(buf), "%d", g->score);
	canvas_draw_str(canvas, 0, 7, buf);
	snprintf(buf, sizeof(buf), "Lives:%d", g->lives);
	canvas_draw_str_aligned(canvas, SCREEN_W, 0, AlignRight, AlignTop, buf);

	for(int r = 0; r < ROWS; r++)
		for(int c = 0; c < COLS; c++)
			if(g->alive[r][c])
				draw_invader(canvas, g->ox + c * INV_SX, g->oy + r * INV_SY, g->frame);

	canvas_draw_box(canvas, g->px, PLAYER_Y + 1, PLAYER_W, PLAYER_H - 1);
	canvas_draw_box(canvas, g->px + PLAYER_W / 2 - 1, PLAYER_Y - 1, 3, 2);

	if(g->shot.active) canvas_draw_line(canvas, g->shot.x, g->shot.y, g->shot.x, g->shot.y + 2);
	for(int i = 0; i < MAX_ENEMY_BULLETS; i++)
		if(g->ebullets[i].active)
			canvas_draw_line(
				canvas, g->ebullets[i].x, g->ebullets[i].y, g->ebullets[i].x, g->ebullets[i].y + 2);

	if(g->over) {
		canvas_set_color(canvas, ColorWhite);
		canvas_draw_box(canvas, 22, 18, 84, 28);
		canvas_set_color(canvas, ColorBlack);
		canvas_draw_frame(canvas, 22, 18, 84, 28);
		canvas_set_font(canvas, FontPrimary);
		canvas_draw_str_aligned(canvas, 64, 27, AlignCenter, AlignCenter, "GAME OVER");
		canvas_set_font(canvas, FontSecondary);
		canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, "OK to restart");
	}

	furi_mutex_release(g->mutex);
}

static void input_cb(InputEvent* input, void* ctx) {
	FuriMessageQueue* q = ctx;
	AppEvent e = {.type = EventInput, .input = *input};
	furi_message_queue_put(q, &e, 0);
}

static void timer_cb(void* ctx) {
	FuriMessageQueue* q = ctx;
	AppEvent e = {.type = EventTick};
	furi_message_queue_put(q, &e, 0);
}

int32_t space_invaders_app(void* p) {
	UNUSED(p);
	srand(furi_get_tick());

	FuriMessageQueue* q = furi_message_queue_alloc(8, sizeof(AppEvent));
	Game* g = malloc(sizeof(Game));
	memset(g, 0, sizeof(Game));
	g->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
	g->imu_ok = imu_init();
	if(g->imu_ok) g->tilt_base = read_tilt();
	reset_game(g);

	ViewPort* vp = view_port_alloc();
	view_port_draw_callback_set(vp, draw_cb, g);
	view_port_input_callback_set(vp, input_cb, q);
	Gui* gui = furi_record_open(RECORD_GUI);
	gui_add_view_port(gui, vp, GuiLayerFullscreen);

	FuriTimer* timer = furi_timer_alloc(timer_cb, FuriTimerTypePeriodic, q);
	furi_timer_start(timer, furi_kernel_get_tick_frequency() / 30);

	AppEvent e;
	bool running = true;
	while(running) {
		if(furi_message_queue_get(q, &e, FuriWaitForever) != FuriStatusOk) continue;
		furi_mutex_acquire(g->mutex, FuriWaitForever);
		if(e.type == EventTick) {
			tick(g);
		} else {
			InputEvent* in = &e.input;
			if(in->key == InputKeyLeft) {
				if(in->type == InputTypePress) g->left = true;
				else if(in->type == InputTypeRelease) g->left = false;
			} else if(in->key == InputKeyRight) {
				if(in->type == InputTypePress) g->right = true;
				else if(in->type == InputTypeRelease) g->right = false;
			} else if(in->key == InputKeyOk && in->type == InputTypeLong) {
				if(g->imu_ok) g->tilt_base = read_tilt();
			} else if(in->key == InputKeyOk && in->type == InputTypePress) {
				if(g->over) {
					reset_game(g);
				} else if(!g->shot.active) {
					g->shot.active = true;
					g->shot.x = g->px + PLAYER_W / 2;
					g->shot.y = PLAYER_Y - 3;
				}
			} else if(in->key == InputKeyBack && in->type == InputTypeShort) {
				running = false;
			}
		}
		furi_mutex_release(g->mutex);
		view_port_update(vp);
	}

	furi_timer_stop(timer);
	furi_timer_free(timer);
	if(g->imu_ok) imu_deinit();
	view_port_enabled_set(vp, false);
	gui_remove_view_port(gui, vp);
	view_port_free(vp);
	furi_record_close(RECORD_GUI);
	furi_message_queue_free(q);
	furi_mutex_free(g->mutex);
	free(g);
	return 0;
}
