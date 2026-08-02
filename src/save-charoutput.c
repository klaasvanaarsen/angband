/**
 * \file save-charoutput.c
 * \brief Write short human-readable character synopsis for angband.live
 *
 * Copyright (c) 2020 Eric Branlund
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

#include "save-charoutput.h"
#include "cave.h"
#include "effects-info.h"
#include "effects.h"
#include "game-world.h"
#include "init.h"
#include "mon-list.h"
#include "mon-lore.h"
#include "mon-predicate.h"
#include "mon-spell.h"
#include "monster.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-info.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player-attack.h"
#include "player-calcs.h"
#include "player-spell.h"
#include "player-timed.h"
#include "player.h"
#include "z-file.h"
#include "z-util.h"

#include <string.h>

/**
Visions:

*/
static bool put_json_string(ang_file *fo, const char *text) {
  const unsigned char *p;

  if (!text)
    text = "";
  if (!file_put(fo, "\""))
    return false;

  p = (const unsigned char *)text;
  while (*p) {
    char tmp[8];

    if (*p == '\\' || *p == '"') {
      tmp[0] = '\\';
      tmp[1] = *p;
      tmp[2] = '\0';
      if (!file_put(fo, tmp))
        return false;
    } else if (*p < 0x20) {
      strnfmt(tmp, sizeof(tmp), "\\u%04x", (unsigned int)*p);
      if (!file_put(fo, tmp))
        return false;
    } else {
      tmp[0] = (char)*p;
      tmp[1] = '\0';
      if (!file_put(fo, tmp))
        return false;
    }

    p++;
  }

  if (!file_put(fo, "\""))
    return false;
  return true;
}

struct danger_profile {
  bool paralyze;
  bool confuse;
  bool blind;
  bool fear;
  bool poison;
  bool acid;
  bool elec;
  bool fire;
  bool cold;
  bool disenchant;
  bool exp_drain;
  bool stat_drain;
  bool hallucinate;
};

struct mitigation_profile {
  bool free_act;
  bool prot_conf;
  bool prot_blind;
  bool prot_fear;
  bool hold_life;
  bool sustain_str;
  bool sustain_int;
  bool sustain_wis;
  bool sustain_dex;
  bool sustain_con;
  bool res_pois;
  bool res_acid;
  bool res_elec;
  bool res_fire;
  bool res_cold;
  bool res_disen;
  bool res_chaos;
};

struct inventory_help_profile {
  bool healing;
  bool escape;
  bool cure_confusion;
  bool cure_blindness;
  bool cure_fear;
  bool cure_poison;
  bool restore_mana;
  bool detect_or_recon;
};

static const char *monster_spell_names[] = {
#define RSF(a, b) #a,
#include "list-mon-spells.h"
#undef RSF
};

static const char *monster_flag_names[] = {
#define RF(a, b, c) #a,
#include "list-mon-race-flags.h"
#undef RF
};

static const char *effect_kind_names[] = {
#define EFFECT(x, a, b, c, d, e, f) #x,
#include "list-effects.h"
#undef EFFECT
};

static int spell_average_damage(const struct class_spell *spell,
                                bool *has_damage);
static double
estimate_best_attack_damage_vs_monster(const struct monster *target);
static bool compute_launcher_state(const struct object *launcher,
                                   struct player_state *state_out);

static bool launcher_matches_ammo(const struct object *launcher,
                                  int ammo_tval) {
  if (!launcher || !launcher->kind)
    return false;

  if (ammo_tval == TV_SHOT)
    return kf_has(launcher->kind->kind_flags, KF_SHOOTS_SHOTS);
  if (ammo_tval == TV_ARROW)
    return kf_has(launcher->kind->kind_flags, KF_SHOOTS_ARROWS);
  if (ammo_tval == TV_BOLT)
    return kf_has(launcher->kind->kind_flags, KF_SHOOTS_BOLTS);

  return false;
}

static bool starts_with(const char *s, const char *prefix) {
  size_t n;

  if (!s || !prefix)
    return false;

  n = strlen(prefix);
  return strncmp(s, prefix, n) == 0;
}

static void put_json_string_array(ang_file *fo, const char *const *items,
                                  const bool *enabled, size_t count) {
  bool first = true;
  size_t i;

  file_put(fo, "[");
  for (i = 0; i < count; i++) {
    if (!enabled[i])
      continue;

    if (!first)
      file_put(fo, ", ");
    (void)put_json_string(fo, items[i]);
    first = false;
  }
  file_put(fo, "]");
}

static void scan_dangerous_attacks(const struct monster_race *race,
                                   struct danger_profile *danger) {
  const struct monster_blow *blow;

  memset(danger, 0, sizeof(*danger));
  if (!race)
    return;

  for (blow = race->blow; blow; blow = blow->next) {
    const char *name =
        (blow->effect && blow->effect->name) ? blow->effect->name : NULL;

    if (!name)
      continue;

    if (streq(name, "PARALYZE"))
      danger->paralyze = true;
    if (streq(name, "CONFUSE"))
      danger->confuse = true;
    if (streq(name, "BLIND"))
      danger->blind = true;
    if (streq(name, "TERRIFY"))
      danger->fear = true;
    if (streq(name, "POISON"))
      danger->poison = true;
    if (streq(name, "ACID"))
      danger->acid = true;
    if (streq(name, "ELEC"))
      danger->elec = true;
    if (streq(name, "FIRE"))
      danger->fire = true;
    if (streq(name, "COLD"))
      danger->cold = true;
    if (streq(name, "DISENCHANT"))
      danger->disenchant = true;
    if (starts_with(name, "EXP_"))
      danger->exp_drain = true;
    if (streq(name, "LOSE_ALL") || starts_with(name, "LOSE_")) {
      danger->stat_drain = true;
    }
    if (streq(name, "HALLU"))
      danger->hallucinate = true;
  }

  if (rsf_has(race->spell_flags, RSF_HOLD))
    danger->paralyze = true;
  if (rsf_has(race->spell_flags, RSF_CONF))
    danger->confuse = true;
  if (rsf_has(race->spell_flags, RSF_BLIND))
    danger->blind = true;
  if (rsf_has(race->spell_flags, RSF_SCARE))
    danger->fear = true;

  if (rsf_has(race->spell_flags, RSF_BO_POIS) ||
      rsf_has(race->spell_flags, RSF_BA_POIS) ||
      rsf_has(race->spell_flags, RSF_BR_POIS)) {
    danger->poison = true;
  }

  if (rsf_has(race->spell_flags, RSF_BO_ACID) ||
      rsf_has(race->spell_flags, RSF_BA_ACID) ||
      rsf_has(race->spell_flags, RSF_BR_ACID)) {
    danger->acid = true;
  }

  if (rsf_has(race->spell_flags, RSF_BO_ELEC) ||
      rsf_has(race->spell_flags, RSF_BA_ELEC) ||
      rsf_has(race->spell_flags, RSF_BR_ELEC) ||
      rsf_has(race->spell_flags, RSF_BE_ELEC)) {
    danger->elec = true;
  }

  if (rsf_has(race->spell_flags, RSF_BO_FIRE) ||
      rsf_has(race->spell_flags, RSF_BA_FIRE) ||
      rsf_has(race->spell_flags, RSF_BR_FIRE)) {
    danger->fire = true;
  }

  if (rsf_has(race->spell_flags, RSF_BO_COLD) ||
      rsf_has(race->spell_flags, RSF_BA_COLD) ||
      rsf_has(race->spell_flags, RSF_BR_COLD)) {
    danger->cold = true;
  }

  if (rsf_has(race->spell_flags, RSF_BR_DISE))
    danger->disenchant = true;

  if (rsf_has(race->spell_flags, RSF_BO_NETH) ||
      rsf_has(race->spell_flags, RSF_BE_NETH) ||
      rsf_has(race->spell_flags, RSF_BR_NETH)) {
    danger->exp_drain = true;
  }

  if (rsf_has(race->spell_flags, RSF_BRAIN_SMASH))
    danger->stat_drain = true;
  if (rsf_has(race->spell_flags, RSF_BR_CHAO))
    danger->hallucinate = true;
}

static void read_player_mitigations(struct mitigation_profile *mitigation) {
  memset(mitigation, 0, sizeof(*mitigation));

  mitigation->free_act = of_has(player->state.flags, OF_FREE_ACT);
  mitigation->prot_conf = of_has(player->state.flags, OF_PROT_CONF);
  mitigation->prot_blind = of_has(player->state.flags, OF_PROT_BLIND);
  mitigation->prot_fear = of_has(player->state.flags, OF_PROT_FEAR);
  mitigation->hold_life = of_has(player->state.flags, OF_HOLD_LIFE);
  mitigation->sustain_str = of_has(player->state.flags, OF_SUST_STR);
  mitigation->sustain_int = of_has(player->state.flags, OF_SUST_INT);
  mitigation->sustain_wis = of_has(player->state.flags, OF_SUST_WIS);
  mitigation->sustain_dex = of_has(player->state.flags, OF_SUST_DEX);
  mitigation->sustain_con = of_has(player->state.flags, OF_SUST_CON);

  mitigation->res_pois = player->state.el_info[ELEM_POIS].res_level > 0;
  mitigation->res_acid = player->state.el_info[ELEM_ACID].res_level > 0;
  mitigation->res_elec = player->state.el_info[ELEM_ELEC].res_level > 0;
  mitigation->res_fire = player->state.el_info[ELEM_FIRE].res_level > 0;
  mitigation->res_cold = player->state.el_info[ELEM_COLD].res_level > 0;
  mitigation->res_disen = player->state.el_info[ELEM_DISEN].res_level > 0;
  mitigation->res_chaos = player->state.el_info[ELEM_CHAOS].res_level > 0;
}

