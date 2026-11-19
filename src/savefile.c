/**
 * \file savefile.c
 * \brief Savefile loading and saving main routines
 *
 * Copyright (c) 2009 Andi Sidwell <andi@takkaria.org>
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */
#include <errno.h>
#include "angband.h"
#include "game-world.h"
#include "init.h"
#include "obj-knowledge.h"
#include "savefile.h"
#include "save-charoutput.h"
#include "score.h"
#include "z-file.h"

/**
 * The savefile code.
 *
 * Savefiles since ~3.1 have used a block-based system.  Each savefile
 * consists of an 8-byte header, the first four bytes of which mark this
 * as a savefile, the second four bytes provide a variant ID.
 *
 * After that, each block has the format:
 * - 16-byte string giving the type of block
 * - 4-byte block version
 * - 4-byte block size
 * - 4-byte block checksum
 * ... data ...
 * padding so that block is a multiple of 4 bytes
 *
 * The savefile deosn't contain the version number of that game that saved it;
 * versioning is left at the individual block level.  The current code
 * keeps a list of savefile blocks to save in savers[] below, along with
 * their current versions.
 *
 * For each block type and version, there is a loading function to load that
 * type/version combination.  For example, there may be a loader for v1
 * and v2 of the RNG block; these must be different functions.  It has been
 * done this way since it allows easier maintenance; after each release, you
 * need simply remove old loaders and you will not have to disentangle
 * lots of code with "if (version > 3)" and its like everywhere.
 *
 * Savefile loading and saving is done by keeping the current block in
 * memory, which is accessed using the wr_* and rd_* functions.  This is
 * then written out, whole, to disk, with the appropriate header.
 *
 *
 * So, if you want to make a savefile compat-breaking change, then there are
 * a few things you should do:
 *
 * - increment the version in 'savers' below
 * - add a loading function that accepts the new version (in addition to
 *   the previous loading function) to 'loaders'
 * - and watch the magic happen.
 *
 *
 * TODO:
 * - wr_ and rd_ should be passed a buffer to work with, rather than using
 *   the rd_ and wr_ functions with a universal buffer
 * - 
 */

/**
 * Global "we've just saved" variable
 */
bool character_saved;

/**
 * Magic bits at beginning of savefile
 */
static const uint8_t savefile_magic[4] = { 83, 97, 118, 101 };
static const uint8_t savefile_name[4] = { 'V', 'N', 'L', 'A' };

/* Some useful types */
typedef int (*loader_t)(void);

struct blockheader {
	char name[16];
	uint32_t version;
	uint32_t size;
};

struct blockinfo {
	char name[16];
	loader_t loader;
	uint32_t version;
};

/**
 * Savefile saving functions
 */
static const struct {
	char name[16];
	void (*save)(void);
	uint32_t version;
} savers[] = {
	{ "description", wr_description, 1 },
	{ "rng", wr_randomizer, 1 },
	{ "options", wr_options, 1 },
	{ "messages", wr_messages, 1 },
	{ "monster memory", wr_monster_memory, 1 },
	{ "object memory", wr_object_memory, 1 },
	{ "quests", wr_quests, 1 },
	{ "player", wr_player, 1 },
	{ "ignore", wr_ignore, 1 },
	{ "misc", wr_misc, 1 },
	{ "artifacts", wr_artifacts, 1 },
	{ "player hp", wr_player_hp, 1 },
	{ "player spells", wr_player_spells, 1 },
	{ "gear", wr_gear, 1 },
	{ "stores", wr_stores, 1 },
	{ "dungeon", wr_dungeon, 1 },
	{ "objects", wr_objects, 1 },
	{ "monsters", wr_monsters, 1 },
	{ "traps", wr_traps, 1 },
	{ "chunks", wr_chunks, 1 },
	{ "history", wr_history, 1 },
};

/**
 * Savefile loading functions
 */
