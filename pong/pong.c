#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdlib.h>
#include <stdio.h>
#include "imu.h"

// Accelerometer axis (0=X, 1=Y, 2=Z) and direction used for tilt control.
// Adjust if the paddle moves the wrong way on your device.
#define TILT_AXIS 1
#define TILT_SIGN 1.0f
#define TILT_DEADZONE 0.05f
#define TILT_SPEED 12.0f

#define SCREEN_W 128
#define SCREEN_H 64
#define PADDLE_W 2
#define PADDLE_H 14
#define BALL_SIZE 2
#define LEFT_X 2
#define RIGHT_X (SCREEN_W - 2 - PADDLE_W)
#define WIN_SCORE 7

typedef enum {
	EventTick,
	EventInput,
} EventType;

typedef struct {
	EventType type;
	InputEvent input;
} AppEvent;

typedef struct {
	FuriMutex* mutex;
	float bx, by, vx, vy;
	float p1, p2;
	int s1, s2;
	bool up, down, paused, over;
	bool imu_ok;
	float tilt_base;
} Pong;

static float read_tilt(void) {
	float a[3];
	if(!imu_read_accel(a)) return 0.0f;
	return a[TILT_AXIS] * TILT_SIGN;
}

static void reset_ball(Pong* g, int dir) {
	g->bx = SCREEN_W / 2;
	g->by = SCREEN_H / 2;
	g->vx = 1.5f * dir;
	g->vy = ((rand() % 2) ? 1.0f : -1.0f) * (0.4f + (rand() % 8) / 10.0f);
}

static void reset_game(Pong* g) {
	g->p1 = g->p2 = (SCREEN_H - PADDLE_H) / 2;
	g->s1 = g->s2 = 0;
	g->paused = false;
	g->over = false;
	reset_ball(g, (rand() % 2) ? 1 : -1);
}

static float clampf(float v, float lo, float hi) {
	return v < lo ? lo : (v > hi ? hi : v);
}

static void tick(Pong* g) {
	if(g->paused || g->over) return;

	if(g->up) g->p1 -= 2.0f;
	if(g->down) g->p1 += 2.0f;
	if(g->imu_ok) {
		float d = read_tilt() - g->tilt_base;
		if(d > TILT_DEADZONE || d < -TILT_DEADZONE) g->p1 += d * TILT_SPEED;
	}
	g->p1 = clampf(g->p1, 0, SCREEN_H - PADDLE_H);

	float center = g->p2 + PADDLE_H / 2.0f;
	if(g->vx > 0) {
		if(g->by > center + 2) g->p2 += 1.2f;
		else if(g->by < center - 2) g->p2 -= 1.2f;
	}
	g->p2 = clampf(g->p2, 0, SCREEN_H - PADDLE_H);

	g->bx += g->vx;
	g->by += g->vy;

	if(g->by <= 0) {
		g->by = 0;
		g->vy = -g->vy;
	} else if(g->by >= SCREEN_H - BALL_SIZE) {
		g->by = SCREEN_H - BALL_SIZE;
		g->vy = -g->vy;
	}

	if(g->vx < 0 && g->bx <= LEFT_X + PADDLE_W && g->bx >= LEFT_X - 2 &&
	   g->by + BALL_SIZE >= g->p1 && g->by <= g->p1 + PADDLE_H) {
		g->bx = LEFT_X + PADDLE_W;
		g->vx = clampf(-g->vx * 1.05f, -4.0f, 4.0f);
		g->vy += ((g->by + 1) - (g->p1 + PADDLE_H / 2.0f)) * 0.12f;
	}
	if(g->vx > 0 && g->bx + BALL_SIZE >= RIGHT_X && g->bx + BALL_SIZE <= RIGHT_X + 2 &&
	   g->by + BALL_SIZE >= g->p2 && g->by <= g->p2 + PADDLE_H) {
		g->bx = RIGHT_X - BALL_SIZE;
		g->vx = clampf(-g->vx * 1.05f, -4.0f, 4.0f);
		g->vy += ((g->by + 1) - (g->p2 + PADDLE_H / 2.0f)) * 0.12f;
	}
	g->vy = clampf(g->vy, -2.5f, 2.5f);

	if(g->bx < -BALL_SIZE) {
		g->s2++;
		reset_ball(g, 1);
	} else if(g->bx > SCREEN_W) {
		g->s1++;
		reset_ball(g, -1);
	}
	if(g->s1 >= WIN_SCORE || g->s2 >= WIN_SCORE) g->over = true;
}

static void draw_cb(Canvas* canvas, void* ctx) {
	Pong* g = ctx;
	furi_mutex_acquire(g->mutex, FuriWaitForever);

	canvas_clear(canvas);
	for(int y = 0; y < SCREEN_H; y += 4) canvas_draw_dot(canvas, SCREEN_W / 2, y);

	canvas_draw_box(canvas, LEFT_X, (int)g->p1, PADDLE_W, PADDLE_H);
	canvas_draw_box(canvas, RIGHT_X, (int)g->p2, PADDLE_W, PADDLE_H);
	canvas_draw_box(canvas, (int)g->bx, (int)g->by, BALL_SIZE, BALL_SIZE);

	char buf[8];
	canvas_set_font(canvas, FontSecondary);
	snprintf(buf, sizeof(buf), "%d", g->s1);
	canvas_draw_str_aligned(canvas, SCREEN_W / 2 - 8, 2, AlignRight, AlignTop, buf);
	snprintf(buf, sizeof(buf), "%d", g->s2);
	canvas_draw_str_aligned(canvas, SCREEN_W / 2 + 8, 2, AlignLeft, AlignTop, buf);

	if(g->over) {
		canvas_set_color(canvas, ColorWhite);
		canvas_draw_box(canvas, 24, 20, 80, 24);
		canvas_set_color(canvas, ColorBlack);
		canvas_draw_frame(canvas, 24, 20, 80, 24);
		canvas_set_font(canvas, FontPrimary);
		canvas_draw_str_aligned(
			canvas, 64, 28, AlignCenter, AlignCenter, g->s1 > g->s2 ? "YOU WIN!" : "YOU LOSE");
		canvas_set_font(canvas, FontSecondary);
		canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, "OK to restart");
	} else if(g->paused) {
		canvas_set_font(canvas, FontPrimary);
		canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "PAUSED");
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

int32_t pong_app(void* p) {
	UNUSED(p);
	srand(furi_get_tick());

	FuriMessageQueue* q = furi_message_queue_alloc(8, sizeof(AppEvent));
	Pong* g = malloc(sizeof(Pong));
	memset(g, 0, sizeof(Pong));
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
			if(in->key == InputKeyUp) {
				if(in->type == InputTypePress) g->up = true;
				else if(in->type == InputTypeRelease) g->up = false;
			} else if(in->key == InputKeyDown) {
				if(in->type == InputTypePress) g->down = true;
				else if(in->type == InputTypeRelease) g->down = false;
			} else if(in->key == InputKeyOk && in->type == InputTypeLong) {
				if(g->imu_ok) g->tilt_base = read_tilt();
			} else if(in->key == InputKeyOk && in->type == InputTypeShort) {
				if(g->over) reset_game(g);
				else g->paused = !g->paused;
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