static void classify_item_help(const struct object *obj, const char *name,
                               struct inventory_help_profile *help) {
  memset(help, 0, sizeof(*help));

  if (!obj || !name)
    return;

  if (tval_is_scroll(obj) || tval_is_potion(obj) || tval_is_rod(obj) ||
      tval_is_wand(obj) || tval_is_staff(obj)) {
    if (my_stristr(name, "Cure") || my_stristr(name, "Heal") ||
        my_stristr(name, "Life")) {
      help->healing = true;
    }

    if (my_stristr(name, "Phase Door") || my_stristr(name, "Teleport") ||
        my_stristr(name, "Escape")) {
      help->escape = true;
    }

    if (my_stristr(name, "Confusion")) {
      help->cure_confusion = true;
    }
    if (my_stristr(name, "Blind")) {
      help->cure_blindness = true;
    }
    if (my_stristr(name, "Fear") || my_stristr(name, "Boldness")) {
      help->cure_fear = true;
    }
    if (my_stristr(name, "Poison")) {
      help->cure_poison = true;
    }
    if (my_stristr(name, "Mana")) {
      help->restore_mana = true;
    }
    if (my_stristr(name, "Detect") || my_stristr(name, "Mapping") ||
        my_stristr(name, "Enlightenment") || my_stristr(name, "Probe")) {
      help->detect_or_recon = true;
    }
  }

  if (tval_is_food(obj) || tval_is_mushroom(obj)) {
    if (my_stristr(name, "Cure") || my_stristr(name, "Restore")) {
      help->healing = true;
    }
  }
}

static bool helps_against_top_threat(const struct inventory_help_profile *help,
                                     const struct danger_profile *danger) {
  if (!help || !danger)
    return false;

  if (help->escape)
    return true;
  if (danger->confuse && help->cure_confusion)
    return true;
  if (danger->blind && help->cure_blindness)
    return true;
  if (danger->fear && help->cure_fear)
    return true;
  if (danger->poison && help->cure_poison)
    return true;
  if ((danger->acid || danger->elec || danger->fire || danger->cold ||
       danger->disenchant || danger->exp_drain || danger->stat_drain ||
       danger->hallucinate) &&
      help->healing) {
    return true;
  }

  return false;
}

static bool
estimate_melee_damage_per_player_turn(const struct monster_race *race,
                                      const struct monster_lore *lore,
                                      double *avg_per_turn, int *max_per_turn,
                                      double *avg_actions, int *max_actions) {
  const struct monster_blow *blow;
  double avg_melee_per_action = 0.0;
  int max_melee_per_action = 0;
  double turn_multiplier = 1.0;
  int turn_multiplier_max = 1;
  bool has_known_blow = false;

  if (!race || !lore || !avg_per_turn || !max_per_turn || !avg_actions ||
      !max_actions)
    return false;

  for (blow = lore->blows; blow; blow = blow->next) {
    double avg_hit;
    int max_hit;

    if (blow->times_seen <= 0)
      continue;

    avg_hit = blow->dice.base + blow->dice.dice * (blow->dice.sides + 1) / 2.0 +
              blow->dice.m_bonus;
    max_hit = blow->dice.base + blow->dice.dice * blow->dice.sides +
              blow->dice.m_bonus;
    if (avg_hit < 0)
      avg_hit = 0;
    if (max_hit < 0)
      max_hit = 0;

    avg_melee_per_action += avg_hit;
    max_melee_per_action += max_hit;
    has_known_blow = true;
  }

  if (!has_known_blow)
    return false;

  if (player->state.speed > 0) {
    int p_energy = turn_energy(player->state.speed);
    int m_energy = turn_energy(race->speed);
    if (p_energy > 0) {
      turn_multiplier = (double)m_energy / (double)p_energy;
      turn_multiplier_max = (m_energy + p_energy - 1) / p_energy;
    }
  }
  if (turn_multiplier_max < 1)
    turn_multiplier_max = 1;

  *avg_actions = turn_multiplier;
  *max_actions = turn_multiplier_max;
  *avg_per_turn = avg_melee_per_action * turn_multiplier;
  *max_per_turn = max_melee_per_action * turn_multiplier_max;
  return true;
}

static void write_monster_list(ang_file *fo) {
  monster_list_t *list = monster_list_new();
  bool first_entry = true;
  size_t i;

  file_put(fo, "  monsterList: [");
  if (!list) {
    file_put(fo, "],\n");
    return;
  }

  monster_list_reset(list);
  monster_list_collect(list);
  monster_list_sort(list, monster_list_standard_compare);

  for (i = 0; i < list->entries_size; i++) {
    const monster_list_entry_t *entry = &list->entries[i];
    const struct monster_lore *lore;
    double avg_turn_damage = 0.0;
    int max_turn_damage = 0;
    double avg_actions = 0.0;
    int max_actions = 0;
    bool has_turn_damage;
    int field;
    int count;
    int asleep;

    if (!entry->race)
      continue;

    field = (entry->count[MONSTER_LIST_SECTION_LOS] > 0)
                ? MONSTER_LIST_SECTION_LOS
                : MONSTER_LIST_SECTION_ESP;
    count = entry->count[field];
    asleep = entry->asleep[field];
    if (count <= 0)
      continue;

    lore = get_lore(entry->race);
    has_turn_damage = estimate_melee_damage_per_player_turn(
        entry->race, lore, &avg_turn_damage, &max_turn_damage, &avg_actions,
        &max_actions);

    if (first_entry) {
      file_put(fo, "\n");
    } else {
      file_put(fo, ",\n");
    }

    file_put(fo, "    { name: ");
    (void)put_json_string(fo, entry->race->name);
    file_putf(fo, ", count: %d", count);
    if (asleep > 0)
      file_putf(fo, ", asleepCount: %d", asleep);
    if (field == MONSTER_LIST_SECTION_ESP)
      file_put(fo, ", section: \"ESP\"");
    if (count == 1)
      file_putf(fo, ", offset: { dy: %d, dx: %d }", entry->dy[field],
                entry->dx[field]);
    if (rf_has(entry->race->flags, RF_UNIQUE))
      file_put(fo, ", isUnique: true");
    if (has_turn_damage) {
      file_putf(fo,
                ", meleeDamagePerPlayerTurn: { average: %.2f, max: %d, "
                "averageActions: %.2f, maxActions: %d }",
                avg_turn_damage, max_turn_damage, avg_actions, max_actions);
      if (count > 1) {
        file_putf(fo,
                  ", groupMeleeDamagePerPlayerTurn: { average: %.2f, max: %d, "
                  "count: %d }",
                  avg_turn_damage * count, max_turn_damage * count, count);
      }
    }
    file_put(fo, " }");

    first_entry = false;
  }

  if (!first_entry)
    file_put(fo, "\n");
  file_put(fo, "  ],\n");

  monster_list_free(list);
}

static void write_monster_brief(ang_file *fo, const struct monster *mon,
                                int dist) {
  double ttk = 0.0;
  double best_damage;

  if (!mon || !mon->race) {
    file_put(fo, "null");
    return;
  }

  best_damage = estimate_best_attack_damage_vs_monster(mon);
  if (best_damage > 0.0 && mon->hp > 0)
    ttk = mon->hp / best_damage;

  file_put(fo, "{ ");
  file_put(fo, "name: ");
  (void)put_json_string(fo, mon->race->name);
  file_putf(fo, ", distance: %d", dist);
  file_putf(fo, ", hp: { current: %d, max: %d }", mon->hp, mon->maxhp);
  file_putf(fo, ", speed: %d", mon->mspeed);
  if (mon->m_timed[MON_TMD_SLEEP] > 0)
    file_put(fo, ", asleep: true");
  if (rf_has(mon->race->flags, RF_UNIQUE))
    file_put(fo, ", isUnique: true");
  if (rf_has(mon->race->flags, RF_INVISIBLE))
    file_put(fo, ", isInvisible: true");
  if (rf_has(mon->race->flags, RF_TAKE_ITEM))
    file_put(fo, ", canStealItems: true");
  if (mon->race->freq_spell > 0 || mon->race->freq_innate > 0)
    file_put(fo, ", canCastSpells: true");
  file_putf(fo, ", timeToKill: %.2f", ttk);
  file_put(fo, " }");
}

