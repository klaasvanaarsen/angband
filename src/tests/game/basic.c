/* game/basic.c */

#include "unit-test.h"
#include "unit-test-data.h"
#include "test-utils.h"

#include <stdio.h>
#include "cave.h"
#include "cmd-core.h"
#include "game-event.h"
#include "game-world.h"
#include "generate.h"
#include "init.h"
#include "mon-make.h"
#include "savefile.h"
#include "score.h"
#include "player.h"
#include "player-birth.h"
#include "player-timed.h"
#include "ui-game.h"
#include "ui-score.h"
#include "ui-term.h"
#include "z-util.h"

static void event_message(game_event_type type, game_event_data *data, void *user) {
	printf("Message: %s\n", data->message.msg);
}

static void println(const char *str) {
	printf("%s\n", str);
}

static int choose_direction(struct chunk *c, struct player *p) {
	int dir = 0;

	while (1) {
		struct loc grid;

		if (dir >= 9) {
			return -1;
		}
		grid.x = p->grid.x + ddx_ddd[dir];
		grid.y = p->grid.y + ddy_ddd[dir];
		if (square_isempty(c, grid)) {
			return ddd[dir];
		}
		++dir;
	}
}

static int reverse_direction(int dir) {
	int rdir[10] = { 5, 9, 8, 7, 6, 5, 4, 3, 2, 1 };

	return (dir >= 0 && dir <= 9) ? rdir[dir] : -1;
}

static void reset_before_load(void) {
	play_again = true;
	wipe_mon_list(cave, player);
	cleanup_angband();
	chunk_list_max = 0;
	init_angband();
	play_again = false;
}

int setup_tests(void **state) {
	/* Register a basic error handler */
	plog_aux = println;

	/* Register some display functions */
	event_add_handler(EVENT_MESSAGE, event_message, NULL);
	event_add_handler(EVENT_INITSTATUS, event_message, NULL);

	/* Init the game */
	set_file_paths();
	init_angband();
#ifdef UNIX
	/* Necessary for creating the randart file. */
	create_needed_dirs();
#endif

	return 0;
}

int teardown_tests(void *state) {
	file_delete("Test1");
	file_delete("TestScore");
	file_delete("TestScoreDead");
	file_delete("TestScoreBroken");
	wipe_mon_list(cave, player);
	cleanup_angband();
	return 0;
}

static int test_newgame(void *state) {

	/* Try making a new game */
	eq(player_make_simple(NULL, NULL, "Tester"), true);

	eq(player->is_dead, false);
	prepare_next_level(player);
	on_new_level();
	notnull(cave);
	eq(player->chp, player->mhp);
	eq(player->timed[TMD_FOOD], PY_FOOD_FULL - 1);

	/* Should be all set up to save properly now */
	eq(savefile_save("Test1"), true);

	/* Make sure it saved properly */
	eq(file_exists("Test1"), true);

	ok;
}

static int test_loadgame(void *state) {
	reset_before_load();

	/* Try loading the just-saved game */
	eq(savefile_load("Test1", false), true);

	eq(player->is_dead, false);
	notnull(cave);
	eq(player->chp, player->mhp);
	eq(player->timed[TMD_FOOD], PY_FOOD_FULL - 1);

	ok;
}

