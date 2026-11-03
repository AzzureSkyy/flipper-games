#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Real seconds per game minute; lower it to speed the pet up.
#define SECONDS_PER_TICK 60
#define HATCH_TICKS 1
#define CHILD_TICKS 10
#define ADULT_TICKS 60
#define HUNGER_EVERY 5
#define STRENGTH_EVERY 7
#define POOP_EVERY 10
#define STARVE_MISTAKE_TICKS 10
#define DEATH_NEGLECT_TICKS 60
#define MAX_OFFLINE_TICKS (24 * 60)
#define SAVE_PATH APP_DATA_PATH("digipet.sav")
#define SAVE_VERSION 1

typedef enum { StageEgg, StageBaby, StageChild, StageAdult, StageGone } Stage;
typedef enum { ModeMain, ModeStatus, ModeTrain, ModeBattle, ModeMessage } Mode;
typedef enum { ActFeed, ActTrain, ActBattle, ActClean, ActMedic, ActStatus, ActCount } Action;

typedef struct {
	uint32_t version;
	uint32_t last_seen;
	uint32_t age;
	uint8_t stage;
	uint8_t hunger;
	uint8_t strength;
	uint8_t poop;
	uint8_t sick;
	uint8_t mistakes;
	uint8_t weight;
	uint8_t good_adult;
	uint16_t wins;
	uint16_t battles;
	uint16_t starving;
	uint16_t neglect;
} Pet;

typedef struct {
	FuriMutex* mutex;
	Pet pet;
	Mode mode;
	Action action;
	uint32_t last_tick_ms;
	uint32_t anim;
	int train_pos;
	int train_dir;
	bool battle_done;
	bool battle_won;
	char message[40];
} App;

static const char* const spr_egg[] = {
	"......####......", "....##....##....", "...#........#...", "..#...##.....#..",
	"..#..####....#..", ".#....##......#.", ".#............#.", ".#...##....#..#.",
	".#..####..###.#.", ".#...##....#..#.", ".#............#.", "..#..........#..",
	"..#..........#..", "...#........#...", "....##....##....", "......####......"};
static const char* const spr_baby[] = {
	"................", "................", "................", "................",
	"................", "......####......", "....##....##....", "...#........#...",
	"..#..##..##..#..", "..#..##..##..#..", "..#..........#..", "..#...####...#..",
	"...#........#...", "....########....", "................", "................"};
static const char* const spr_child[] = {
	"................", "..#..........#..", "..##........##..", "..#.#......#.#..",
	"..#..######..#..", "..#..........#..", ".#...##..##...#.", ".#...##..##...#.",
	".#............#.", ".#.....##.....#.", "..#..######..#..", "...#........#...",
	"....########....", "....#.#..#.#....", "....##....##....", "................"};
static const char* const spr_drake[] = {
	"..#..........#..", "..##........##..", "...##..##..##...", "....########....",
	"...#........#...", "..#..##..##..#..", "..#..##..##..#..", "#.#..........#.#",
	"###...####...###", "#.#..#....#..#.#", "...#..####..#...", "....#......#....",
	"...##########...", "...#.#....#.#...", "..##.#....#.##..", "................"};
static const char* const spr_sludge[] = {
	"................", "................", "................", ".......##.......",
	"......####......", "....########....", "...#........#...", "..#.###..###.#..",
	"..#.#.#..#.#.#..", ".#............#.", ".#..#.#..#.#..#.", ".#...#.##.#...#.",
	"#..............#", "#.##..####..##.#", ".#..##....##..#.", "................"};
static const char* const spr_poop[] = {
	"...#....", "..##....", ".####...", "######..", "########", "........"};

static const char* const action_names[ActCount] = {
	"Feed", "Train", "Battle", "Clean", "Medic", "Status"};
static const char* const stage_names[] = {"Egg", "Baby", "Child", "Adult", "Gone"};

static const char* const* pet_sprite(const Pet* p) {
	switch(p->stage) {
	case StageEgg:
		return spr_egg;
	case StageBaby:
		return spr_baby;
	case StageChild:
		return spr_child;
	default:
		return p->good_adult ? spr_drake : spr_sludge;
	}
}

static const char* pet_name(const Pet* p) {
	switch(p->stage) {
	case StageEgg:
		return "DigiEgg";
	case StageBaby:
		return "Blipling";
	case StageChild:
		return "Clawpup";
	case StageAdult:
		return p->good_adult ? "Pyrodrake" : "Sludgeon";
	default:
		return "Departed";
	}
}