static void write_monster_recall_object(ang_file *fo,
                                        const struct monster *mon) {
  const struct monster_lore *lore;

  if (!mon || !mon->race) {
    file_put(fo, "null");
    return;
  }

  lore = get_lore(mon->race);
  if (!lore) {
    file_put(fo, "null");
    return;
  }

  {
    bitflag known_flags[RF_SIZE];
    bool known_spell_enabled[RSF_MAX];
    bool known_flag_enabled[RF_MAX];
    const struct monster_blow *blow;
    double avg_turn_damage = 0.0;
    int max_turn_damage = 0;
    double avg_actions = 0.0;
    int max_actions = 0;
    bool has_turn_damage;
    int flag;
    bool first_blow = true;

    memset(known_spell_enabled, 0, sizeof(known_spell_enabled));
    memset(known_flag_enabled, 0, sizeof(known_flag_enabled));

    for (flag = rsf_next(lore->spell_flags, FLAG_START); flag != FLAG_END;
         flag = rsf_next(lore->spell_flags, flag + 1)) {
      if (flag > RSF_NONE && flag < RSF_MAX)
        known_spell_enabled[flag] = true;
    }

    monster_flags_known(mon->race, lore, known_flags);
    for (flag = rf_next(known_flags, FLAG_START); flag != FLAG_END;
         flag = rf_next(known_flags, flag + 1)) {
      if (flag > 0 && flag < RF_MAX)
        known_flag_enabled[flag] = true;
    }

    file_put(fo, "{ ");
    file_putf(fo, "sightings: %u", (unsigned int)lore->sights);
    file_putf(fo, ", deathsByThisRace: %u", (unsigned int)lore->deaths);
    file_putf(fo, ", killsThisLife: %u", (unsigned int)lore->pkills);
    file_putf(fo, ", killsAllLives: %u", (unsigned int)lore->tkills);

    if (lore->armour_known)
      file_putf(fo, ", knownArmorClass: %d", mon->race->ac);
    if (lore->sleep_known)
      file_putf(fo, ", knownSleepiness: %d", mon->race->sleep);
    if (lore->spell_freq_known)
      file_putf(fo, ", knownSpellFreq: %d", mon->race->freq_spell);
    if (lore->innate_freq_known)
      file_putf(fo, ", knownInnateFreq: %d", mon->race->freq_innate);

    file_put(fo, ", knownSpells: ");
    put_json_string_array(fo, monster_spell_names, known_spell_enabled,
                          sizeof(known_spell_enabled) /
                              sizeof(known_spell_enabled[0]));

    file_put(fo, ", knownFlags: ");
    put_json_string_array(fo, monster_flag_names, known_flag_enabled,
                          sizeof(known_flag_enabled) /
                              sizeof(known_flag_enabled[0]));

    has_turn_damage = estimate_melee_damage_per_player_turn(
        mon->race, lore, &avg_turn_damage, &max_turn_damage, &avg_actions,
        &max_actions);

    file_put(fo, ", knownMeleeAttacks: [");
    for (blow = lore->blows; blow; blow = blow->next) {
      double avg_hit;
      int max_hit;

      if (blow->times_seen <= 0)
        continue;

      avg_hit = blow->dice.base +
                blow->dice.dice * (blow->dice.sides + 1) / 2.0 +
                blow->dice.m_bonus;
      max_hit = blow->dice.base + blow->dice.dice * blow->dice.sides +
                blow->dice.m_bonus;
      if (avg_hit < 0)
        avg_hit = 0;
      if (max_hit < 0)
        max_hit = 0;

      if (!first_blow)
        file_put(fo, ", ");

      file_put(fo, "{ method: ");
      (void)put_json_string(fo, blow->method ? blow->method->name : "");
      file_put(fo, ", effect: ");
      (void)put_json_string(fo, blow->effect ? blow->effect->name : "");
      file_putf(fo,
                ", avgDamagePerHit: %.2f, maxDamagePerHit: %d, timesSeen: %d }",
                avg_hit, max_hit, blow->times_seen);
      first_blow = false;
    }
    file_put(fo, "]");
    if (has_turn_damage) {
      file_putf(fo,
                ", meleeDamagePerPlayerTurn: { average: %.2f, max: %d, "
                "averageActions: %.2f, maxActions: %d }",
                avg_turn_damage, max_turn_damage, avg_actions, max_actions);
    }
    file_put(fo, " }");
  }
}

static void write_top_threat_recall(ang_file *fo,
                                    const struct monster *top_threat) {
  file_put(fo, "  topThreatRecall: ");
  write_monster_recall_object(fo, top_threat);
  file_put(fo, ",\n");
}

static int weapon_blows_centiblows(const struct object *weapon) {
  int weapon_slot = slot_by_name(player, "weapon");
  struct object *current_weapon = slot_object(player, weapon_slot);
  struct player_state local_state;
  int old_number = weapon ? weapon->number : 0;

  if (!weapon || weapon_slot < 0)
    return 0;

  if (weapon != current_weapon) {
    ((struct object *)weapon)->number = 1;
    player->body.slots[weapon_slot].obj = (struct object *)weapon;
  }

  local_state.stat_ind[STAT_STR] = 0;
  local_state.stat_ind[STAT_DEX] = 0;
  calc_bonuses(player, &local_state, true, false);

  if (weapon != current_weapon) {
    ((struct object *)weapon)->number = old_number;
    player->body.slots[weapon_slot].obj = current_weapon;
  }

  return local_state.num_blows;
}

static void write_weapon_eval(ang_file *fo, const struct object *weapon,
                              const struct monster *top_threat) {
  int normal_damage = -1;
  int *brand_damage;
  int *slay_damage;
  bool nonweap_slay = false;
  bool has_average_damage = false;
  int blows_centiblows = 0;
  int to_hit_base;

  if (!weapon)
    return;

  brand_damage = mem_zalloc(z_info->brand_max * sizeof(*brand_damage));
  slay_damage = mem_zalloc(z_info->slay_max * sizeof(*slay_damage));

  if (weapon->known && weapon->known->dd > 0 && weapon->known->ds > 0) {
    (void)obj_known_damage(weapon, &normal_damage, brand_damage, slay_damage,
                           &nonweap_slay, false);
    has_average_damage = (normal_damage >= 0);
  }

  blows_centiblows = weapon_blows_centiblows(weapon);
  to_hit_base = chance_of_melee_hit_base(player, weapon);

  file_put(fo, ", weaponEval: { ");
  if (has_average_damage) {
    file_putf(fo, "avgDamagePerRound: %.1f", normal_damage / 10.0);
  } else {
    file_put(fo, "avgDamagePerRound: null");
  }

  file_putf(fo, ", blowsPerRound: %.2f", blows_centiblows / 100.0);

  if (has_average_damage && blows_centiblows > 0) {
    double avg_per_hit = (normal_damage * 10.0) / blows_centiblows;
    file_putf(fo, ", avgDamagePerHit: %.2f", avg_per_hit);
  } else {
    file_put(fo, ", avgDamagePerHit: null");
  }

  file_putf(fo, ", toHitBase: %d", to_hit_base);

  if (top_threat && top_threat->race) {
    random_chance hit;
    int pct;
    double avg_vs_top = 0.0;

    hit_chance(&hit, to_hit_base, top_threat->race->ac);
    pct = random_chance_scaled(hit, 100);
    if (has_average_damage)
      avg_vs_top = (normal_damage / 10.0) * (pct / 100.0);
    file_putf(fo, ", avgDamagePerRoundVsTopThreat: %.2f", avg_vs_top);
    file_putf(fo, ", topThreatAC: %d", top_threat->race->ac);
  } else {
    file_put(fo, ", avgDamagePerRoundVsTopThreat: null");
  }

  file_put(fo, " }");

  mem_free(slay_damage);
  mem_free(brand_damage);
}

static int effect_min_damage(const struct effect *effect,
                             const dice_t *shared_dice) {
  if (!effect)
    return 0;

  if (effect->index == EF_RANDOM || effect->index == EF_SELECT) {
    const struct effect *e = effect->next;
    int n = dice_evaluate(shared_dice ? shared_dice : effect->dice, 0, AVERAGE,
                          NULL);
    int i;
    int best = -1;

    for (i = 0; e && i < n; i++, e = e->next) {
      int d = effect_min_damage(e, shared_dice);
      if (best < 0 || d < best)
        best = d;
    }

    return best < 0 ? 0 : best;
  }

  if (!effect_damages(effect))
    return 0;

  return dice_evaluate(shared_dice ? shared_dice : effect->dice, 0, MINIMISE,
                       NULL);
}

static int spell_min_damage(const struct class_spell *spell) {
  const struct effect *effect;
  const dice_t *shared_dice = NULL;
  int total = 0;

  if (!spell)
    return 0;

  for (effect = spell->effect; effect;
       effect = effect_next((struct effect *)effect)) {
    if (effect->index == EF_SET_VALUE) {
      shared_dice = effect->dice;
      continue;
    }
    if (effect->index == EF_CLEAR_VALUE) {
      shared_dice = NULL;
      continue;
    }
    if (!effect_damages(effect))
      continue;

    total += effect_min_damage(effect, shared_dice);
  }

  return total;
}

static int max_circle_squares(int radius) {
  int dy, dx;
  int n = 0;

  if (radius <= 0)
    return 1;

  for (dy = -radius; dy <= radius; dy++) {
    for (dx = -radius; dx <= radius; dx++) {
      if (dx * dx + dy * dy <= radius * radius)
        n++;
    }
  }

  return n;
}