static const struct blockinfo loaders[] = {
	{ "description", rd_null, 1 },
	{ "rng", rd_randomizer, 1 },
	{ "options", rd_options, 1 },
	{ "messages", rd_messages, 1 },
	{ "monster memory", rd_monster_memory, 1 },
	{ "object memory", rd_object_memory, 1 },
	{ "quests", rd_quests, 1 },
	{ "player", rd_player, 1 },
	{ "ignore", rd_ignore, 1 },
	{ "misc", rd_misc, 1 },
	{ "artifacts", rd_artifacts, 1 },
	{ "player hp", rd_player_hp, 1 },
	{ "player spells", rd_player_spells, 1 },
	{ "gear", rd_gear, 1 },	
	{ "stores", rd_stores, 1 },	
	{ "dungeon", rd_dungeon, 1 },
	{ "objects", rd_objects, 1 },	
	{ "monsters", rd_monsters, 1 },
	{ "traps", rd_traps, 1 },
	{ "chunks", rd_chunks, 1 },
	{ "history", rd_history, 1 },
};


/* Buffer bits */
static uint8_t *buffer;
static uint32_t buffer_size;
static uint32_t buffer_pos;
static uint32_t buffer_check;

#define BUFFER_INITIAL_SIZE		1024
#define BUFFER_BLOCK_INCREMENT	1024

#define SAVEFILE_HEAD_SIZE		28


/**
 * ------------------------------------------------------------------------
 * Utility
 * ------------------------------------------------------------------------ */


/**
 * Tell the UI something about loading the game.
 */
void note(const char *message)
{
	event_signal_message(EVENT_INITSTATUS, MSG_BIRTH, message);
}


/**
 * ------------------------------------------------------------------------
 * Base put/get
 * ------------------------------------------------------------------------ */

static void sf_put(uint8_t v)
{
	assert(buffer != NULL);
	assert(buffer_size > 0);

	if (buffer_size == buffer_pos)
	{
		buffer_size += BUFFER_BLOCK_INCREMENT;
		buffer = mem_realloc(buffer, buffer_size);
	}

	assert(buffer_pos < buffer_size);

	buffer[buffer_pos++] = v;
	buffer_check += v;
}

static uint8_t sf_get(void)
{
	if ((buffer == NULL) || (buffer_size <= 0) || (buffer_pos >= buffer_size))
		quit("Broken savefile - probably from a development version");

	buffer_check += buffer[buffer_pos];

	return buffer[buffer_pos++];
}


/**
 * ------------------------------------------------------------------------
 * Accessor functions
 * ------------------------------------------------------------------------ */

void wr_byte(uint8_t v)
{
	sf_put(v);
}

void wr_u16b(uint16_t v)
{
	sf_put((uint8_t)(v & 0xFF));
	sf_put((uint8_t)((v >> 8) & 0xFF));
}

void wr_s16b(int16_t v)
{
	wr_u16b((uint16_t)v);
}

void wr_u32b(uint32_t v)
{
	sf_put((uint8_t)(v & 0xFF));
	sf_put((uint8_t)((v >> 8) & 0xFF));
	sf_put((uint8_t)((v >> 16) & 0xFF));
	sf_put((uint8_t)((v >> 24) & 0xFF));
}

void wr_s32b(int32_t v)
{
	wr_u32b((uint32_t)v);
}

void wr_string(const char *str)
{
	while (*str)
	{
		wr_byte(*str);
		str++;
	}
	wr_byte(*str);
}


void rd_byte(uint8_t *ip)
{
	*ip = sf_get();
}

void rd_u16b(uint16_t *ip)
{
	(*ip) = sf_get();
	(*ip) |= ((uint16_t)(sf_get()) << 8);
}

void rd_s16b(int16_t *ip)
{
	rd_u16b((uint16_t*)ip);
}

void rd_u32b(uint32_t *ip)
{
	(*ip) = sf_get();
	(*ip) |= ((uint32_t)(sf_get()) << 8);
	(*ip) |= ((uint32_t)(sf_get()) << 16);
	(*ip) |= ((uint32_t)(sf_get()) << 24);
}

void rd_s32b(int32_t *ip)
{
	rd_u32b((uint32_t*)ip);
}

void rd_string(char *str, int max)
{
	uint8_t tmp8u;
	int i = 0;

	do {
		rd_byte(&tmp8u);

		if (i < max) str[i] = tmp8u;
		if (!tmp8u) break;
	} while (++i);

	str[max - 1] = '\0';
}

