#pragma once
#ifndef CATA_SRC_NPC_AI_COMBAT_SOCIAL_H
#define CATA_SRC_NPC_AI_COMBAT_SOCIAL_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "npc_ai_event_stream.h"

class npc;
class monster;
class Creature;

namespace npc_ai
{

struct ai_request_completion;
struct ai_completion_apply_timings;

enum class combat_social_event_type : int {
    combat_start,
    enemy_spotted,
    dangerous_enemy_spotted,
    npc_attack,
    npc_hit,
    npc_badly_hurt,
    npc_bleeding,
    npc_grabbed,
    player_hit,
    player_badly_hurt,
    player_bleeding,
    player_grabbed,
    player_surrounded,
    ally_hit,
    ally_badly_hurt,
    ally_bleeding,
    ally_grabbed,
    ally_surrounded,
    enemy_killed,
    enemy_group_detected,
    low_stamina,
    low_ammo,
    out_of_ammo,
    weapon_jammed,
    retreat_started,
    ally_retreating,
    failed_escape,
    grab_broken,
    dragged,
    ally_dragged,
    significant_critical,
    ally_critical_hit,
    player_critical_hit,
    attack_missed,
    dodge,
    ally_saved,
    heal_started,
    heal_completed,
    combat_end
};

struct combat_visible_creature {
    int character_id = -1;
    std::uint64_t runtime_identity = 0;
    std::string name;
    std::string attitude;
    std::string condition;
    std::string held_item;
    std::string target_name;
    int dx = 0;
    int dy = 0;
    int dz = 0;
    int distance = 0;
    int adjacent_hostiles = 0;
    // Internal transition detector; the prompt exposes only condition.
    int hp_percent = 100;
    bool player = false;
    bool npc = false;
    bool monster = false;
    bool hostile = false;
    bool bleeding = false;
    bool grabbed = false;
    bool retreating = false;
    bool adjacent = false;
    bool observer_target = false;
    bool targeting_observer = false;
};

struct combat_audible_event {
    std::string kind;
    int dx = 0;
    int dy = 0;
    int dz = 0;
    int distance = 0;
    int volume = 0;
};

struct combat_perception_snapshot {
    int observer_id = -1;
    int turn = 0;
    int x = 0;
    int y = 0;
    int z = 0;
    int visible_enemy_count = 0;
    int visible_ally_count = 0;
    int stamina_percent = 100;
    int pain = 0;
    int morale = 0;
    int fear = 0;
    int hp_percent = 100;
    int ammo_remaining = -1;
    int ammo_capacity = -1;
    bool bleeding = false;
    bool grabbed = false;
    bool retreating = false;
    bool in_combat = false;
    bool outside = false;
    float ambient_light = 0.0f;
    float tactical_danger = 0.0f;
    std::string wielded_weapon;
    std::string current_tile;
    std::string weather;
    std::string tactical_intent;
    // Diagnostic work counter used by scaling tests.  This counts observer-
    // specific LOS checks, not the number of creatures finally rendered.
    std::size_t creature_visibility_checks = 0;
    std::vector<combat_visible_creature> visible_creatures;
    std::vector<combat_audible_event> audible_events;
};

struct combat_social_event {
    combat_social_event_type type = combat_social_event_type::combat_start;
    int target_id = -1;
    std::string target_name;
    int importance = 0;
    bool may_bypass_cooldown = false;
    std::string detail;
    std::uint64_t sequence_id = 0;
    int actor_id = -1;
    std::string actor_name;
    std::string actor_identity;
    std::string target_identity;
    std::vector<std::uint64_t> coalesced_sequences;
    std::uint64_t encounter_generation = 0;
    bool confirmed_outcome = false;
    std::string body_part;
    std::string attack_mode;
    int damage = 0;
    world_event_claim_level claim_level = world_event_claim_level::fact_only;
    // Snapshot-only metadata.  A first sight is an observer transition, while
    // its request lifecycle is shared by every allied observer of the same
    // typed target identity.
    bool observer_first_sight = false;
    bool first_sight_count_companion = false;
    int previous_visible_enemy_count = 0;
    int current_visible_enemy_count = 0;
    bool group_already_verbalized = false;
};

struct combat_social_metrics {
    std::uint64_t narrable_events_captured = 0;
    std::uint64_t narrable_events_verbalized = 0;
    std::uint64_t inferences_queued = 0;
    std::uint64_t candidates_validated = 0;
    std::uint64_t lines_emitted = 0;
    std::uint64_t discarded_cooldown = 0;
    std::uint64_t discarded_deduplication = 0;
    std::uint64_t discarded_expiration = 0;
    std::uint64_t discarded_knowledge = 0;
    std::uint64_t discarded_validation = 0;
    std::uint64_t discarded_tactical_promise = 0;
    std::uint64_t fallback_activations = 0;
    std::size_t queue_depth_max = 0;
    std::size_t queue_depth_p95 = 0;
    std::vector<std::pair<int, std::uint64_t>> lines_by_speaker;
};

struct combat_social_process_result {
    bool event_detected = false;
    bool request_queued = false;
    combat_social_event event;
    std::uint64_t request_id = 0;
};

combat_perception_snapshot build_combat_perception_snapshot( const npc &who );
std::vector<combat_social_event> detect_combat_social_events_for_test(
    const combat_perception_snapshot &before,
    const combat_perception_snapshot &now );
std::size_t combat_snapshot_visibility_check_limit();
bool combat_social_situation_is_clear( const npc &who );
bool combat_social_text_claims_no_threats( const std::string &text );
std::string build_combat_social_prompt( const npc &who, const combat_perception_snapshot &snapshot,
                                        const combat_social_event &event );
std::string combat_social_event_name( combat_social_event_type type );
std::string combat_social_intent_name( combat_social_event_type type );
bool combat_social_text_has_unconfirmed_tactical_promise( const std::string &text );
int combat_social_speak_priority( const combat_social_event &event );
combat_social_metrics combat_social_metrics_snapshot();
void reset_combat_social_metrics();
void set_combat_social_batching_for_test( bool enabled );

// Called after vanilla has refreshed the NPC combat cache.  It observes and
// queues speech only; it never changes combat actions or waits for Ollama.
combat_social_process_result process_combat_social( npc &who );

// Main-thread completion application.
void apply_combat_social_ai_completion( npc &who, const ai_request_completion &completion,
                                        ai_completion_apply_timings *timings );

// Records a confirmed physical death for allied observers that can actually
// see it.  Request creation remains deferred to their normal movement turn.
void notify_visible_enemy_killed( const monster &victim, const Creature *killer );

void reset_combat_social_state_for_test( const npc &who );
void reset_all_combat_social_states();

} // namespace npc_ai

#endif // CATA_SRC_NPC_AI_COMBAT_SOCIAL_H