static int estimate_max_aoe_squares(const struct effect *effect,
                                    const char *shape) {
  int default_line_range =
      (z_info && z_info->max_range > 0) ? z_info->max_range : 1;

  if (!effect || !shape)
    return 1;

  if (streq(shape, "ball"))
    return max_circle_squares(MAX(1, effect->radius));

  if (streq(shape, "cone")) {
    int range = MAX(1, effect->y);
    int arc = effect->other > 0 ? effect->other : 60;
    int full = max_circle_squares(range);
    return MAX(1, (full * arc + 359) / 360);
  }

  if (streq(shape, "beam") || streq(shape, "lineOfSight") ||
      streq(shape, "bolt"))
    return MAX(1, effect->y > 0 ? effect->y : default_line_range);

  if (streq(shape, "adjacentArea"))
    return 8;

  return 1;
}

struct attack_action_summary {
  char name[128];
  double expected_damage;
  double min_damage;
  double time_to_kill;
  int sp_cost;
  double expected_damage_per_sp;
};

static double damage_multiplier_for_target(const struct monster *target,
                                           const char *projection) {
  if (!target || !target->race || !projection || !projection[0])
    return 1.0;

  if (streq(projection, "acid")) {
    if (rf_has(target->race->flags, RF_IM_ACID))
      return 0.0;
    return 1.0;
  }

  if (streq(projection, "lightning") || streq(projection, "elec")) {
    if (rf_has(target->race->flags, RF_IM_ELEC))
      return 0.0;
    return 1.0;
  }

  if (streq(projection, "fire")) {
    if (rf_has(target->race->flags, RF_IM_FIRE))
      return 0.0;
    if (rf_has(target->race->flags, RF_HURT_FIRE))
      return 2.0;
    return 1.0;
  }

  if (streq(projection, "cold") || streq(projection, "frost")) {
    if (rf_has(target->race->flags, RF_IM_COLD))
      return 0.0;
    if (rf_has(target->race->flags, RF_HURT_COLD))
      return 2.0;
    return 1.0;
  }

  if (streq(projection, "poison")) {
    if (rf_has(target->race->flags, RF_IM_POIS))
      return 0.0;
    return 1.0;
  }

  if (streq(projection, "light")) {
    if (rf_has(target->race->flags, RF_HURT_LIGHT))
      return 2.0;
    return 0.0;
  }

  return 1.0;
}

static int top_threat_hit_percent(int to_hit,
                                  const struct monster *top_threat) {
  random_chance hit;

  if (!top_threat || !top_threat->race)
    return 100;

  hit_chance(&hit, to_hit, top_threat->race->ac);
  return random_chance_scaled(hit, 100);
}

static void add_attack_action(struct attack_action_summary *actions, int *count,
                              int max_count, const char *name,
                              double expected_damage, double min_damage) {
  if (!actions || !count || !name || *count >= max_count)
    return;

  my_strcpy(actions[*count].name, name, sizeof(actions[*count].name));
  actions[*count].expected_damage = expected_damage;
  actions[*count].min_damage = min_damage;
  actions[*count].time_to_kill = 0.0;
  actions[*count].sp_cost = 0;
  actions[*count].expected_damage_per_sp = 0.0;
  (*count)++;
}

static void collect_attack_actions(struct attack_action_summary *actions,
                                   int *action_count, int max_count,
                                   const struct monster *target) {
  const struct object *obj;

  if (!actions || !action_count)
    return;

  *action_count = 0;

  for (obj = player->gear; obj; obj = obj->next) {
    if (tval_is_melee_weapon(obj)) {
      int normal_damage = -1;
      int *brand_damage = mem_zalloc(z_info->brand_max * sizeof(*brand_damage));
      int *slay_damage = mem_zalloc(z_info->slay_max * sizeof(*slay_damage));
      bool nonweap_slay = false;
      bool has_brands_or_slays = obj_known_damage(
          obj, &normal_damage, brand_damage, slay_damage, &nonweap_slay, false);

      if (obj->known && obj->known->dd > 0 && obj->known->ds > 0 &&
          normal_damage >= 0) {
        int to_hit = chance_of_melee_hit_base(player, obj);
        int hit_pct = top_threat_hit_percent(to_hit, target);
        int blows_centiblows = weapon_blows_centiblows(obj);
        int min_per_hit =
            obj->known->dd + object_to_dam(obj->known) + player->state.to_d;
        double min_per_round;
        double expected;
        char action_name[180];

        if (min_per_hit < 0)
          min_per_hit = 0;
        min_per_round =
            (blows_centiblows / 100.0) * min_per_hit * (hit_pct / 100.0);
        expected = (normal_damage / 10.0) * (hit_pct / 100.0);

        object_desc(action_name, sizeof(action_name), obj,
                    ODESC_PREFIX | ODESC_FULL, player);
        if (normal_damage >= 0 && blows_centiblows > 0 && min_per_hit > 0) {
          add_attack_action(actions, action_count, max_count, action_name,
                            expected, min_per_round);
        }
      }

      mem_free(slay_damage);
      mem_free(brand_damage);
    }

    if (tval_is_ammo(obj)) {
      const struct object *best_launcher = NULL;
      struct player_state best_state;
      int best_to_hit = 0;
      double best_avg_shot = 0.0;
      double best_min_shot = 0.0;
      double best_avg_round_vs_target = -1.0;
      const struct object *launch;
      int ammo_dd =
          (obj->known && obj->known->dd > 0) ? obj->known->dd : obj->dd;
      int ammo_ds =
          (obj->known && obj->known->ds > 0) ? obj->known->ds : obj->ds;

      if (ammo_dd > 0 && ammo_ds > 0) {
        for (launch = player->gear; launch; launch = launch->next) {
          struct player_state local_state;
          int to_hit;
          int hit_pct;
          double avg_shot;
          double avg_round_vs_target;
          double min_shot;

          if (!tval_is_launcher(launch))
            continue;
          if (!launcher_matches_ammo(launch, obj->tval))
            continue;
          if (!compute_launcher_state(launch, &local_state))
            continue;

          to_hit =
              local_state.skills[SKILL_TO_HIT_BOW] +
              (object_to_hit(obj) + local_state.to_h + object_to_hit(launch)) *
                  BTH_PLUS_ADJ;
          hit_pct = top_threat_hit_percent(to_hit, target);

          avg_shot = ammo_dd * (ammo_ds + 1) / 2.0 + object_to_dam(obj) +
                     object_to_dam(launch);
          min_shot = ammo_dd + object_to_dam(obj) + object_to_dam(launch);
          avg_shot *= MAX(1, local_state.ammo_mult);
          min_shot *= MAX(1, local_state.ammo_mult);
          if (avg_shot < 0)
            avg_shot = 0;
          if (min_shot < 0)
            min_shot = 0;

          avg_round_vs_target =
              avg_shot * (local_state.num_shots / 10.0) * (hit_pct / 100.0);
          if (avg_round_vs_target > best_avg_round_vs_target) {
            best_avg_round_vs_target = avg_round_vs_target;
            best_launcher = launch;
            best_state = local_state;
            best_to_hit = to_hit;
            best_avg_shot = avg_shot;
            best_min_shot = min_shot;
          }
        }

        if (best_launcher) {
          int hit_pct = top_threat_hit_percent(best_to_hit, target);
          double min_round =
              best_min_shot * (best_state.num_shots / 10.0) * (hit_pct / 100.0);
          char ammo_name[180];
          char launcher_name[180];
          char action_name[300];

          object_desc(ammo_name, sizeof(ammo_name), obj,
                      ODESC_PREFIX | ODESC_FULL, player);
          object_desc(launcher_name, sizeof(launcher_name), best_launcher,
                      ODESC_PREFIX | ODESC_FULL, player);
          strnfmt(action_name, sizeof(action_name), "%s with %s", ammo_name,
                  launcher_name);

          if (best_avg_round_vs_target > 0.0 || min_round > 0.0) {
            add_attack_action(actions, action_count, max_count, action_name,
                              best_avg_round_vs_target, min_round);
          }
        }
      }
    }

    {
      struct effect *chain = object_effect(obj);
      struct effect *effect;
      const dice_t *shared_dice = NULL;
      double avg_total = 0.0;
      double min_total = 0.0;
      bool has_damage = false;

      if (!chain)
        continue;

      for (effect = chain; effect; effect = effect_next(effect)) {
        const char *projection;

        if (effect->index == EF_SET_VALUE) {
          shared_dice = effect->dice;
          continue;
        }
        if (effect->index == EF_CLEAR_VALUE) {
          shared_dice = NULL;
          continue;
        }
        if (!effect_damages(effect))
          continue;

        projection = effect_projection(effect);
        avg_total += effect_avg_damage(effect, shared_dice) *
                     damage_multiplier_for_target(target, projection);
        min_total += effect_min_damage(effect, shared_dice) *
                     damage_multiplier_for_target(target, projection);
        has_damage = true;
      }

      if (has_damage && (avg_total > 0.0 || min_total > 0.0)) {
        char item_name[180];
        object_desc(item_name, sizeof(item_name), obj,
                    ODESC_PREFIX | ODESC_FULL, player);
        add_attack_action(actions, action_count, max_count, item_name,
                          avg_total, min_total);
      }
    }
  }

  for (obj = player->gear; obj; obj = obj->next) {
    const struct class_book *book = player_object_to_book(player, obj);
    int bi;

    if (!book)
      continue;

    for (bi = 0; bi < book->num_spells; bi++) {
      int sidx = book->spells[bi].sidx;
      const struct class_spell *spell;
      int fail;
      bool has_damage = false;
      double avg = 0.0;
      double min = 0.0;
      const dice_t *shared_dice = NULL;
      struct effect *effect;
      char action_name[160];

      spell = spell_by_index(player, sidx);
      if (!spell)
        continue;
      if (!(player->spell_flags[sidx] & PY_SPELL_LEARNED))
        continue;
      if (player->spell_flags[sidx] & PY_SPELL_FORGOTTEN)
        continue;
      if (spell->smana > player->csp)
        continue;

      for (effect = spell->effect; effect; effect = effect_next(effect)) {
        const char *projection;

        if (effect->index == EF_SET_VALUE) {
          shared_dice = effect->dice;
          continue;
        }
        if (effect->index == EF_CLEAR_VALUE) {
          shared_dice = NULL;
          continue;
        }
        if (!effect_damages(effect))
          continue;

        projection = effect_projection(effect);
        avg += effect_avg_damage(effect, shared_dice) *
               damage_multiplier_for_target(target, projection);
        min += effect_min_damage(effect, shared_dice) *
               damage_multiplier_for_target(target, projection);
        has_damage = true;
      }
      if (!has_damage || (avg <= 0.0 && min <= 0.0))
        continue;

      fail = spell_chance(sidx);
      strnfmt(action_name, sizeof(action_name), "Spell: %s", spell->name);
      add_attack_action(actions, action_count, max_count, action_name,
                        avg * (100 - fail) / 100.0, min * (100 - fail) / 100.0);
      if (*action_count > 0 && spell->smana > 0) {
        actions[*action_count - 1].sp_cost = spell->smana;
        actions[*action_count - 1].expected_damage_per_sp =
            actions[*action_count - 1].expected_damage / spell->smana;
      }
    }
  }

  if (*action_count > 0) {
    int i;
    for (i = 0; i < *action_count; i++) {
      if (actions[i].expected_damage > 0.0 && target && target->hp > 0) {
        actions[i].time_to_kill = target->hp / actions[i].expected_damage;
      } else {
        actions[i].time_to_kill = 0.0;
      }
    }

    for (i = 0; i < *action_count; i++) {
      int j;
      int best = i;
      for (j = i + 1; j < *action_count; j++) {
        if (actions[j].expected_damage > actions[best].expected_damage)
          best = j;
      }
      if (best != i) {
        struct attack_action_summary t = actions[i];
        actions[i] = actions[best];
        actions[best] = t;
      }
    }
  }
}