void strip_bytes(int n)
{
	uint8_t tmp8u;
	while (n--) rd_byte(&tmp8u);
}

void pad_bytes(int n)
{
	while (n--) wr_byte(0);
}


/**
 * ------------------------------------------------------------------------
 * Savefile saving functions
 * ------------------------------------------------------------------------ */


static bool try_save(ang_file *file)
{
	uint8_t savefile_head[SAVEFILE_HEAD_SIZE];
	size_t i, pos;
	bool success = true;

	/* Start off the buffer */
	buffer = mem_alloc(BUFFER_INITIAL_SIZE);
	buffer_size = BUFFER_INITIAL_SIZE;

	for (i = 0; i < N_ELEMENTS(savers); i++) {
		buffer_pos = 0;
		buffer_check = 0;

		savers[i].save();

		/* 16-byte block name */
		pos = my_strcpy((char *)savefile_head,
				savers[i].name,
				sizeof savefile_head);
		while (pos < 16)
			savefile_head[pos++] = 0;

#define SAVE_U32B(v)	\
		savefile_head[pos++] = (v & 0xFF); \
		savefile_head[pos++] = ((v >> 8) & 0xFF); \
		savefile_head[pos++] = ((v >> 16) & 0xFF); \
		savefile_head[pos++] = ((v >> 24) & 0xFF);

		SAVE_U32B(savers[i].version);
		SAVE_U32B(buffer_pos);
		SAVE_U32B(buffer_check);

		assert(pos == SAVEFILE_HEAD_SIZE);

		if (! file_write(file, (char *)savefile_head,
				SAVEFILE_HEAD_SIZE)) {
			success = false;
		}
		if (! file_write(file, (char *)buffer, buffer_pos)) {
			success = false;
		}

		/* pad to 4 byte multiples */
		if (buffer_pos % 4) {
			if (! file_write(file, "xxx", 4 - (buffer_pos % 4))) {
				success = false;
			}
		}
	}

	mem_free(buffer);

	return success;
}

/**
 * Attempt to save the player in a savefile
 */
bool savefile_save(const char *path)
{
	ang_file *file;
	char new_savefile[1024];
	char old_savefile[1024];
	bool ok = false;

	/* Generate a CharOutput.txt, mainly for angband.live, when saving. */
	(void) save_charoutput();

	/* New savefile */
	safe_setuid_grab();
	file_get_savefile(old_savefile, sizeof(old_savefile), path, "old");

	/* Open the savefile */
        file_get_savefile(new_savefile, sizeof(new_savefile), path, "new");

	file = file_open(new_savefile, MODE_WRITE, FTYPE_SAVE);
	safe_setuid_drop();

	if (file) {
		if (file_write(file, (char*)&savefile_magic, 4)
				&& file_write(file, (char*)&savefile_name, 4)) {
			ok = try_save(file);
		}
		if (!file_close(file)) {
			ok = false;
		}
	}

	if (ok) {
		/*
		 * Moving the files about, if interrupted, could leave no save
		 * file in place.
		 */
		character_saved = false;

		safe_setuid_grab();

		if (file_exists(path) && !file_move(path, old_savefile)) {
			ok = false;
		} else if (!file_move(new_savefile, path)) {
			ok = false;
			(void)file_move(old_savefile, path);
		} else {
			(void)file_delete(old_savefile);
		} 

		safe_setuid_drop();

		character_saved = ok;

		return ok;
	}

	/* Delete temp file if the save failed */
	if (file) {
		/* File is no longer valid, but it still points to a non zero
		 * value if the file was created above */
		safe_setuid_grab();
		file_delete(new_savefile);
		safe_setuid_drop();
	}
	return false;
}



/**
 * ------------------------------------------------------------------------
 * Savefile loading functions
 * ------------------------------------------------------------------------ */

/**
 * Check the savefile header file clearly inicates that it's a savefile
 */
static bool check_header(ang_file *f) {
	uint8_t head[8];

	if (file_read(f, (char *) &head, 8) == 8 &&
			memcmp(&head[0], savefile_magic, 4) == 0 &&
			memcmp(&head[4], savefile_name, 4) == 0)
		return true;

	return false;
}

/**
 * Get the next block header from the savefile
 */