static void draw_sprite(Canvas* c, const char* const* rows, int count, int x, int y, int scale, bool flip) {
	for(int r = 0; r < count; r++) {
		int len = strlen(rows[r]);
		for(int col = 0; col < len; col++) {
			if(rows[r][flip ? len - 1 - col : col] == '#') {
				canvas_draw_box(c, x + col * scale, y + r * scale, scale, scale);
			}
		}
	}
}

static void draw_meter(Canvas* c, int x, int y, const char* label, int value) {
	canvas_draw_str(c, x, y, label);
	for(int i = 0; i < 4; i++) {
		if(i < value)
			canvas_draw_box(c, x + 50 + i * 10, y - 7, 8, 8);
		else
			canvas_draw_frame(c, x + 50 + i * 10, y - 7, 8, 8);
	}
}

static void pet_new(Pet* p) {
	memset(p, 0, sizeof(Pet));
	p->version = SAVE_VERSION;
	p->stage = StageEgg;
	p->hunger = 2;
	p->strength = 1;
	p->weight = 5;
	p->last_seen = furi_hal_rtc_get_timestamp();
}

static void pet_tick(Pet* p) {
	if(p->stage == StageGone) return;
	p->age++;

	if(p->stage == StageEgg) {
		if(p->age >= HATCH_TICKS) p->stage = StageBaby;
		return;
	}
	if(p->age % HUNGER_EVERY == 0 && p->hunger > 0) p->hunger--;
	if(p->age % STRENGTH_EVERY == 0 && p->strength > 0) p->strength--;
	if(p->age % POOP_EVERY == 0 && p->poop < 4) p->poop++;
	if(p->poop >= 3 && !p->sick && rand() % 4 == 0) p->sick = 1;

	if(p->hunger == 0) {
		p->starving++;
		if(p->starving == STARVE_MISTAKE_TICKS && p->mistakes < 255) p->mistakes++;
	} else {
		p->starving = 0;
	}
	if(p->hunger == 0 && p->sick) {
		if(++p->neglect >= DEATH_NEGLECT_TICKS) p->stage = StageGone;
	} else {
		p->neglect = 0;
	}

	if(p->stage == StageBaby && p->age >= CHILD_TICKS) p->stage = StageChild;
	if(p->stage == StageChild && p->age >= ADULT_TICKS) {
		p->stage = StageAdult;
		p->good_adult = p->mistakes <= 2 && p->weight <= 30;
	}
}