static double
estimate_best_attack_damage_vs_monster(const struct monster *target) {
  struct attack_action_summary actions[64];
  int action_count = 0;

  if (!target)
    return 0.0;

  collect_attack_actions(actions, &action_count, 64, target);
  return action_count > 0 ? actions[0].expected_damage : 0.0;
}

static void write_best_attack_actions(ang_file *fo,
                                      const struct monster *top_threat) {
  struct attack_action_summary actions[64];
  int action_count = 0;
  int i;

  file_put(fo, "  bestAttackActions: [");

  collect_attack_actions(actions, &action_count, 64, top_threat);

  if (action_count > 0) {
    int limit = MIN(action_count, 8);
    for (i = 0; i < limit; i++) {
      if (i == 0)
        file_put(fo, "\n");
      else
        file_put(fo, ",\n");
      file_put(fo, "    { action: ");
      (void)put_json_string(fo, actions[i].name);
      file_putf(fo, ", expectedDamageVsTopThreat: %.2f",
                actions[i].expected_damage);
      file_putf(fo, ", minDamageVsTopThreat: %.2f", actions[i].min_damage);
      file_putf(fo, ", timeToKillVsTopThreat: %.2f", actions[i].time_to_kill);
      if (actions[i].sp_cost > 0) {
        file_putf(fo, ", spCost: %d", actions[i].sp_cost);
        file_putf(fo, ", expectedDamagePerSp: %.2f",
                  actions[i].expected_damage_per_sp);
      }
      file_put(fo, " }");
    }
    file_put(fo, "\n");
  }

  file_put(fo, "  ],\n");
}

static bool compute_launcher_state(const struct object *launcher,
                                   struct player_state *state_out) {
  int shooting_slot = slot_by_name(player, "shooting");
  struct object *current_launcher = slot_object(player, shooting_slot);
  int old_number = launcher ? launcher->number : 0;

  if (!launcher || shooting_slot < 0 || !state_out)
    return false;

  if (launcher != current_launcher) {
    ((struct object *)launcher)->number = 1;
    player->body.slots[shooting_slot].obj = (struct object *)launcher;
  }

  state_out->stat_ind[STAT_STR] = 0;
  state_out->stat_ind[STAT_DEX] = 0;
  calc_bonuses(player, state_out, true, false);

  if (launcher != current_launcher) {
    ((struct object *)launcher)->number = old_number;
    player->body.slots[shooting_slot].obj = current_launcher;
  }

  return true;
}

static void write_missile_eval(ang_file *fo, const struct object *ammo,
                               const struct monster *top_threat) {
  const struct object *best_launcher = NULL;
  struct player_state best_state;
  int best_to_hit = 0;
  double best_avg_shot = 0.0;
  double best_avg_round = 0.0;
  double best_avg_round_vs_top = 0.0;
  bool found = false;
  const struct object *obj;
  int ammo_dd;
  int ammo_ds;

  if (!ammo || !tval_is_ammo(ammo))
    return;

  ammo_dd = ammo->known && ammo->known->dd > 0 ? ammo->known->dd : ammo->dd;
  ammo_ds = ammo->known && ammo->known->ds > 0 ? ammo->known->ds : ammo->ds;
  if (ammo_dd <= 0 || ammo_ds <= 0)
    return;

  for (obj = player->gear; obj; obj = obj->next) {
    struct player_state local_state;
    int to_hit;
    int hit_pct = 0;
    double per_hit;
    double per_round;
    double per_round_vs_top;

    if (!tval_is_launcher(obj))
      continue;
    if (!launcher_matches_ammo(obj, ammo->tval))
      continue;
    if (!compute_launcher_state(obj, &local_state))
      continue;

    to_hit = local_state.skills[SKILL_TO_HIT_BOW] +
             (object_to_hit(ammo) + local_state.to_h + object_to_hit(obj)) *
                 BTH_PLUS_ADJ;

    per_hit = ammo_dd * (ammo_ds + 1) / 2.0;
    per_hit += object_to_dam(ammo);
    per_hit += object_to_dam(obj);
    per_hit *= MAX(1, local_state.ammo_mult);
    if (per_hit < 0)
      per_hit = 0;

    if (top_threat && top_threat->race) {
      random_chance hit;
      hit_chance(&hit, to_hit, top_threat->race->ac);
      hit_pct = random_chance_scaled(hit, 100);
    }

    per_round = per_hit * (local_state.num_shots / 10.0);
    per_round_vs_top = top_threat && top_threat->race
                           ? per_round * (hit_pct / 100.0)
                           : per_round;

    if (!found || per_round_vs_top > best_avg_round_vs_top) {
      found = true;
      best_launcher = obj;
      best_state = local_state;
      best_to_hit = to_hit;
      best_avg_shot = per_hit;
      best_avg_round = per_round;
      best_avg_round_vs_top = per_round_vs_top;
    }
  }

  if (!found)
    return;

  file_put(fo, ", missileEval: { bestLauncher: ");
  {
    char launcher_name[200];
    object_desc(launcher_name, sizeof(launcher_name), best_launcher,
                ODESC_PREFIX | ODESC_FULL, player);
    (void)put_json_string(fo, launcher_name);
  }
  file_putf(fo, ", shotsPerRound: %.2f", best_state.num_shots / 10.0);
  file_putf(fo, ", toHitWithBestLauncher: %d", best_to_hit);
  file_putf(fo, ", avgDamagePerShotWithBestLauncher: %.2f", best_avg_shot);
  file_putf(fo, ", avgDamagePerRoundWithBestLauncher: %.2f", best_avg_round);
  if (top_threat && top_threat->race) {
    random_chance hit;
    int hit_pct;
    hit_chance(&hit, best_to_hit, top_threat->race->ac);
    hit_pct = random_chance_scaled(hit, 100);
    file_putf(fo, ", hitChanceVsTopThreatVisible: %d", hit_pct);
    file_putf(fo, ", avgDamagePerRoundVsTopThreat: %.2f",
              best_avg_round_vs_top);
  }
  file_put(fo, " }");
}

