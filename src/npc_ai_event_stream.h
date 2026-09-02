#pragma once
#ifndef CATA_SRC_NPC_AI_EVENT_STREAM_H
#define CATA_SRC_NPC_AI_EVENT_STREAM_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

class Creature;
class npc;

namespace npc_ai
{

// Physical/narrative facts captured on the main game thread.  The stream does
// not schedule speech; Combat Social remains the sole combat speech scheduler.
enum class world_event_type : int {
    combat_start,
    combat_end,
    enemy_spotted,
    dangerous_enemy_spotted,
    enemy_group_detected,
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
    heal_completed
};

// Maximum physical assertion the supplied fact can support.  This is decided
// by CDDA before generation; model text is never evidence for a stronger
// outcome.
enum class world_event_claim_level : int {
    fact_only = 0,
    hit_confirmed = 1,
    limb_disabled = 2,
    death_confirmed = 3
};

struct world_entity_snapshot {
    std::string kind;
    int character_id = -1;
    std::string type_id;
    std::string name;
    int x = 0;
    int y = 0;
    int z = 0;
};

struct world_event {
    std::uint64_t sequence_id = 0;
    std::uint64_t session_generation = 0;
    std::uint64_t encounter_generation = 0;
    int game_turn = 0;
    world_event_type type = world_event_type::combat_start;
    world_entity_snapshot actor;
    world_entity_snapshot target;
    int previous_x = 0;
    int previous_y = 0;
    int previous_z = 0;
    int importance = 0;
    int audible_volume = 0;
    bool resolve_perception_lazily = false;
    bool confirmed_outcome = false;
    std::string source;
    std::string detail;
    std::string body_part;
    std::string attack_mode;
    int damage = 0;
    world_event_claim_level claim_level = world_event_claim_level::fact_only;
    std::vector<int> known_by_npc_ids;
    std::vector<std::uint64_t> coalesced_with;
    std::string candidate_state = "captured";
    bool selected = false;
    std::uint64_t request_id = 0;
    std::string discard_reason;
};

constexpr std::size_t world_event_ring_capacity = 256;

world_entity_snapshot snapshot_entity( const Creature *creature );
std::string world_event_type_name( world_event_type type );
std::string world_event_claim_level_name( world_event_claim_level level );

// Records a confirmed fact and snapshots which allied NPCs could know it at
// that moment (participant, sight, or hearing when audible_volume > 0).
std::uint64_t record_world_event( world_event event );
std::uint64_t record_creature_world_event( world_event_type type, const Creature *actor,
        const Creature *target, int importance, const std::string &source,
        const std::string &detail, bool confirmed_outcome = true,
        std::uint64_t encounter_generation = 0, int audible_volume = 0,
        int previous_x = 0, int previous_y = 0, int previous_z = 0,
        const std::string &body_part = {}, int damage = 0,
        const std::string &attack_mode = {},
        world_event_claim_level claim_level = world_event_claim_level::fact_only );

std::optional<world_event> world_event_by_sequence( std::uint64_t sequence_id );

// Adds an observer to an already captured shared fact.  This is used when a
// second ally learns an active fact later; it must not create a second event
// or a second speech request for the same fact.
bool add_world_event_known_observer( std::uint64_t sequence_id, int observer_id );

std::vector<world_event> recent_world_events_for( const npc &observer,
        std::uint64_t after_sequence = 0, std::size_t maximum = 10,
        int maximum_age_turns = 120 );
std::string build_recent_world_event_context( const npc &observer,
        std::size_t maximum = 10, int maximum_age_turns = 120 );

std::size_t world_event_stream_size();
std::uint64_t latest_world_event_sequence();
void reset_world_event_stream();
void annotate_world_event( std::uint64_t sequence_id, const std::string &candidate_state,
                           bool selected = false, std::uint64_t request_id = 0,
                           const std::string &discard_reason = {},
                           const std::vector<std::uint64_t> &coalesced_with = {},
                           std::uint64_t encounter_generation = 0 );

// Off by default.  When enabled the bounded diagnostic log is written beside
// the active world as npc_ai_events.jsonl (or under the user dir before load).
void set_world_event_jsonl_debug( bool enabled );
bool world_event_jsonl_debug_enabled();
std::string world_event_jsonl_path();

} // namespace npc_ai

#endif // CATA_SRC_NPC_AI_EVENT_STREAM_H
