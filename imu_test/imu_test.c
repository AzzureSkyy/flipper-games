#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdio.h>

#include "imu.h"

typedef struct {
	FuriMutex* mutex;
	bool otg;
	bool detected;
	uint8_t who;
	float accel[3];
	uint32_t reads;
} ImuTestState;

static void draw_cb(Canvas* canvas, void* ctx) {
	ImuTestState* s = ctx;
	char line[32];
	furi_mutex_acquire(s->mutex, FuriWaitForever);

	canvas_clear(canvas);
	canvas_set_font(canvas, FontPrimary);
	canvas_draw_str(canvas, 2, 10, "IMU Test (VGM)");
	canvas_set_font(canvas, FontSecondary);

	snprintf(line, sizeof(line), "5V OTG: %s", s->otg ? "ON" : "OFF");
	canvas_draw_str(canvas, 2, 21, line);
	snprintf(line, sizeof(line), "WHO_AM_I: 0x%02X (want 0x47)", s->who);
	canvas_draw_str(canvas, 2, 31, line);
	canvas_draw_str(canvas, 2, 41, s->detected ? "Sensor: FOUND" : "Sensor: NOT FOUND");

	if(s->detected) {
		snprintf(line, sizeof(line), "X%+5d Y%+5d Z%+5d mg", (int)(s->accel[0] * 1000.0f),
			(int)(s->accel[1] * 1000.0f), (int)(s->accel[2] * 1000.0f));
		canvas_draw_str(canvas, 2, 51, line);
		snprintf(line, sizeof(line), "reads %lu", (unsigned long)s->reads);
		canvas_draw_str(canvas, 80, 41, line);
	} else {
		canvas_draw_str(canvas, 2, 51, s->who == 0x00 || s->who == 0xFF ? "No reply on GPIO SPI" : "Unexpected chip ID");
	}
	canvas_draw_str(canvas, 2, 62, "OK: retry  Back: exit");

	furi_mutex_release(s->mutex);
}

static void input_cb(InputEvent* event, void* ctx) {
	furi_message_queue_put(ctx, event, FuriWaitForever);
}

static void probe(ImuTestState* s) {
	bool ok = imu_init();
	uint8_t who = imu_who_am_i();
	furi_mutex_acquire(s->mutex, FuriWaitForever);
	s->detected = ok;
	s->who = who;
	s->otg = furi_hal_power_is_otg_enabled();
	furi_mutex_release(s->mutex);
}

int32_t imu_test_app(void* p) {
	UNUSED(p);
	ImuTestState* s = malloc(sizeof(ImuTestState));
	memset(s, 0, sizeof(ImuTestState));
	s->mutex = furi_mutex_alloc(FuriMutexTypeNormal);

	FuriMessageQueue* queue = furi_message_queue_alloc(8, sizeof(InputEvent));
	ViewPort* vp = view_port_alloc();
	view_port_draw_callback_set(vp, draw_cb, s);
	view_port_input_callback_set(vp, input_cb, queue);
	Gui* gui = furi_record_open(RECORD_GUI);
	gui_add_view_port(gui, vp, GuiLayerFullscreen);

	probe(s);
	view_port_update(vp);

	InputEvent ev;
	bool running = true;
	while(running) {
		if(furi_message_queue_get(queue, &ev, 100) == FuriStatusOk && ev.type == InputTypeShort) {
			if(ev.key == InputKeyBack) running = false;
			if(ev.key == InputKeyOk) probe(s);
		}
		if(s->detected) {
			float a[3];
			if(imu_read_accel(a)) {
				furi_mutex_acquire(s->mutex, FuriWaitForever);
				memcpy(s->accel, a, sizeof(a));
				s->reads++;
				furi_mutex_release(s->mutex);
			}
		}
		view_port_update(vp);
	}

	imu_deinit();
	gui_remove_view_port(gui, vp);
	furi_record_close(RECORD_GUI);
	view_port_free(vp);
	furi_message_queue_free(queue);
	furi_mutex_free(s->mutex);
	free(s);
	return 0;
}