static int spell_average_damage(const struct class_spell *spell,
                                bool *has_damage) {
  const struct effect *effect;
  const dice_t *shared_dice = NULL;
  int total = 0;

  *has_damage = false;
  if (!spell)
    return 0;

  for (effect = spell->effect; effect;
       effect = effect_next((struct effect *)effect)) {
    if (effect->index == EF_SET_VALUE) {
      shared_dice = effect->dice;
      continue;
    }
    if (effect->index == EF_CLEAR_VALUE) {
      shared_dice = NULL;
      continue;
    }
    if (!effect_damages(effect))
      continue;

    total += effect_avg_damage(effect, shared_dice);
    *has_damage = true;
  }

  return total;
}

static const char *effect_area_shape(const struct effect *effect) {
  if (!effect)
    return NULL;

  switch (effect->index) {
  case EF_BALL:
  case EF_SPHERE:
  case EF_SPOT:
  case EF_SWARM:
  case EF_STRIKE:
  case EF_STAR_BALL:
    return "ball";

  case EF_BREATH:
  case EF_ARC:
    return "cone";

  case EF_BEAM:
  case EF_SHORT_BEAM:
  case EF_LINE:
  case EF_LASH:
    return "beam";

  case EF_BOLT:
  case EF_BOLT_OR_BEAM:
  case EF_BOLT_STATUS:
  case EF_BOLT_STATUS_DAM:
  case EF_BOLT_AWARE:
    return "bolt";

  case EF_PROJECT_LOS:
  case EF_PROJECT_LOS_AWARE:
  case EF_STAR:
    return "lineOfSight";

  case EF_TOUCH:
  case EF_TOUCH_AWARE:
    return "adjacentArea";

  default:
    return NULL;
  }
}

static void write_item_threat_effects(ang_file *fo, const struct object *obj) {
  struct effect *chain;
  struct effect *effect;
  const dice_t *shared_dice = NULL;
  bool first_effect = true;
  bool has_damage = false;
  int total_avg_damage = 0;

  if (!obj)
    return;

  chain = object_effect(obj);
  if (!chain)
    return;

  file_put(fo, ", threatEffects: { ");
  file_putf(fo, "requiresAim: %s", obj_needs_aim(obj) ? "true" : "false");
  file_put(fo, ", effects: [");

  for (effect = chain; effect; effect = effect_next(effect)) {
    const char *area_shape;
    const char *attack_type;
    char menu_name[160];
    bool this_damages;

    if (effect->index == EF_SET_VALUE) {
      shared_dice = effect->dice;
      continue;
    }
    if (effect->index == EF_CLEAR_VALUE) {
      shared_dice = NULL;
      continue;
    }

    this_damages = effect_damages(effect);
    area_shape = effect_area_shape(effect);
    attack_type = effect_projection(effect);

    if (first_effect) {
      file_put(fo, " ");
    } else {
      file_put(fo, ", ");
    }

    file_put(fo, "{ kind: ");
    if (effect->index >= 0 && effect->index < EF_MAX) {
      (void)put_json_string(fo, effect_kind_names[effect->index]);
    } else {
      (void)put_json_string(fo, "UNKNOWN");
    }

    if (effect_get_menu_name(menu_name, sizeof(menu_name), effect) > 0) {
      file_put(fo, ", summary: ");
      (void)put_json_string(fo, menu_name);
    }

    if (this_damages) {
      int avg = effect_avg_damage(effect, shared_dice);

      has_damage = true;
      total_avg_damage += avg;
      file_putf(fo, ", avgDamage: %d", avg);

      if (attack_type && attack_type[0]) {
        file_put(fo, ", attackType: ");
        (void)put_json_string(fo, attack_type);
      }
    }

    if (area_shape) {
      file_put(fo, ", area: { shape: ");
      (void)put_json_string(fo, area_shape);
      if (effect->radius > 0)
        file_putf(fo, ", radius: %d", effect->radius);
      if (effect->y > 0)
        file_putf(fo, ", maxRange: %d", effect->y);
      if (streq(area_shape, "cone") && effect->other > 0)
        file_putf(fo, ", arcDegrees: %d", effect->other);
      file_put(fo, " }");
    }

    file_put(fo, " }");
    first_effect = false;
  }

  if (first_effect) {
    file_put(fo, "]");
  } else {
    file_put(fo, " ]");
  }

  if (has_damage)
    file_putf(fo, ", totalAvgDamage: %d", total_avg_damage);

  file_put(fo, " }");
}

static char tactical_grid_cell(struct loc grid) {
  struct monster *mon;

  if (loc_eq(grid, player->grid))
    return '@';
  if (!square_in_bounds(cave, grid))
    return ' ';
  if (!square_isknown(cave, grid))
    return '?';

  mon = square_monster(cave, grid);
  if (mon && mon->hp > 0) {
    if (monster_is_visible(mon))
      return 'M';
    return 'm';
  }

  if (square_isupstairs(cave, grid))
    return '<';
  if (square_isdownstairs(cave, grid))
    return '>';
  if (square_isvisibletrap(cave, grid))
    return '^';
  if (square_iscloseddoor(cave, grid))
    return '+';
  if (square_isopendoor(cave, grid))
    return '\'';
  if (square_isrubble(cave, grid))
    return '*';
  if (square_seemslikewall(cave, grid) || square_iswall_inner(cave, grid) ||
      square_iswall_outer(cave, grid) || square_iswall_solid(cave, grid)) {
    return '#';
  }
  if (square_ispassable(cave, grid))
    return '.';

  return '?';
}

static void write_tactical_grid(ang_file *fo, int radius) {
  const int width = radius * 2 + 1;
  bool first_row = true;
  int y;

  file_put(fo, "  tacticalGrid: {\n");
  file_putf(fo, "    radius: %d,\n", radius);
  file_put(fo, "    legend: { \"@\": \"player\", \"M\": \"visibleMonster\", "
               "\"m\": \"knownMonster\", \"#\": \"wall\", \".\": \"passable\", "
               "\"+\": \"closedDoor\", \"'\": \"openDoor\", \"<\": "
               "\"upstairs\", \">\": \"downstairs\", \"^\": \"visibleTrap\", "
               "\"*\": \"rubble\", \"?\": \"unknown\" },\n");
  file_put(fo, "    rows: [");

  for (y = player->grid.y - radius; y <= player->grid.y + radius; y++) {
    int x;
    char *row = mem_zalloc((size_t)width + 1);

    for (x = player->grid.x - radius; x <= player->grid.x + radius; x++) {
      struct loc g = loc(x, y);
      row[x - (player->grid.x - radius)] = tactical_grid_cell(g);
    }

    if (first_row) {
      file_put(fo, "\n");
    } else {
      file_put(fo, ",\n");
    }
    file_put(fo, "      ");
    (void)put_json_string(fo, row);
    first_row = false;

    mem_free(row);
  }

  if (!first_row)
    file_put(fo, "\n");
  file_put(fo, "    ]\n");
  file_put(fo, "  },\n");
}

static void write_available_spells(ang_file *fo) {
  bool first = true;
  bool *seen;
  const struct object *obj;

  file_put(fo, "  availableSpells: [");
  if (!player || player->class->magic.total_spells <= 0) {
    file_put(fo, "],\n");
    return;
  }

  seen = mem_zalloc((size_t)player->class->magic.total_spells * sizeof(*seen));

  for (obj = player->gear; obj; obj = obj->next) {
    const struct class_book *book = player_object_to_book(player, obj);
    int i;

    if (!book)
      continue;

    for (i = 0; i < book->num_spells; i++) {
      int sidx = book->spells[i].sidx;
      const struct class_spell *spell;
      bool learned;
      bool forgotten;
      bool castable_now;
      bool has_damage;
      int avg_damage;
      int min_damage = 0;
      const char *attack_types[16];
      int attack_type_count = 0;
      const struct effect *effect;

      if (sidx < 0 || sidx >= player->class->magic.total_spells)
        continue;
      if (seen[sidx])
        continue;
      seen[sidx] = true;

      spell = spell_by_index(player, sidx);
      if (!spell)
        continue;

      learned = (player->spell_flags[sidx] & PY_SPELL_LEARNED) != 0;
      forgotten = (player->spell_flags[sidx] & PY_SPELL_FORGOTTEN) != 0;
      castable_now = learned && !forgotten && spell->smana <= player->csp;
      avg_damage = spell_average_damage(spell, &has_damage);

      for (effect = spell->effect; effect;
           effect = effect_next((struct effect *)effect)) {
        const char *projection;
        int j;
        bool exists = false;

        if (effect->index == EF_SET_VALUE || effect->index == EF_CLEAR_VALUE)
          continue;
        if (!effect_damages(effect))
          continue;

        min_damage += effect_min_damage(effect, NULL);

        projection = effect_projection(effect);
        if (!projection || !projection[0])
          continue;

        for (j = 0; j < attack_type_count; j++) {
          if (streq(attack_types[j], projection)) {
            exists = true;
            break;
          }
        }
        if (!exists && attack_type_count < (int)(sizeof(attack_types) /
                                                 sizeof(attack_types[0])))
          attack_types[attack_type_count++] = projection;
      }

      if (first) {
        file_put(fo, "\n");
      } else {
        file_put(fo, ",\n");
      }

      file_put(fo, "    { name: ");
      (void)put_json_string(fo, spell->name);
      file_putf(fo, ", manaCost: %d", spell->smana);
      file_putf(fo, ", failChance: %d", spell_chance(sidx));
      if (forgotten)
        file_put(fo, ", forgotten: true");
      file_putf(fo, ", castableNow: %s", castable_now ? "true" : "false");
      if (has_damage) {
        file_putf(fo, ", avgDamagePerTarget: %d", avg_damage);
        file_putf(fo, ", minDamagePerTarget: %d", min_damage);
        file_put(fo, ", attackTypes: ");
        if (attack_type_count > 0) {
          int j;

          file_put(fo, "[");
          for (j = 0; j < attack_type_count; j++) {
            if (j > 0)
              file_put(fo, ", ");
            (void)put_json_string(fo, attack_types[j]);
          }
          file_put(fo, "]");
        } else {
          file_put(fo, "[\"untyped\"]");
        }

        file_put(fo, ", areasOfEffect: [");
        {
          bool first_area = true;

          for (effect = spell->effect; effect;
               effect = effect_next((struct effect *)effect)) {
            const char *shape;

            if (effect->index == EF_SET_VALUE ||
                effect->index == EF_CLEAR_VALUE)
              continue;
            if (!effect_damages(effect))
              continue;

            shape = effect_area_shape(effect);
            if (!shape)
              shape = "singleTarget";

            if (!first_area)
              file_put(fo, ", ");
            file_put(fo, "{ shape: ");
            (void)put_json_string(fo, shape);
            if (effect->radius > 0)
              file_putf(fo, ", radius: %d", effect->radius);
            if (effect->y > 0)
              file_putf(fo, ", maxRange: %d", effect->y);
            if (streq(shape, "cone") && effect->other > 0)
              file_putf(fo, ", arcDegrees: %d", effect->other);
            file_putf(fo, ", maxSquaresAffected: %d",
                      estimate_max_aoe_squares(effect, shape));
            file_put(fo, " }");
            first_area = false;
          }
        }
        file_put(fo, "]");
      }
      file_put(fo, " }");

      first = false;
    }
  }

  if (!first)
    file_put(fo, "\n");
  file_put(fo, "  ],\n");

  mem_free(seen);
}