static errr next_blockheader(ang_file *f, struct blockheader *b) {
	uint8_t savefile_head[SAVEFILE_HEAD_SIZE];
	size_t len;

	len = file_read(f, (char *)savefile_head, SAVEFILE_HEAD_SIZE);
	if (len == 0) /* no more blocks */
		return 1;

	if (len != SAVEFILE_HEAD_SIZE || savefile_head[15] != 0) {
		return -1;
	}

#define RECONSTRUCT_U32B(from) \
	((uint32_t) savefile_head[from]) | \
	((uint32_t) savefile_head[from+1] << 8) | \
	((uint32_t) savefile_head[from+2] << 16) | \
	((uint32_t) savefile_head[from+3] << 24);

	my_strcpy(b->name, (char *)&savefile_head, sizeof b->name);
	b->version = RECONSTRUCT_U32B(16);
	b->size = RECONSTRUCT_U32B(20);

	/* Pad to 4 bytes */
	if (b->size % 4) {
		if (b->size > UINT32_MAX - 3) return -1;
		b->size += 4 - (b->size % 4);
	}

	return 0;
}

/**
 * Find the right loader for this block, return it
 */
static loader_t find_loader(struct blockheader *b,
							const struct blockinfo *local_loaders)
{
	size_t i = 0;

	/* Find the right loader */
	for (i = 0; local_loaders[i].name[0]; i++) {
		if (!streq(b->name, local_loaders[i].name)) continue;
		if (b->version != local_loaders[i].version) continue;

		return local_loaders[i].loader;
	} 

	return NULL;
}

/**
 * Load a given block with the given loader
 */
static bool load_block(ang_file *f, struct blockheader *b, loader_t loader)
{
	/* Allocate space for the buffer */
	buffer = mem_alloc(b->size);
	buffer_pos = 0;
	buffer_check = 0;

	buffer_size = file_read(f, (char *) buffer, b->size);
	if (buffer_size != b->size ||
			loader() != 0) {
		mem_free(buffer);
		return false;
	}

	mem_free(buffer);
	return true;
}

/**
 * Skip a block
 */
static void skip_block(ang_file *f, struct blockheader *b)
{
	file_skip(f, b->size);
}

/**
 * Try to load a savefile
 */
static bool try_load(ang_file *f, const struct blockinfo *local_loaders)
{
	struct blockheader b;
	errr err;

	if (!check_header(f)) {
		note("Savefile is corrupted -- incorrect file header.");
		return false;
	}

	/* Get the next block header */
	while ((err = next_blockheader(f, &b)) == 0) {
		loader_t loader = find_loader(&b, local_loaders);
		if (!loader) {
			note("Savefile block can't be read.");
			note("Maybe try and load the savefile in an earlier version of Angband.");
			return false;
		}

		if (!load_block(f, &b, loader)) {
			note(format("Savefile corrupted - Couldn't load block %s", b.name));
			return false;
		}
	}

	if (err == -1) {
		note("Savefile is corrupted -- block header mangled.");
		return false;
	}

	return true;
}

/* XXX this isn't nice but it'll have to do */
static char savefile_desc[120];

static int get_desc(void) {
	rd_string(savefile_desc, sizeof savefile_desc);
	return 0;
}

/**
 * Try to get the 'description' block from a savefile.  Fail gracefully.
 */
const char *savefile_get_description(const char *path) {
	struct blockheader b;
	ang_file *f;

	safe_setuid_grab();
	f = file_open(path, MODE_READ, FTYPE_TEXT);
	safe_setuid_drop();
	if (!f) return NULL;

	/* Blank the description */
	savefile_desc[0] = 0;

	if (!check_header(f)) {
		my_strcpy(savefile_desc, "Invalid savefile", sizeof savefile_desc);
	} else {
		while (!next_blockheader(f, &b)) {
			if (!streq(b.name, "description")) {
				skip_block(f, &b);
				continue;
			}
			load_block(f, &b, get_desc);
			break;
		}
	}

	file_close(f);
	return savefile_desc;
}

struct score_reader {
	const uint8_t *data;
	size_t size;
	size_t pos;
};

