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
#include "obj-slays.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player-attack.h"
#include "player-calcs.h"
#include "player-spell.h"
#include "player-timed.h"
#include "player.h"
#include "project.h"
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

    if (!entry->race)
      continue;

    field = (entry->count[MONSTER_LIST_SECTION_LOS] > 0)
                ? MONSTER_LIST_SECTION_LOS
                : MONSTER_LIST_SECTION_ESP;
    count = entry->count[field];
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


static const char *const mon_spell_names[] = {
#define RSF(a, b) #a,
#include "list-mon-spells.h"
#undef RSF
};

static bool is_projecting_effect(const struct effect *effect) {
  return effect->index == EF_BOLT || effect->index == EF_BOLT_AWARE ||
         effect->index == EF_BALL || effect->index == EF_ARC ||
         effect->index == EF_BREATH;
}

/* Writes the highest known spell damage the monster can deal in one player
 * turn, after the player's resistances. */
static void write_max_special_damage(ang_file *fo, const struct monster *mon,
                                     const struct monster_lore *lore) {
  int best_dam = 0;
  int best_flag = RSF_NONE;
  int max_actions = 1;
  int flag;

  for (flag = rsf_next(lore->spell_flags, FLAG_START); flag != FLAG_END;
       flag = rsf_next(lore->spell_flags, flag + 1)) {
    const struct monster_spell *spell;
    const struct effect *effect;
    int dam;

    if (flag <= RSF_NONE || flag >= RSF_MAX)
      continue;
    spell = monster_spell_by_index(flag);
    if (!spell || !spell->effect)
      continue;
    effect = spell->effect;

    if (effect->index == EF_BREATH)
      dam = breath_dam(effect->subtype, mon->hp);
    else
      dam = mon_spell_lore_damage(flag, mon->race, false);
    if (dam <= 0)
      continue;

    if (is_projecting_effect(effect))
      dam = adjust_dam(player, effect->subtype, dam, MAXIMISE, 0, false);

    if (dam > best_dam) {
      best_dam = dam;
      best_flag = flag;
    }
  }

  if (best_flag == RSF_NONE)
    return;

  if (player->state.speed > 0) {
    int p_energy = turn_energy(player->state.speed);
    int m_energy = turn_energy(mon->mspeed);

    if (p_energy > 0)
      max_actions = MAX(1, (m_energy + p_energy - 1) / p_energy);
  }

  file_put(fo, ", maxSpecialDamagePerPlayerRound: { spell: ");
  (void)put_json_string(fo, mon_spell_names[best_flag]);
  file_putf(fo, ", damagePerCast: %d, maxCasts: %d, total: %d }", best_dam,
            max_actions, best_dam * max_actions);
}