static int nearest_stair_distance(bool upstairs) {
  int best = -1;
  int y, x;

  for (y = 0; y < cave->height; y++) {
    for (x = 0; x < cave->width; x++) {
      struct loc grid = loc(x, y);
      int d;

      if (upstairs) {
        if (!square_isupstairs(cave, grid))
          continue;
      } else {
        if (!square_isdownstairs(cave, grid))
          continue;
      }

      d = distance(player->grid, grid);
      if (best < 0 || d < best) {
        best = d;
      }
    }
  }

  return best;
}

static const char *choose_action_hint(int nearest_upstairs,
                                      int nearest_downstairs,
                                      const struct monster *top_threat,
                                      int threat_dist) {
  if (top_threat && threat_dist <= 1) {
    if (top_threat->mspeed > player->state.speed && nearest_upstairs >= 0 &&
        nearest_upstairs <= 6) {
      return "Immediate retreat to upstairs is recommended.";
    }

    if (player->chp <= player->mhp / 2) {
      return "Disengage now; avoid trading melee while at low HP.";
    }

    return "Adjacent threat: prefer burst damage or controlled retreat.";
  }

  if (top_threat && top_threat->mspeed > player->state.speed &&
      threat_dist <= 4 && nearest_upstairs >= 0 && nearest_upstairs <= 8) {
    return "Faster visible threat nearby; keep a path to upstairs open.";
  }

  if (nearest_downstairs >= 0 && nearest_upstairs >= 0 &&
      nearest_upstairs <= nearest_downstairs) {
    return "You are positioned to bail out quickly if a fight turns bad.";
  }

  return "No immediate red flag detected; advance cautiously and preserve "
         "escape routes.";
}