static void pet_save(const Pet* p) {
	Storage* storage = furi_record_open(RECORD_STORAGE);
	File* f = storage_file_alloc(storage);
	if(storage_file_open(f, SAVE_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
		storage_file_write(f, p, sizeof(Pet));
	}
	storage_file_close(f);
	storage_file_free(f);
	furi_record_close(RECORD_STORAGE);
}

static void pet_load(Pet* p) {
	bool ok = false;
	Storage* storage = furi_record_open(RECORD_STORAGE);
	File* f = storage_file_alloc(storage);
	if(storage_file_open(f, SAVE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
		ok = storage_file_read(f, p, sizeof(Pet)) == sizeof(Pet) && p->version == SAVE_VERSION;
	}
	storage_file_close(f);
	storage_file_free(f);
	furi_record_close(RECORD_STORAGE);
	if(!ok) {
		pet_new(p);
		return;
	}
	// Catch up on time spent with the app closed.
	uint32_t now = furi_hal_rtc_get_timestamp();
	if(now > p->last_seen) {
		uint32_t ticks = (now - p->last_seen) / SECONDS_PER_TICK;
		if(ticks > MAX_OFFLINE_TICKS) ticks = MAX_OFFLINE_TICKS;
		for(uint32_t i = 0; i < ticks; i++) pet_tick(p);
	}
	p->last_seen = now;
}

static void show_message(App* app, const char* text) {
	strncpy(app->message, text, sizeof(app->message) - 1);
	app->message[sizeof(app->message) - 1] = '\0';
	app->mode = ModeMessage;
}

static void draw_main(Canvas* c, App* app) {
	const Pet* p = &app->pet;
	canvas_set_font(c, FontSecondary);
	canvas_draw_str(c, 2, 8, pet_name(p));
	if(p->sick) canvas_draw_str(c, 100, 8, "SICK");

	if(p->stage == StageGone) {
		canvas_set_font(c, FontPrimary);
		canvas_draw_str_aligned(c, 64, 30, AlignCenter, AlignCenter, "Your pet has gone...");
		canvas_set_font(c, FontSecondary);
		canvas_draw_str_aligned(c, 64, 46, AlignCenter, AlignCenter, "OK: new egg");
		return;
	}

	bool frame = (app->anim / 5) % 2;
	int x = (p->stage == StageEgg) ? 48 : 36 + (frame ? 8 : 0);
	draw_sprite(c, pet_sprite(p), 16, x, 12 + (p->stage == StageEgg && frame ? 1 : 0), 2, frame);

	for(int i = 0; i < p->poop; i++) draw_sprite(c, spr_poop, 6, 104 + (i % 2) * 11, 16 + (i / 2) * 9, 1, false);

	canvas_draw_line(c, 0, 52, 127, 52);
	canvas_draw_str_aligned(c, 64, 61, AlignCenter, AlignBottom, action_names[app->action]);
	canvas_draw_str(c, 2, 61, "<");
	canvas_draw_str(c, 122, 61, ">");
}

static void draw_status(Canvas* c, App* app) {
	const Pet* p = &app->pet;
	char line[48];
	canvas_set_font(c, FontPrimary);
	canvas_draw_str(c, 2, 10, pet_name(p));
	canvas_set_font(c, FontSecondary);
	snprintf(line, sizeof(line), "%s  age %lum", stage_names[p->stage], (unsigned long)p->age);
	canvas_draw_str(c, 60, 10, line);
	draw_meter(c, 2, 24, "Hunger", p->hunger);
	draw_meter(c, 2, 35, "Strength", p->strength);
	snprintf(line, sizeof(line), "Wt %ug  Win %u/%u  Err %u", p->weight, p->wins, p->battles, p->mistakes);
	canvas_draw_str(c, 2, 48, line);
	canvas_draw_str(c, 2, 61, "Back: return");
}

static void draw_train(Canvas* c, App* app) {
	canvas_set_font(c, FontPrimary);
	canvas_draw_str_aligned(c, 64, 10, AlignCenter, AlignCenter, "Training!");
	canvas_set_font(c, FontSecondary);
	canvas_draw_str_aligned(c, 64, 22, AlignCenter, AlignCenter, "Press OK in the zone");
	canvas_draw_frame(c, 14, 30, 100, 12);
	canvas_draw_box(c, 54, 30, 20, 3);
	canvas_draw_box(c, 54, 39, 20, 3);
	canvas_draw_box(c, 14 + app->train_pos, 31, 4, 10);
	canvas_draw_str(c, 2, 61, "Back: quit");
}

static void draw_battle(Canvas* c, App* app) {
	bool frame = (app->anim / 3) % 2;
	draw_sprite(c, pet_sprite(&app->pet), 16, 4, 6 + (frame ? 1 : 0), 2, false);
	draw_sprite(c, spr_child, 16, 92, 6 + (frame ? 0 : 1), 2, true);
	canvas_set_font(c, FontPrimary);
	canvas_draw_str_aligned(c, 64, 24, AlignCenter, AlignCenter, "VS");
	canvas_set_font(c, FontSecondary);
	if(!app->battle_done) {
		canvas_draw_str_aligned(c, 64, 52, AlignCenter, AlignCenter, "OK: attack!");
	} else {
		canvas_draw_str_aligned(c, 64, 52, AlignCenter, AlignCenter, app->battle_won ? "You win!" : "You lost...");
		canvas_draw_str_aligned(c, 64, 61, AlignCenter, AlignBottom, "OK: continue");
	}
}

static void draw_cb(Canvas* c, void* ctx) {
	App* app = ctx;
	furi_mutex_acquire(app->mutex, FuriWaitForever);
	canvas_clear(c);
	switch(app->mode) {
	case ModeStatus:
		draw_status(c, app);
		break;
	case ModeTrain:
		draw_train(c, app);
		break;
	case ModeBattle:
		draw_battle(c, app);
		break;
	case ModeMessage:
		canvas_set_font(c, FontPrimary);
		canvas_draw_str_aligned(c, 64, 28, AlignCenter, AlignCenter, app->message);
		canvas_set_font(c, FontSecondary);
		canvas_draw_str_aligned(c, 64, 50, AlignCenter, AlignCenter, "OK");
		break;
	default:
		draw_main(c, app);
		break;
	}
	furi_mutex_release(app->mutex);
}

static void input_cb(InputEvent* event, void* ctx) {
	furi_message_queue_put(ctx, event, FuriWaitForever);
}

static void do_action(App* app) {
	Pet* p = &app->pet;
	if(p->stage == StageGone) {
		pet_new(p);
		return;
	}
	if(p->stage == StageEgg && app->action != ActStatus) {
		show_message(app, "The egg is still warm...");
		return;
	}
	switch(app->action) {
	case ActFeed:
		if(p->hunger >= 4) {
			show_message(app, "Not hungry!");
		} else {
			p->hunger++;
			if(p->weight < 99) p->weight++;
			show_message(app, "Yum!");
		}
		break;
	case ActTrain:
		app->train_pos = 0;
		app->train_dir = 3;
		app->mode = ModeTrain;
		break;
	case ActBattle:
		if(p->hunger == 0 || p->sick) {
			show_message(app, p->sick ? "Too sick to fight" : "Too hungry to fight");
		} else {
			app->battle_done = false;
			app->mode = ModeBattle;
		}
		break;
	case ActClean:
		p->poop = 0;
		show_message(app, "All clean!");
		break;
	case ActMedic:
		if(p->sick) {
			p->sick = 0;
			show_message(app, "Feeling better!");
		} else {
			show_message(app, "Healthy already");
		}
		break;
	default:
		app->mode = ModeStatus;
		break;
	}
}

static void handle_ok(App* app) {
	Pet* p = &app->pet;
	switch(app->mode) {
	case ModeMain:
		do_action(app);
		break;
	case ModeTrain: {
		bool hit = app->train_pos >= 38 && app->train_pos <= 58;
		if(hit) {
			if(p->strength < 4) p->strength++;
			if(p->weight > 1) p->weight--;
		}
		show_message(app, hit ? "Great training!" : "Missed! Try again");
		break;
	}
	case ModeBattle:
		if(!app->battle_done) {
			int power = p->strength * 3 + p->stage * 3 + rand() % 6;
			int enemy = rand() % 10 + p->stage * 3;
			app->battle_won = power >= enemy;
			p->battles++;
			if(app->battle_won) p->wins++;
			if(p->hunger > 0) p->hunger--;
			app->battle_done = true;
		} else {
			app->mode = ModeMain;
		}
		break;
	default:
		app->mode = ModeMain;
		break;
	}
}

int32_t digipet_app(void* p) {
	UNUSED(p);
	App* app = malloc(sizeof(App));
	memset(app, 0, sizeof(App));
	app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
	srand(furi_hal_random_get());
	pet_load(&app->pet);
	app->last_tick_ms = furi_get_tick();

	FuriMessageQueue* queue = furi_message_queue_alloc(8, sizeof(InputEvent));
	ViewPort* vp = view_port_alloc();
	view_port_draw_callback_set(vp, draw_cb, app);
	view_port_input_callback_set(vp, input_cb, queue);
	Gui* gui = furi_record_open(RECORD_GUI);
	gui_add_view_port(gui, vp, GuiLayerFullscreen);

	bool running = true;
	InputEvent ev;
	while(running) {
		bool got = furi_message_queue_get(queue, &ev, 100) == FuriStatusOk;
		furi_mutex_acquire(app->mutex, FuriWaitForever);
		if(got && (ev.type == InputTypeShort || ev.type == InputTypeRepeat)) {
			if(ev.key == InputKeyBack) {
				if(app->mode == ModeMain)
					running = false;
				else
					app->mode = ModeMain;
			} else if(app->mode == ModeMain && ev.key == InputKeyLeft) {
				app->action = (app->action + ActCount - 1) % ActCount;
			} else if(app->mode == ModeMain && ev.key == InputKeyRight) {
				app->action = (app->action + 1) % ActCount;
			} else if(ev.key == InputKeyOk) {
				handle_ok(app);
			}
		}

		app->anim++;
		if(app->mode == ModeTrain) {
			app->train_pos += app->train_dir;
			if(app->train_pos <= 0 || app->train_pos >= 96) app->train_dir = -app->train_dir;
		}
		if(furi_get_tick() - app->last_tick_ms >= SECONDS_PER_TICK * 1000U) {
			app->last_tick_ms = furi_get_tick();
			pet_tick(&app->pet);
			app->pet.last_seen = furi_hal_rtc_get_timestamp();
			pet_save(&app->pet);
		}
		furi_mutex_release(app->mutex);
		view_port_update(vp);
	}

	app->pet.last_seen = furi_hal_rtc_get_timestamp();
	pet_save(&app->pet);

	gui_remove_view_port(gui, vp);
	furi_record_close(RECORD_GUI);
	view_port_free(vp);
	furi_message_queue_free(queue);
	furi_mutex_free(app->mutex);
	free(app);
	return 0;
}