/* Writes the known lore of a monster as fields of an enclosing object. */
static void write_monster_recall_fields(ang_file *fo,
                                        const struct monster *mon) {
  const struct monster_lore *lore;

  if (!mon || !mon->race)
    return;

  lore = get_lore(mon->race);
  if (!lore)
    return;

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

    if (lore->armour_known)
      file_putf(fo, ", knownArmorClass: %d", mon->race->ac);

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
    write_max_special_damage(fo, mon, lore);
  }
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
  double fail_pct;
  bool fail_is_miss;
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

  if (streq(projection, "light") || streq(projection, "weak light")) {
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

/* The expected damage is reduced by the failure (or miss) chance; the minimum
 * damage is not. */
static void add_attack_action(struct attack_action_summary *actions, int *count,
                              int max_count, const char *name,
                              double avg_damage, double min_damage,
                              double fail_pct, bool fail_is_miss) {
  if (!actions || !count || !name || *count >= max_count)
    return;

  my_strcpy(actions[*count].name, name, sizeof(actions[*count].name));
  actions[*count].expected_damage = avg_damage * (100.0 - fail_pct) / 100.0;
  actions[*count].min_damage = min_damage;
  actions[*count].fail_pct = fail_pct;
  actions[*count].fail_is_miss = fail_is_miss;
  actions[*count].time_to_kill = 0.0;
  actions[*count].sp_cost = 0;
  actions[*count].expected_damage_per_sp = 0.0;
  (*count)++;
}

static int object_device_level(const struct object *obj) {
  if (obj->artifact)
    return obj->artifact->level;
  if (obj->activation)
    return obj->activation->level;
  return obj->kind->level;
}

/* Sums the damage of a chain of effects against the target.  The device skill
 * boost does not apply to breaths. */
static bool sum_effect_damage(struct effect *chain,
                              const struct monster *target, int boost,
                              double *avg_total, double *min_total) {
  struct effect *effect;
  const dice_t *shared_dice = NULL;
  bool has_damage = false;

  *avg_total = 0.0;
  *min_total = 0.0;

  for (effect = chain; effect; effect = effect_next(effect)) {
    double mult;
    double scale;

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

    mult = damage_multiplier_for_target(target, effect_projection(effect));
    scale = effect->index == EF_BREATH ? 1.0 : (100 + boost) / 100.0;
    *avg_total += effect_avg_damage(effect, shared_dice) * mult * scale;
    *min_total += effect_min_damage(effect, shared_dice) * mult * scale;
    has_damage = true;
  }

  return has_damage && (*avg_total > 0.0 || *min_total > 0.0);
}

/* Returns the extra multiplier the best brand or slay of the given objects
 * (and, in melee, of the other equipment and temporary effects) gives against
 * the target, or 0 if none applies.  Only known brands and slays count. */
static int target_brand_slay_mult(const struct monster *target,
                                  const struct object *obj1,
                                  const struct object *obj2, bool melee,
                                  int *brand_out, int *slay_out) {
  int b = 0;
  int s = 0;
  char verb[20];

  *brand_out = 0;
  *slay_out = 0;
  if (!target || !target->race)
    return 0;

  if (melee) {
    int j;

    for (j = 2; j < player->body.count; j++) {
      struct object *slot_obj = slot_object(player, j);

      if (slot_obj && slot_obj->known)
        improve_attack_modifier(player, slot_obj->known, target, &b, &s, verb,
                                false);
    }
  }
  if (obj1 && obj1->known)
    improve_attack_modifier(player, obj1->known, target, &b, &s, verb, !melee);
  if (obj2 && obj2->known)
    improve_attack_modifier(player, obj2->known, target, &b, &s, verb, !melee);
  if (melee)
    improve_attack_modifier(player, NULL, target, &b, &s, verb, false);

  *brand_out = b;
  *slay_out = s;
  if (s)
    return slays[s].multiplier;
  if (b)
    return get_monster_brand_multiplier(target, &brands[b], false);
  return 0;
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

      (void)obj_known_damage(obj, &normal_damage, brand_damage, slay_damage,
                             &nonweap_slay, false);

      if (obj->known && obj->known->dd > 0 && obj->known->ds > 0 &&
          normal_damage >= 0) {
        int to_hit = chance_of_melee_hit_base(player, obj);
        int hit_pct = top_threat_hit_percent(to_hit, target);
        int blows_centiblows = weapon_blows_centiblows(obj);
        int b, s;
        int mult = target_brand_slay_mult(target, obj, NULL, true, &b, &s);
        int min_per_hit;
        double min_per_round;
        double expected = normal_damage / 10.0;
        char action_name[180];

        if (mult > 1) {
          int base_mult = s ? slays[s].multiplier : brands[b].multiplier;
          int tenths = s ? slay_damage[s] : brand_damage[b];

          /* Vulnerability doubles the brand's extra damage. */
          if (tenths > normal_damage && base_mult > 1) {
            expected = (normal_damage + (double)(tenths - normal_damage) *
                                            (mult - 1) / (base_mult - 1)) /
                       10.0;
          }
        } else {
          mult = 1;
        }

        min_per_hit = obj->known->dd * mult + object_to_dam(obj->known) +
                      player->state.to_d;
        if (min_per_hit < 0)
          min_per_hit = 0;
        min_per_round = (blows_centiblows / 100.0) * min_per_hit;
        object_desc(action_name, sizeof(action_name), obj,
                    ODESC_PREFIX | ODESC_FULL, player);
        if (normal_damage >= 0 && blows_centiblows > 0 && min_per_hit > 0) {
          add_attack_action(actions, action_count, max_count, action_name,
                            expected, min_per_round, 100 - hit_pct, true);
        }
      }

      mem_free(slay_damage);
      mem_free(brand_damage);
    }

    if (tval_is_ammo(obj)) {
      const struct object *best_launcher = NULL;
      struct player_state best_state;
      int best_to_hit = 0;
      double best_min_shot = 0.0;
      double best_avg_round_vs_target = -1.0;
      double best_avg_round_raw = 0.0;
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

          int b, s;
          int extra;
          double factor;

          if (!tval_is_launcher(launch))
            continue;
          if (!launcher_matches_ammo(launch, obj->tval))
            continue;
          if (!compute_launcher_state(launch, &local_state))
            continue;
          extra = target_brand_slay_mult(target, obj, launch, false, &b, &s);
          factor = (MAX(1, local_state.ammo_mult) + extra) /
                   (double)MAX(1, local_state.ammo_mult);

          to_hit =
              local_state.skills[SKILL_TO_HIT_BOW] +
              (object_to_hit(obj) + local_state.to_h + object_to_hit(launch)) *
                  BTH_PLUS_ADJ;
          hit_pct = top_threat_hit_percent(to_hit, target);

          avg_shot = ammo_dd * (ammo_ds + 1) / 2.0 + object_to_dam(obj) +
                     object_to_dam(launch);
          min_shot = ammo_dd + object_to_dam(obj) + object_to_dam(launch);
          avg_shot *= MAX(1, local_state.ammo_mult) * factor;
          min_shot *= MAX(1, local_state.ammo_mult) * factor;
          if (avg_shot < 0)
            avg_shot = 0;
          if (min_shot < 0)
            min_shot = 0;

          avg_round_vs_target =
              avg_shot * (local_state.num_shots / 10.0) * (hit_pct / 100.0);
          if (avg_round_vs_target > best_avg_round_vs_target) {
            best_avg_round_vs_target = avg_round_vs_target;
            best_avg_round_raw = avg_shot * (local_state.num_shots / 10.0);
            best_launcher = launch;
            best_state = local_state;
            best_to_hit = to_hit;
            best_min_shot = min_shot;
          }
        }

        if (best_launcher) {
          int hit_pct = top_threat_hit_percent(best_to_hit, target);
          double min_round =
              best_min_shot * (best_state.num_shots / 10.0);
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
                              best_avg_round_raw, min_round, 100 - hit_pct,
                              true);
          }
        }
      }
    }

    {
      struct effect *chain = object_effect(obj);
      double avg_total;
      double min_total;
      int boost;
      double fail_pct = 0.0;

      if (!chain)
        continue;

      boost = MAX(
          (player->state.skills[SKILL_DEVICE] - object_device_level(obj)) / 2,
          0);
      if (!tval_is_edible(obj) && !tval_is_potion(obj) && !tval_is_scroll(obj))
        fail_pct = get_use_device_chance(obj) / 10.0;

      if (sum_effect_damage(chain, target, boost, &avg_total, &min_total)) {
        char item_name[180];
        object_desc(item_name, sizeof(item_name), obj,
                    ODESC_PREFIX | ODESC_FULL, player);
        add_attack_action(actions, action_count, max_count, item_name,
                          avg_total, min_total, fail_pct, false);
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
      bool has_damage;
      double avg = 0.0;
      double min = 0.0;
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

      has_damage = sum_effect_damage(spell->effect, target, 0, &avg, &min);
      if (!has_damage)
        continue;

      fail = spell_chance(sidx);
      strnfmt(action_name, sizeof(action_name), "Spell: %s", spell->name);
      add_attack_action(actions, action_count, max_count, action_name, avg, min,
                        fail, false);
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
      file_putf(fo, ", expectedDamVsTopThreat: %.2f",
                actions[i].expected_damage);
      file_putf(fo, ", minDam: %.2f", actions[i].min_damage);
      file_putf(fo, actions[i].fail_is_miss ? ", missChancePct: %.1f"
                                            : ", failChancePct: %.1f",
                actions[i].fail_pct);
      file_putf(fo, ", timeToKill: %.2f", actions[i].time_to_kill);
      if (actions[i].sp_cost > 0) {
        file_putf(fo, ", spCost: %d", actions[i].sp_cost);
        file_putf(fo, ", expectedDamPerSp: %.2f",
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
  file_putf(fo, ", avgDamPerShotWithBestLauncher: %.2f", best_avg_shot);
  file_putf(fo, ", avgDamPerRoundWithBestLauncher: %.2f", best_avg_round);
  if (top_threat && top_threat->race) {
    random_chance hit;
    int hit_pct;
    hit_chance(&hit, best_to_hit, top_threat->race->ac);
    hit_pct = random_chance_scaled(hit, 100);
    file_putf(fo, ", hitChanceVsTopThreatVisible: %d", hit_pct);
    file_putf(fo, ", avgDamPerRoundVsTopThreat: %.2f",
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

/* Based on the adaptation of Exo's patch to frogcomposband. */
bool save_charoutput(void) {
  char path[1024];
  ang_file *fo;
  struct monster *top_threat = NULL;
  struct danger_profile danger;
  struct mitigation_profile mitigation;
  int i;
  int top_threat_score = -100000;
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
    }
  }

  file_put(fo, "{\n");

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

  write_monster_list(fo);

  file_put(fo, "  topThreat: ");
  if (top_threat) {
    bool danger_enabled[13];

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
    file_putf(fo, ", hp: \"%d/%d\"", top_threat->hp, top_threat->maxhp);
    file_putf(fo, ", speed: %d", top_threat->mspeed);
    if (rf_has(top_threat->race->flags, RF_INVISIBLE))
      file_put(fo, ", isInvisible: true");

    {
      double ttk = 0.0;
      double best_damage = estimate_best_attack_damage_vs_monster(top_threat);

      if (best_damage > 0.0 && top_threat->hp > 0)
        ttk = top_threat->hp / best_damage;
      file_putf(fo, ", timeToKill: %.2f", ttk);
    }

    write_monster_recall_fields(fo, top_threat);

    file_put(fo, " }");
  } else {
    file_put(fo, "null");
  }
  file_put(fo, ",\n");

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

  return file_put(fo, "}") && file_close(fo);
}