/* Based on the adaptation of Exo's patch to frogcomposband. */
bool save_charoutput(void) {
  char path[1024];
  ang_file *fo;
  struct monster *top_threat = NULL;
  struct monster *subwindow_violet_top = NULL;
  struct monster *subwindow_red_top = NULL;
  struct danger_profile danger;
  struct mitigation_profile mitigation;
  int i;
  int visible_count = 0;
  int adjacent_count = 0;
  int faster_count = 0;
  int unique_visible_count = 0;
  int spellcaster_visible_count = 0;
  int steal_visible_count = 0;
  int invisible_visible_count = 0;
  int top_threat_score = -100000;
  int top_threat_dist = -1;
  int subwindow_violet_dist = -1;
  int subwindow_red_dist = -1;
  int subwindow_violet_level = -1;
  int subwindow_red_level = -1;
  int subwindow_violet_score = -100000;
  int subwindow_red_score = -100000;
  int nearest_upstairs;
  int nearest_downstairs;
  bool first_inventory;

  path_build(path, sizeof(path), ANGBAND_DIR_USER, "CharOutput.txt");
  fo = file_open(path, MODE_WRITE, FTYPE_TEXT);
  if (!fo)
    return false;

  read_player_mitigations(&mitigation);
  memset(&danger, 0, sizeof(danger));

  for (i = 1; i < cave_monster_max(cave); i++) {
    struct monster *mon = cave_monster(cave, i);
    int dist;
    int score;

    if (!mon || !mon->race)
      continue;
    if (mon->hp <= 0)
      continue;
    if (!monster_is_visible(mon))
      continue;

    dist = distance(player->grid, mon->grid);
    visible_count++;
    if (dist <= 1)
      adjacent_count++;
    if (mon->mspeed > player->state.speed)
      faster_count++;
    if (rf_has(mon->race->flags, RF_UNIQUE))
      unique_visible_count++;
    if (rf_has(mon->race->flags, RF_INVISIBLE))
      invisible_visible_count++;
    if (rf_has(mon->race->flags, RF_TAKE_ITEM))
      steal_visible_count++;
    if (mon->race->freq_spell > 0 || mon->race->freq_innate > 0) {
      spellcaster_visible_count++;
    }

    score = 0;
    score += mon->mspeed - player->state.speed;
    score += (dist <= 1) ? 25 : (10 - MIN(dist, 10));
    score += rf_has(mon->race->flags, RF_UNIQUE) ? 20 : 0;
    score += rf_has(mon->race->flags, RF_INVISIBLE) ? 8 : 0;
    score += (mon->race->freq_spell > 0 || mon->race->freq_innate > 0) ? 8 : 0;
    score += rf_has(mon->race->flags, RF_TAKE_ITEM) ? 5 : 0;
    score += (mon->m_timed[MON_TMD_SLEEP] > 0) ? -6 : 0;

    if (!top_threat || score > top_threat_score) {
      top_threat = mon;
      top_threat_score = score;
      top_threat_dist = dist;
    }

    if (rf_has(mon->race->flags, RF_UNIQUE)) {
      if (!subwindow_violet_top || mon->race->level > subwindow_violet_level ||
          (mon->race->level == subwindow_violet_level &&
           score > subwindow_violet_score)) {
        subwindow_violet_top = mon;
        subwindow_violet_dist = dist;
        subwindow_violet_level = mon->race->level;
        subwindow_violet_score = score;
      }
    } else if (mon->race->level > player->depth) {
      if (!subwindow_red_top || mon->race->level > subwindow_red_level ||
          (mon->race->level == subwindow_red_level &&
           score > subwindow_red_score)) {
        subwindow_red_top = mon;
        subwindow_red_dist = dist;
        subwindow_red_level = mon->race->level;
        subwindow_red_score = score;
      }
    }
  }

  nearest_upstairs = nearest_stair_distance(true);
  nearest_downstairs = nearest_stair_distance(false);

  file_put(fo, "{\n");
  // file_put(fo, "  meta: {\n");
  // file_put(fo, "    format: ");
  // (void)put_json_string(fo, "angband-threat-report-v11");
  // file_put(fo, ",\n");
  // file_putf(fo, "    isDead: %s\n", player->is_dead ? "true" : "false");
  // file_put(fo, "  },\n");

  file_put(fo, "  player: {\n");
  file_put(fo, "    race: ");
  (void)put_json_string(fo, player->race->name);
  file_put(fo, ",\n");
  file_put(fo, "    class: ");
  (void)put_json_string(fo, player->class->name);
  file_put(fo, ",\n");
  file_putf(fo, "    depth: %d,\n", player->depth);
  file_putf(fo, "    level: %d,\n", player->lev);
  file_putf(fo, "    hp: { current: %d, max: %d },\n", player->chp,
            player->mhp);
  file_putf(fo, "    sp: { current: %d, max: %d },\n", player->csp,
            player->msp);
  file_putf(fo, "    speed: %d,\n", player->state.speed);
  file_putf(fo, "    ac: %d,\n", player->state.ac + player->state.to_a);
  file_putf(fo, "    gold: %d,\n", player->au);
  file_putf(fo, "    position: { y: %d, x: %d },\n", player->grid.y,
            player->grid.x);
  file_putf(fo, "    onUpstairs: %s,\n",
            square_isupstairs(cave, player->grid) ? "true" : "false");
  file_putf(fo, "    onDownstairs: %s\n",
            square_isdownstairs(cave, player->grid) ? "true" : "false");
  file_put(fo, "  },\n");

  file_put(fo, "  navigation: {\n");
  file_putf(fo, "    nearestUpstairs: %d,\n", nearest_upstairs);
  file_putf(fo, "    nearestDownstairs: %d\n", nearest_downstairs);
  file_put(fo, "  },\n");

  file_put(fo, "  status: {\n");
  file_put(fo, "    activeTimed: [");
  {
    bool first = true;

    for (i = 0; i < TMD_MAX; i++) {
      char timed_buf[128];

      if (player->timed[i] <= 0)
        continue;

      if (!first)
        file_put(fo, ", ");

      strnfmt(timed_buf, sizeof(timed_buf), "%s:%d", timed_effects[i].name,
              player->timed[i]);
      (void)put_json_string(fo, timed_buf);
      first = false;
    }
  }
  file_put(fo, "]\n");
  file_put(fo, "  },\n");

  file_put(fo, "  visibility: {\n");
  file_putf(fo, "    visibleMonsters: %d,\n", visible_count);
  file_putf(fo, "    adjacentMonsters: %d,\n", adjacent_count);
  file_putf(fo, "    fasterThanPlayer: %d,\n", faster_count);
  file_putf(fo, "    uniqueVisible: %d,\n", unique_visible_count);
  file_putf(fo, "    invisibleVisible: %d,\n", invisible_visible_count);
  file_putf(fo, "    spellcastersVisible: %d,\n", spellcaster_visible_count);
  file_putf(fo, "    stealersVisible: %d\n", steal_visible_count);
  file_put(fo, "  },\n");

  write_monster_list(fo);

  file_put(fo, "  topThreat: ");
  if (top_threat) {
    const char *danger_names[] = {
        "paralyze", "confuse",   "blind",      "fear", "poison",
        "acid",     "elec",      "fire",       "cold", "disenchant",
        "expDrain", "statDrain", "hallucinate"};
    bool danger_enabled[13];
    const char *mitigation_names[] = {
        "freeAct",    "protConf",  "protBlind",  "protFear",   "holdLife",
        "resPoison",  "resAcid",   "resElec",    "resFire",    "resCold",
        "resDisen",   "resChaos",  "sustainStr", "sustainInt", "sustainWis",
        "sustainDex", "sustainCon"};
    bool mitigation_enabled[] = {
        mitigation.free_act,    mitigation.prot_conf,   mitigation.prot_blind,
        mitigation.prot_fear,   mitigation.hold_life,   mitigation.res_pois,
        mitigation.res_acid,    mitigation.res_elec,    mitigation.res_fire,
        mitigation.res_cold,    mitigation.res_disen,   mitigation.res_chaos,
        mitigation.sustain_str, mitigation.sustain_int, mitigation.sustain_wis,
        mitigation.sustain_dex, mitigation.sustain_con};

    scan_dangerous_attacks(top_threat->race, &danger);
    danger_enabled[0] = danger.paralyze;
    danger_enabled[1] = danger.confuse;
    danger_enabled[2] = danger.blind;
    danger_enabled[3] = danger.fear;
    danger_enabled[4] = danger.poison;
    danger_enabled[5] = danger.acid;
    danger_enabled[6] = danger.elec;
    danger_enabled[7] = danger.fire;
    danger_enabled[8] = danger.cold;
    danger_enabled[9] = danger.disenchant;
    danger_enabled[10] = danger.exp_drain;
    danger_enabled[11] = danger.stat_drain;
    danger_enabled[12] = danger.hallucinate;

    file_put(fo, "{ ");
    file_put(fo, "name: ");
    (void)put_json_string(fo, top_threat->race->name);
    file_putf(fo, ", distance: %d", top_threat_dist);
    file_putf(fo, ", hp: { current: %d, max: %d }", top_threat->hp,
              top_threat->maxhp);
    file_putf(fo, ", speed: %d", top_threat->mspeed);
    if (top_threat->m_timed[MON_TMD_SLEEP] > 0)
      file_put(fo, ", asleep: true");
    if (rf_has(top_threat->race->flags, RF_UNIQUE))
      file_put(fo, ", isUnique: true");
    if (rf_has(top_threat->race->flags, RF_INVISIBLE))
      file_put(fo, ", isInvisible: true");
    if (rf_has(top_threat->race->flags, RF_TAKE_ITEM))
      file_put(fo, ", canStealItems: true");
    if (top_threat->race->freq_spell > 0 || top_threat->race->freq_innate > 0)
      file_put(fo, ", canCastSpells: true");

    file_put(fo, ", dangerousAttacks: ");
    put_json_string_array(fo, danger_names, danger_enabled,
                          sizeof(danger_enabled) / sizeof(danger_enabled[0]));
    file_put(fo, ", playerMitigation: ");
    put_json_string_array(fo, mitigation_names, mitigation_enabled,
                          sizeof(mitigation_enabled) /
                              sizeof(mitigation_enabled[0]));

    {
      double ttk = 0.0;
      double best_damage = estimate_best_attack_damage_vs_monster(top_threat);

      if (best_damage > 0.0 && top_threat->hp > 0)
        ttk = top_threat->hp / best_damage;
      file_putf(fo, ", timeToKill: %.2f", ttk);
    }

    file_put(fo, " }");
  } else {
    file_put(fo, "null");
  }
  file_put(fo, ",\n");

  write_top_threat_recall(fo, top_threat);

  file_put(fo, "  subwindowPriorityTop: {\n");
  file_put(fo, "    violetTop: ");
  if (subwindow_violet_top) {
    file_put(fo, "{ monster: ");
    write_monster_brief(fo, subwindow_violet_top, subwindow_violet_dist);
    file_put(fo, ", recall: ");
    write_monster_recall_object(fo, subwindow_violet_top);
    file_put(fo, " }");
  } else {
    file_put(fo, "null");
  }
  file_put(fo, ",\n");

  file_put(fo, "    redTop: ");
  if (subwindow_red_top) {
    file_put(fo, "{ monster: ");
    write_monster_brief(fo, subwindow_red_top, subwindow_red_dist);
    file_put(fo, ", recall: ");
    write_monster_recall_object(fo, subwindow_red_top);
    file_put(fo, " }");
  } else {
    file_put(fo, "null");
  }
  file_put(fo, "\n  },\n");

  write_best_attack_actions(fo, top_threat);

  file_put(fo, "  inventory: [\n");
  first_inventory = true;
  for (const struct object *obj = player->gear; obj; obj = obj->next) {
    char name_buf[320];
    const char *category_name;
    struct inventory_help_profile item_help;
    bool equipped;
    bool in_quiver;
    bool helps_top;

    object_desc(name_buf, sizeof(name_buf), obj, ODESC_PREFIX | ODESC_FULL,
                player);
    category_name = tval_find_name(obj->tval);
    if (!category_name)
      category_name = "unknown";

    classify_item_help(obj, name_buf, &item_help);
    equipped = object_is_equipped(player->body, obj);
    in_quiver = object_is_in_quiver(player, obj);
    helps_top = top_threat && helps_against_top_threat(&item_help, &danger);

    if (!first_inventory)
      file_put(fo, ",\n");

    file_put(fo, "    { name: ");
    (void)put_json_string(fo, name_buf);
    file_putf(fo, ", quantity: %d", obj->number);
    file_put(fo, ", category: ");
    (void)put_json_string(fo, category_name);
    if (equipped)
      file_put(fo, ", equipped: true");
    if (in_quiver)
      file_put(fo, ", inQuiver: true");
    if (helps_top)
      file_put(fo, ", helpsAgainstTopThreat: true");

    if (tval_is_melee_weapon(obj))
      write_weapon_eval(fo, obj, top_threat);

    if (tval_is_ammo(obj))
      write_missile_eval(fo, obj, top_threat);

    write_item_threat_effects(fo, obj);

    {
      const char *help_names[] = {
          "healing",  "escape",     "cureConfusion", "cureBlindness",
          "cureFear", "curePoison", "restoreMana",   "detectOrRecon"};
      bool help_enabled[] = {
          item_help.healing,        item_help.escape,
          item_help.cure_confusion, item_help.cure_blindness,
          item_help.cure_fear,      item_help.cure_poison,
          item_help.restore_mana,   item_help.detect_or_recon};

      file_put(fo, ", helps: ");
      put_json_string_array(fo, help_names, help_enabled,
                            sizeof(help_enabled) / sizeof(help_enabled[0]));
    }
    file_put(fo, " }");

    first_inventory = false;
  }
  file_put(fo, "\n  ],\n");

  write_available_spells(fo);
  write_tactical_grid(fo, 8);

  // file_put(fo, "  actionHint: ");
  // (void)put_json_string(fo,
  //                       choose_action_hint(nearest_upstairs,
  //                       nearest_downstairs,
  //                                          top_threat, top_threat_dist));
  // file_put(fo, "\n");

  return file_put(fo, "}") && file_close(fo);
}