static bool score_read_bytes(struct score_reader *r, size_t count)
{
	if (count > r->size - r->pos) return false;
	r->pos += count;
	return true;
}

static bool score_read_string(struct score_reader *r, char *out, size_t size)
{
	const uint8_t *end = memchr(r->data + r->pos, 0, r->size - r->pos);
	size_t len;

	if (!end) return false;
	len = (size_t)(end - (r->data + r->pos));
	if (out) {
		if (len >= size) return false;
		memcpy(out, r->data + r->pos, len);
		out[len] = '\0';
	}
	r->pos += len + 1;
	return true;
}

static bool score_read_u16(struct score_reader *r, uint16_t *value)
{
	size_t pos = r->pos;
	if (!score_read_bytes(r, 2)) return false;
	*value = r->data[pos] | ((uint16_t)r->data[pos + 1] << 8);
	return true;
}

static bool score_read_u32(struct score_reader *r, uint32_t *value)
{
	size_t pos = r->pos;
	if (!score_read_bytes(r, 4)) return false;
	*value = (uint32_t)r->data[pos] | ((uint32_t)r->data[pos + 1] << 8) |
		((uint32_t)r->data[pos + 2] << 16) |
		((uint32_t)r->data[pos + 3] << 24);
	return true;
}

static bool read_score_player(struct score_reader *r, struct player *p)
{
	char race_name[80], class_name[80];
	struct player_race *race;
	struct player_class *class;
	uint16_t value, slots;
	uint32_t n;
	uint8_t stats;
	size_t i;

	if (!score_read_string(r, p->full_name, sizeof(p->full_name)) ||
		!score_read_string(r, NULL, 0) || !score_read_string(r, NULL, 0) ||
		!score_read_string(r, race_name, sizeof(race_name)) ||
		!score_read_string(r, NULL, 0) ||
		!score_read_string(r, class_name, sizeof(class_name))) return false;
	for (race = races; race && !streq(race->name, race_name);
			race = race->next) {}
	for (class = classes; class && !streq(class->name, class_name);
			class = class->next) {}
	if (!race || !class) return false;
	p->race = race;
	p->class = class;
	if (!score_read_bytes(r, 9) || !score_read_bytes(r, 1)) return false;
	stats = r->data[r->pos - 1];
	if (stats > STAT_MAX || !score_read_bytes(r, (size_t)stats * 8 + 10) ||
		!score_read_string(r, NULL, 0) || !score_read_u16(r, &slots) ||
		slots > z_info->equip_slots_max) return false;
	for (i = 0; i < slots; ++i) {
		if (!score_read_bytes(r, 2) || !score_read_string(r, NULL, 0))
			return false;
	}
	if (!score_read_bytes(r, 4) || !score_read_u32(r, &n) ||
		n > INT32_MAX) return false;
	p->au = n;
	if (!score_read_u32(r, &n) || n > PY_MAX_EXP) return false;
	p->max_exp = n;
	if (!score_read_bytes(r, 6) || !score_read_u16(r, &value)) return false;
	p->lev = (int16_t)value;
	if (!score_read_bytes(r, 12) || !score_read_u16(r, &value)) return false;
	p->max_lev = (int16_t)value;
	if (!score_read_u16(r, &value)) return false;
	p->max_depth = (int16_t)value;
	if (p->lev < 1 || p->lev > PY_MAX_LEVEL) return false;
	if (p->max_lev < p->lev) p->max_lev = p->lev;
	if (p->max_depth < 0) p->max_depth = 1;
	return true;
}

/*
 * Read just the score fields from a savefile, without changing the current
 * game.  Only the existing version 1 blocks are supported.
 */
