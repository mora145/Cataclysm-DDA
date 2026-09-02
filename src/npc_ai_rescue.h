#pragma once
#ifndef CATA_SRC_NPC_AI_RESCUE_H
#define CATA_SRC_NPC_AI_RESCUE_H

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "calendar.h"
#include "coordinates.h"

class npc;

namespace npc_ai
{

enum class casualty_mobility {
    can_walk,
    impaired,
    needs_drag,
    must_not_move
};

enum class drag_step_outcome {
    moved,
    opened_door,
    rejected
};

struct drag_step_result {
    drag_step_outcome outcome = drag_step_outcome::rejected;
    int move_cost = 0;
    std::string reason;

    explicit operator bool() const {
        return outcome == drag_step_outcome::moved;
    }
};

using rescue_id = std::uint64_t;

enum class rescue_phase {
    approaching,
    positioning,
    attached,
    dragging,
    complete,
    failed,
    cancelled
};

struct rescue_state {
    rescue_id id = 0;
    int rescuer_id = -1;
    int casualty_id = -1;
    tripoint_abs_ms casualty_destination;
    tripoint_abs_ms rescuer_final_anchor;
    rescue_phase phase = rescue_phase::approaching;
    std::uint64_t goal_id = 0;
    time_point created_turn = calendar::turn_zero;
    time_point last_progress_turn = calendar::turn_zero;
    std::vector<tripoint_abs_ms> casualty_path;
    std::vector<tripoint_abs_ms> rescuer_path;
    std::string failure_reason;
};

enum class rescue_tick_result {
    not_applicable,
    deferred_to_vanilla,
    consumed_turn,
    completed,
    cancelled
};

enum class rescue_finish_outcome {
    completed,
    failed,
    cancelled
};

struct rescue_command_result {
    bool handled = false;
    bool started = false;
    bool cancelled = false;
    rescue_id id = 0;
    npc *rescuer = nullptr;
    npc *casualty = nullptr;
    std::string message;
};

using rescue_destination_selector =
    std::function<std::optional<tripoint_bub_ms>()>;
using rescue_goal_factory =
    std::function<std::uint64_t( const npc &, const std::string & )>;

casualty_mobility classify_casualty_mobility( const npc &casualty );
bool casualty_allows_ordered_drag( const npc &casualty );
bool parse_rescue_order( const std::string &player_line );

/**
 * Opens an ordinary unlocked door for a future drag step.  Opening consumes
 * this turn and never moves either NPC.
 */
drag_step_result try_open_drag_door( npc &rescuer, npc &casualty,
                                     const tripoint_bub_ms &rescuer_dest );

/**
 * Pull-follow one-tile movement.  The rescuer enters rescuer_dest and the
 * casualty enters the rescuer's old tile.  Every tile and both movement edges
 * are validated before either NPC is moved.
 */
drag_step_result try_drag_step( npc &rescuer, npc &casualty,
                                const tripoint_bub_ms &rescuer_dest );

std::optional<rescue_id> claim_rescue( const npc &rescuer, const npc &casualty,
                                       const tripoint_abs_ms &casualty_destination,
                                       const tripoint_abs_ms &rescuer_final_anchor,
                                       std::string *failure_reason = nullptr );
std::optional<rescue_id> begin_rescue( npc &rescuer, npc &casualty,
                                       const tripoint_bub_ms &casualty_destination,
                                       std::string *failure_reason = nullptr );
rescue_command_result execute_rescue_order( const std::vector<npc *> &candidate_rescuers,
        const std::string &player_line, const rescue_destination_selector &select_destination );
std::optional<rescue_state> rescue_for_rescuer( const npc &rescuer );
std::optional<rescue_state> rescue_for_casualty( const npc &casualty );
rescue_tick_result tick_rescue( npc &rescuer );
bool consume_linked_casualty_turn( npc &casualty );
bool finish_rescue( rescue_id id, rescue_finish_outcome outcome,
                    const std::string &reason = std::string() );
bool release_rescue( rescue_id id, rescue_phase terminal_phase,
                     const std::string &reason = std::string() );
bool cancel_rescue_for( const npc &participant, const std::string &reason );
std::size_t cancel_rescues_for_new_order( const std::vector<npc *> &targets,
        const std::string &reason );
void set_rescue_goal_factory_for_test( rescue_goal_factory factory );
void clear_rescues_for_test();
void reset_all_rescues();

} // namespace npc_ai

#endif // CATA_SRC_NPC_AI_RESCUE_H