static int test_living_save_score(void *state) {
	struct high_score expected, actual;
	ang_file *broken;
	int32_t saved_max_exp = player->max_exp;
	int16_t saved_max_depth = player->max_depth;
	int16_t saved_depth = player->depth;

	player->max_exp = 12345;
	player->max_depth = 20;
	player->depth = 12;
	build_score(&expected, player, ALIVE_HOW, NULL);
	require(savefile_save("TestScore"));
	player->max_exp = saved_max_exp;
	player->max_depth = saved_max_depth;
	player->depth = saved_depth;

	require(savefile_get_living_score("TestScore", &actual));
	require(streq(expected.who, actual.who));
	require(streq(expected.pts, actual.pts));
	require(streq(expected.gold, actual.gold));
	require(streq(expected.turns, actual.turns));
	require(streq(expected.p_r, actual.p_r));
	require(streq(expected.p_c, actual.p_c));
	require(streq(expected.cur_lev, actual.cur_lev));
	require(streq(expected.cur_dun, actual.cur_dun));
	require(streq(expected.max_lev, actual.max_lev));
	require(streq(expected.max_dun, actual.max_dun));
	require(streq(actual.how, ALIVE_HOW));
	require(streq(actual.day, "SAVED"));
	eq(player->max_exp, saved_max_exp);
	eq(player->depth, saved_depth);

	player->is_dead = true;
	require(savefile_save("TestScoreDead"));
	player->is_dead = false;
	require(!savefile_get_living_score("TestScoreDead", &actual));
	require(!savefile_get_living_score("TestScoreMissing", &actual));
	broken = file_open("TestScoreBroken", MODE_WRITE, FTYPE_RAW);
	notnull(broken);
	require(file_write(broken, "SaveVNLA", 8));
	require(file_close(broken));
	require(!savefile_get_living_score("TestScoreBroken", &actual));
	ok;
}

#ifndef SETGID
static int test_hall_of_fame_living(void *state) {
	term test_term;
	char *old_save_dir = ANGBAND_DIR_SAVE;
	char *old_score_dir = ANGBAND_DIR_SCORES;
	char old_savefile[sizeof(savefile)];
	char old_name[sizeof(player->full_name)];
	char first[81], second[81], third[81], status[81];
	bool old_generated = character_generated;
	wchar_t ch;
	int attr, x, living_attrs[3], current_attrs[3];

	my_strcpy(old_savefile, savefile, sizeof(old_savefile));
	my_strcpy(old_name, player->full_name, sizeof(old_name));
	ANGBAND_DIR_SAVE = ".";
	ANGBAND_DIR_SCORES = "TestScoreMissing";
	path_build(savefile, sizeof(savefile), ".", "Test1");
	my_strcpy(player->full_name, "CurrentTester", sizeof(player->full_name));
	character_generated = true;
	term_init(&test_term, 80, 24, 16);
	Term_activate(&test_term);
	angband_term[0] = &test_term;
	Term_keypress(ESCAPE, 0);
	predict_score(true);

	for (x = 0; x < 3; ++x) {
		Term_what(15, 2 + x, &living_attrs[x], &ch);
		Term_what(15, 6 + x, &current_attrs[x], &ch);
	}

	for (x = 0; x < 80; ++x) {
		Term_what(x, 2, &attr, &ch);
		first[x] = ch ? (char)ch : ' ';
		Term_what(x, 6, &attr, &ch);
		second[x] = ch ? (char)ch : ' ';
		Term_what(x, 10, &attr, &ch);
		third[x] = ch ? (char)ch : ' ';
		Term_what(x, 3, &attr, &ch);
		status[x] = ch ? (char)ch : ' ';
	}
	first[80] = second[80] = third[80] = status[80] = '\0';

	term_nuke(&test_term);
	angband_term[0] = NULL;
	ANGBAND_DIR_SAVE = old_save_dir;
	ANGBAND_DIR_SCORES = old_score_dir;
	my_strcpy(savefile, old_savefile, sizeof(savefile));
	my_strcpy(player->full_name, old_name, sizeof(player->full_name));
	character_generated = old_generated;

	require(strstr(first, "Tester the") != NULL);
	require(strstr(second, "CurrentTester the") != NULL);
	require(strstr(third, "Tester") == NULL);
	require(strstr(status, "Alive on dungeon level 12") != NULL);
	for (x = 0; x < 3; ++x) {
		eq(living_attrs[x], COLOUR_YELLOW);
		eq(current_attrs[x], COLOUR_L_GREEN);
	}
	ok;
}
#endif