bool savefile_get_living_score(const char *path, struct high_score *score)
{
	struct blockheader b;
	struct player saved_player;
	ang_file *f;
	bool have_player = false, have_misc = false, have_dungeon = false;
	bool dead = false, valid = true;
	uint32_t saved_turn = 0;
	errr err = 0;

	memset(&saved_player, 0, sizeof(saved_player));
	safe_setuid_grab();
	f = file_open(path, MODE_READ, FTYPE_RAW);
	safe_setuid_drop();
	if (!f) return false;
	if (!check_header(f)) {
		file_close(f);
		return false;
	}

	while ((err = next_blockheader(f, &b)) == 0) {
		bool is_player = streq(b.name, "player");
		bool is_misc = streq(b.name, "misc");
		bool is_dungeon = streq(b.name, "dungeon");
		if (is_player || is_misc || is_dungeon) {
			uint8_t *data;
			struct score_reader r;
			uint16_t value;
			size_t read_size = is_dungeon ? MIN(b.size, 4) : b.size;
			if (b.version != 1 || read_size == 0 || read_size > 65536 ||
				b.size > INT_MAX) {
				valid = false;
				break;
			}
			data = mem_alloc(read_size);
			r.data = data;
			r.size = read_size;
			r.pos = 0;
			if (file_read(f, (char *)data, read_size) != (int)read_size ||
				!file_skip(f, b.size - read_size)) {
				mem_free(data);
				valid = false;
				break;
			}
			if (is_player) {
				valid = !have_player && read_score_player(&r, &saved_player);
				have_player = true;
			} else if (is_misc) {
				valid = !have_misc && score_read_bytes(&r, 12) &&
					score_read_bytes(&r, 1) &&
					score_read_u32(&r, &saved_turn);
				if (valid) dead = r.data[12] != 0;
				have_misc = true;
			} else {
				valid = !have_dungeon && score_read_u16(&r, &value) &&
					value < z_info->max_depth;
				if (valid) saved_player.depth = value;
				have_dungeon = true;
			}
			mem_free(data);
			if (!valid || (have_player && have_misc && have_dungeon))
				break;
		} else if (b.size > INT_MAX || !file_skip(f, b.size)) {
			valid = false;
			break;
		}
	}
	file_close(f);
	if (!valid || err < 0 || !have_player || !have_misc || !have_dungeon) {
		plog_fmt("Could not read score from savefile %s.", path);
		return false;
	}
	if (dead) return false;

	build_score(score, &saved_player, ALIVE_HOW, NULL);
	my_strcpy(score->day, "SAVED", sizeof(score->day));
	strnfmt(score->turns, sizeof(score->turns), "%9ld", (long)saved_turn);
	return true;
}


/**
 * Load a savefile.
 */
bool savefile_load(const char *path, bool cheat_death)
{
	bool ok;
	ang_file *f;

	safe_setuid_grab();
	f = file_open(path, MODE_READ, FTYPE_TEXT);
	safe_setuid_drop();
	if (!f) {
		note("Couldn't open savefile.");
		return false;
	}

	ok = try_load(f, loaders);
	file_close(f);

	if (player->is_dead && cheat_death) {
			player->is_dead = false;
			player->chp = player->mhp;
			player->noscore |= NOSCORE_WIZARD;
	}

	if (!player->is_dead) {
		/*
		 * Bring the stock curse objects up-to-date with what the
		 * player knows.
		 */
		update_player_object_knowledge(player);

		/*
		 * Reset the dungeon noise to what it was when the game was
		 * saved.  Can skip that if the noise had not been calculated
		 * yet:  the noise levels were all zero when the save happened
		 * and that is what they are after the load calls cave_new().
		 */
		if (character_dungeon && (player->noise_grid.x
				|| player->noise_grid.y)
				&& square_in_bounds(cave, player->noise_grid)) {
			make_noise(player, &player->noise_grid,
				&player->noise_falloff);
		}
	}

	/* Character is now "complete" */
	character_generated = true;
	player->upkeep->playing = true;

	return ok;
}


/**
 * Fill the given buffer with the panic save equivalent for a savefile.
 *
 * \param buf Is the buffer to fill.  The result wil be an empty string if
 * the result can't fit in the given buffer or the savefile name or panic
 * save directory are invalid.
 * \param len Is the maximum number of characters that the buffer can hold.
 * \param path Is the name of the savefile to use.  The storage for that must
 * not overlap buffer.
 */
void savefile_get_panic_name(char *buf, size_t len, const char *path)
{
	size_t name_offset = path_filename_index(path);
	size_t used = path_build(buf, len, ANGBAND_DIR_PANIC,
		path + name_offset);

	if (len > 0 && (used == 0 || !suffix(buf, path + name_offset))) {
		buf[0] = '\0';
	}
}