static int test_stairs1(void *state) {
	reset_before_load();

	/* Load the saved game */
	eq(savefile_load("Test1", false), true);

	/* Perform normal set up after loading. */
	require(character_dungeon);
	on_new_level();

	cmdq_push(CMD_GO_DOWN);
	run_game_loop();
	eq(player->depth, 1);

	ok;
}

static int test_stairs2(void *state) {
	bool at_stairs = true;
	int dir;

	reset_before_load();

	/* Load the saved game */
	eq(savefile_load("Test1", false), true);

	/* Perform normal set up after loading. */
	require(character_dungeon);
	on_new_level();

	dir = choose_direction(cave, player);
	if (dir >= 0) {
		cmdq_push(CMD_WALK);
		cmd_set_arg_direction(cmdq_peek(), "direction", dir);
		run_game_loop();
		if (!square_monster(cave, loc_sum(player->grid,
				ddgrid[reverse_direction(dir)]))) {
			cmdq_push(CMD_WALK);
			cmd_set_arg_direction(cmdq_peek(), "direction",
				reverse_direction(dir));
			run_game_loop();
		} else {
			/*
			 * A monster got in the way.  Skip testing if can go
			 * down the stairs and report the test was successful.
			 */
			at_stairs = false;
		}
	}
	if (at_stairs) {
		cmdq_push(CMD_GO_DOWN);
		run_game_loop();
		eq(player->depth, 1);
	}

	ok;
}

static int test_drop_pickup(void *state) {
	int dir;

	reset_before_load();

	/* Load the saved game */
	eq(savefile_load("Test1", false), true);

	/* Perform normal set up after loading. */
	require(character_dungeon);
	on_new_level();

	dir = choose_direction(cave, player);
	if (dir >= 0) {
		cmdq_push(CMD_WALK);
		cmd_set_arg_direction(cmdq_peek(), "direction", dir);
		run_game_loop();
		if (player->upkeep->inven[0]->number > 1) {
			cmdq_push(CMD_DROP);
			cmd_set_arg_item(cmdq_peek(), "item",
				player->upkeep->inven[0]);
			cmd_set_arg_number(cmdq_peek(), "quantity", 1);
			run_game_loop();
			eq(square_object(cave, player->grid)->number, 1);
			cmdq_push(CMD_AUTOPICKUP);
			run_game_loop();
		}
		null(square_object(cave, player->grid));
	}

	ok;
}

static int test_drop_eat(void *state) {
	int num = 0;
	int dir;

	reset_before_load();

	/* Load the saved game */
	eq(savefile_load("Test1", false), true);
	num = player->upkeep->inven[0]->number;

	/* Perform normal set up after loading. */
	require(character_dungeon);
	on_new_level();

	dir = choose_direction(cave, player);
	if (dir >= 0) {
		cmdq_push(CMD_WALK);
		cmd_set_arg_direction(cmdq_peek(), "direction", dir);
		run_game_loop();
		cmdq_push(CMD_DROP);
		cmd_set_arg_item(cmdq_peek(), "item", player->upkeep->inven[0]);
		cmd_set_arg_number(cmdq_peek(), "quantity",
					   player->upkeep->inven[0]->number);
		run_game_loop();
		eq(square_object(cave, player->grid)->number, num);
		cmdq_push(CMD_EAT);
		cmd_set_arg_item(cmdq_peek(), "item",
					 square_object(cave, player->grid));
		run_game_loop();
		if (num > 1) {
			eq(square_object(cave, player->grid)->number, num - 1);
		} else {
			null(square_object(cave, player->grid));
		}
	}

	ok;
}

const char *suite_name = "game/basic";
struct test tests[] = {
	{ "newgame", test_newgame },
	{ "living_save_score", test_living_save_score },
#ifndef SETGID
	{ "hall_of_fame_living", test_hall_of_fame_living },
#endif
	{ "loadgame", test_loadgame },
	{ "stairs1", test_stairs1 },
	{ "stairs2", test_stairs2 },
	{ "droppickup", test_drop_pickup },
	{ "dropeat", test_drop_eat },
	{ NULL, NULL }
};
