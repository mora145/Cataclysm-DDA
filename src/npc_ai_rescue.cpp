#include "npc_ai_rescue.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cata_utility.h"
#include "catacharset.h"
#include "character.h"
#include "character_id.h"
#include "character_modifier.h"
#include "creature_tracker.h"
#include "debug.h"
#include "field.h"
#include "game.h"
#include "flag.h"
#include "map.h"
#include "mapdata.h"
#include "npc.h"
#include "npc_ai_goal.h"
#include "pathfinding.h"
#include "point.h"
#include "string_formatter.h"
#include "trap.h"
#include "translations.h"
#include "type_id.h"
#include "units.h"
#include "unicode.h"

namespace
{

static const character_modifier_id character_modifier_limb_run_cost_mod(
    "limb_run_cost_mod" );

static const efftype_id effect_beartrap( "beartrap" );
static const efftype_id effect_crushed( "crushed" );
static const efftype_id effect_heavysnare( "heavysnare" );
static const efftype_id effect_in_pit( "in_pit" );
static const efftype_id effect_lightsnare( "lightsnare" );
static const efftype_id effect_narcosis( "narcosis" );
static const efftype_id effect_psi_stunned( "psi_stunned" );
static const efftype_id effect_stunned( "stunned" );
static const efftype_id effect_under_operation( "under_operation" );
static const efftype_id effect_webbed( "webbed" );

static const json_character_flag json_flag_CANNOT_MOVE( "CANNOT_MOVE" );
static const json_character_flag json_flag_GRAB( "GRAB" );

std::unordered_map<npc_ai::rescue_id, npc_ai::rescue_state> rescues;
std::unordered_map<int, npc_ai::rescue_id> rescue_by_rescuer;
std::unordered_map<int, npc_ai::rescue_id> rescue_by_casualty;
npc_ai::rescue_id next_rescue_id = 1;
npc_ai::rescue_goal_factory rescue_goal_factory_override;

constexpr time_duration rescue_watchdog_timeout = 30_turns;

struct route_plan {
    tripoint_bub_ms anchor;
    std::vector<tripoint_bub_ms> casualty_path;
    std::vector<tripoint_bub_ms> rescuer_path;
};

std::string normalize_rescue_text( const std::string &text )
{
    std::u32string codepoints = utf8_to_utf32( text );
    for( char32_t &codepoint : codepoints ) {
        u32_to_lowercase( codepoint );
        remove_accent( codepoint );
    }
    std::string normalized;
    bool previous_space = true;
    for( const char32_t codepoint : codepoints ) {
        if( ( codepoint >= U'a' && codepoint <= U'z' ) ||
            ( codepoint >= U'0' && codepoint <= U'9' ) ) {
            normalized.push_back( static_cast<char>( codepoint ) );
            previous_space = false;
        } else if( !previous_space ) {
            normalized.push_back( ' ' );
            previous_space = true;
        }
    }
    while( !normalized.empty() && normalized.back() == ' ' ) {
        normalized.pop_back();
    }
    return normalized;
}

bool names_casualty_after_drag_verb( const std::string &line, const std::string &name )
{
    const std::string normalized_line = normalize_rescue_text( line );
    const std::string normalized_name = normalize_rescue_text( name );
    if( normalized_name.empty() ) {
        return false;
    }
    static constexpr std::array<std::string_view, 3> spanish_verbs = {
        "arrastra", "arrastrad", "arrastrar"
    };
    static constexpr std::array<std::string_view, 2> english_verbs = {
        "drag", "haul"
    };
    const auto contains_words = [&]( const std::string &phrase ) {
        std::size_t position = normalized_line.find( phrase );
        while( position != std::string::npos ) {
            const std::size_t after = position + phrase.size();
            if( ( position == 0 || normalized_line[position - 1] == ' ' ) &&
                ( after == normalized_line.size() || normalized_line[after] == ' ' ) ) {
                return true;
            }
            position = normalized_line.find( phrase, position + 1 );
        }
        return false;
    };
    for( const std::string_view verb : spanish_verbs ) {
        if( contains_words( std::string( verb ) + " a " + normalized_name ) ) {
            return true;
        }
    }
    for( const std::string_view verb : english_verbs ) {
        if( contains_words( std::string( verb ) + " " + normalized_name ) ) {
            return true;
        }
    }
    return false;
}

int npc_key( const npc &who )
{
    return who.getID().get_value();
}

bool is_adjacent_same_z( const tripoint_bub_ms &from, const tripoint_bub_ms &to )
{
    return from.z() == to.z() && from != to &&
           std::abs( from.x() - to.x() ) <= 1 &&
           std::abs( from.y() - to.y() ) <= 1;
}

bool has_forbidden_rescue_effect( const npc &who )
{
    return who.has_effect( effect_under_operation ) || who.has_effect( effect_crushed ) ||
           who.has_effect( effect_beartrap ) || who.has_effect( effect_heavysnare ) ||
           who.has_effect( effect_lightsnare ) || who.has_effect( effect_webbed ) ||
           who.has_effect( effect_in_pit ) || who.has_effect_with_flag( json_flag_GRAB );
}

bool tile_has_v1_special_movement( const map &here, const tripoint_bub_ms &p )
{
    return here.is_open_air( p ) ||
           here.has_flag_ter_or_furn( ter_furn_flag::TFLAG_CLIMBABLE, p ) ||
           here.has_flag_ter_or_furn( ter_furn_flag::TFLAG_DEEP_WATER, p ) ||
           here.has_flag_ter_or_furn( ter_furn_flag::TFLAG_RAMP, p ) ||
           here.has_flag_ter_or_furn( ter_furn_flag::TFLAG_RAMP_UP, p ) ||
           here.has_flag_ter_or_furn( ter_furn_flag::TFLAG_RAMP_DOWN, p ) ||
           here.has_flag_ter_or_furn( ter_furn_flag::TFLAG_GOES_UP, p ) ||
           here.has_flag_ter_or_furn( ter_furn_flag::TFLAG_GOES_DOWN, p );
}

bool tile_is_v1_safe( map &here, const tripoint_bub_ms &p, std::string &reason )
{
    if( !here.inbounds( p ) ) {
        reason = "tile is outside the loaded map";
        return false;
    }
    if( here.veh_at( p ) ) {
        reason = "vehicle tiles are not supported";
        return false;
    }
    if( !here.tr_at( p ).is_null() ) {
        reason = "traps are not supported";
        return false;
    }
    if( here.field_at( p ).field_count() != 0 ) {
        reason = "fields are not supported";
        return false;
    }
    if( tile_has_v1_special_movement( here, p ) ) {
        reason = "special terrain movement is not supported";
        return false;
    }
    return true;
}

bool diagonal_edge_is_clear( map &here, const tripoint_bub_ms &from,
                             const tripoint_bub_ms &to, std::string &reason )
{
    if( from.x() == to.x() || from.y() == to.y() ) {
        return true;
    }
    const tripoint_bub_ms x_side{ to.x(), from.y(), from.z() };
    const tripoint_bub_ms y_side{ from.x(), to.y(), from.z() };
    for( const tripoint_bub_ms &side : { x_side, y_side } ) {
        if( !tile_is_v1_safe( here, side, reason ) || !here.passable_through( side ) ) {
            reason = "diagonal movement cuts a blocked corner";
            return false;
        }
    }
    return true;
}

bool edge_is_v1_legal( map &here, const tripoint_bub_ms &from,
                       const tripoint_bub_ms &to, std::string &reason )
{
    if( !is_adjacent_same_z( from, to ) ) {
        reason = "movement edge is not adjacent on the same z-level";
        return false;
    }
    if( !tile_is_v1_safe( here, from, reason ) || !tile_is_v1_safe( here, to, reason ) ) {
        return false;
    }
    if( !here.passable_through( to ) || !here.valid_move( from, to, false, false, false ) ) {
        reason = "movement edge is impassable";
        return false;
    }
    if( !diagonal_edge_is_clear( here, from, to, reason ) ) {
        return false;
    }
    if( here.combined_movecost( from, to ) <= 0 ) {
        reason = "movement edge has no valid movement cost";
        return false;
    }
    return true;
}

bool tile_is_route_available( map &here, const tripoint_bub_ms &p,
                              const npc *allowed_first, const npc *allowed_second )
{
    std::string ignored;
    if( !tile_is_v1_safe( here, p, ignored ) ) {
        return false;
    }
    const Creature *occupant = get_creature_tracker().creature_at( p );
    return occupant == nullptr || occupant == allowed_first || occupant == allowed_second;
}

bool route_edge_is_v1_legal( npc &door_opener, map &here,
                             const tripoint_bub_ms &from, const tripoint_bub_ms &to )
{
    std::string ignored;
    if( !is_adjacent_same_z( from, to ) ||
        !tile_is_v1_safe( here, from, ignored ) || !tile_is_v1_safe( here, to, ignored ) ) {
        return false;
    }
    if( here.passable_through( to ) ) {
        return here.valid_move( from, to, false, false, false ) &&
               diagonal_edge_is_clear( here, from, to, ignored ) &&
               here.combined_movecost( from, to ) > 0;
    }
    // Doors are opened by a separate tick.  Diagonal door entry remains
    // forbidden because it cannot establish an unambiguous safe edge.
    return ( from.x() == to.x() || from.y() == to.y() ) &&
           here.open_door( door_opener, to, !here.is_outside( from ), true );
}

bool build_v1_route( npc &route_actor, const tripoint_bub_ms &from,
                     const tripoint_bub_ms &to, const npc *allowed_first,
                     const npc *allowed_second, std::vector<tripoint_bub_ms> &result,
                     const std::optional<tripoint_bub_ms> &forbidden = std::nullopt )
{
    result.clear();
    map &here = get_map();
    if( !tile_is_route_available( here, from, allowed_first, allowed_second ) ||
        !tile_is_route_available( here, to, allowed_first, allowed_second ) ) {
        return false;
    }
    result.push_back( from );
    if( from == to ) {
        return true;
    }

    const auto avoid = [&]( const tripoint_bub_ms &p ) {
        if( forbidden && p == *forbidden ) {
            return true;
        }
        if( p == from || p == to ) {
            return false;
        }
        return !tile_is_route_available( here, p, allowed_first, allowed_second );
    };
    const std::vector<tripoint_bub_ms> vanilla_route = here.route(
                from, pathfinding_target::point( to ),
                route_actor.get_pathfinding_settings( true ), avoid );
    if( vanilla_route.empty() ) {
        return false;
    }

    tripoint_bub_ms previous = from;
    for( const tripoint_bub_ms &next : vanilla_route ) {
        if( route_edge_is_v1_legal( route_actor, here, previous, next ) ) {
            result.push_back( next );
            previous = next;
            continue;
        }

        if( previous.x() == next.x() || previous.y() == next.y() ) {
            return false;
        }
        const std::array<tripoint_bub_ms, 2> middles = {
            tripoint_bub_ms{ next.x(), previous.y(), previous.z() },
            tripoint_bub_ms{ previous.x(), next.y(), previous.z() }
        };
        bool expanded = false;
        for( const tripoint_bub_ms &middle : middles ) {
            if( tile_is_route_available( here, middle, allowed_first, allowed_second ) &&
                route_edge_is_v1_legal( route_actor, here, previous, middle ) &&
                route_edge_is_v1_legal( route_actor, here, middle, next ) ) {
                result.push_back( middle );
                result.push_back( next );
                previous = next;
                expanded = true;
                break;
            }
        }
        if( !expanded ) {
            return false;
        }
    }
    return result.back() == to;
}

void store_route_plan( npc_ai::rescue_state &state, map &here, const route_plan &plan )
{
    state.casualty_path.clear();
    state.rescuer_path.clear();
    state.casualty_path.reserve( plan.casualty_path.size() );
    state.rescuer_path.reserve( plan.rescuer_path.size() );
    for( const tripoint_bub_ms &p : plan.casualty_path ) {
        state.casualty_path.push_back( here.get_abs( p ) );
    }
    for( const tripoint_bub_ms &p : plan.rescuer_path ) {
        state.rescuer_path.push_back( here.get_abs( p ) );
    }
}

bool build_coupled_plan( npc &rescuer, npc &casualty,
                         const tripoint_bub_ms &destination,
                         const tripoint_bub_ms &anchor, route_plan &plan,
                         std::string &reason )
{
    map &here = get_map();
    const tripoint_bub_ms casualty_from = casualty.pos_bub( here );
    if( casualty_from == destination ) {
        reason = "casualty is already at the destination";
        return false;
    }
    if( casualty_from.z() != destination.z() || destination.z() != anchor.z() ) {
        reason = "z-level changes are not supported";
        return false;
    }
    if( !tile_is_route_available( here, destination, &rescuer, &casualty ) ||
        !here.passable_through( destination ) ) {
        reason = "casualty destination is unsafe or impassable";
        return false;
    }
    if( !tile_is_route_available( here, anchor, &rescuer, nullptr ) ||
        !here.passable_through( anchor ) ||
        !edge_is_v1_legal( here, destination, anchor, reason ) ) {
        reason = "rescuer final anchor is no longer safe";
        return false;
    }

    // The last pull must be predecessor -> destination for the casualty while
    // the rescuer moves destination -> anchor.  Constrain the casualty route
    // by that predecessor instead of merely finding any route to destination.
    const tripoint_rel_ms final_delta = anchor - destination;
    const tripoint_bub_ms predecessor = destination - final_delta;
    if( !tile_is_route_available( here, predecessor, &rescuer, &casualty ) ||
        !edge_is_v1_legal( here, predecessor, destination, reason ) ||
        !build_v1_route( rescuer, casualty_from, predecessor, &rescuer, &casualty,
                         plan.casualty_path, destination ) || plan.casualty_path.empty() ||
        std::find( plan.casualty_path.begin(), plan.casualty_path.end(), destination ) !=
        plan.casualty_path.end() ) {
        reason = "no safe casualty route reaches the destination";
        return false;
    }
    plan.casualty_path.push_back( destination );

    plan.anchor = anchor;
    plan.rescuer_path.assign( plan.casualty_path.begin() + 1, plan.casualty_path.end() );
    plan.rescuer_path.push_back( anchor );
    return true;
}

bool choose_initial_plan( npc &rescuer, npc &casualty,
                          const tripoint_bub_ms &destination,
                          route_plan &plan, std::string &reason )
{
    map &here = get_map();
    if( !here.inbounds( destination ) || casualty.posz() != destination.z() ) {
        reason = "casualty destination is outside the loaded z-level";
        return false;
    }
    if( !tile_is_route_available( here, destination, nullptr, nullptr ) ||
        !here.passable_through( destination ) ) {
        reason = "casualty destination is unsafe, occupied, or impassable";
        return false;
    }

    std::vector<tripoint_bub_ms> anchors;
    for( const tripoint_bub_ms &candidate : here.points_in_radius( destination, 1 ) ) {
        if( candidate == destination || candidate.z() != destination.z() ||
            !tile_is_route_available( here, candidate, nullptr, nullptr ) ||
            !here.passable_through( candidate ) ) {
            continue;
        }
        std::string edge_reason;
        if( edge_is_v1_legal( here, destination, candidate, edge_reason ) ) {
            anchors.push_back( candidate );
        }
    }
    std::sort( anchors.begin(), anchors.end(), []( const tripoint_bub_ms &lhs,
    const tripoint_bub_ms &rhs ) {
        if( lhs.y() != rhs.y() ) {
            return lhs.y() < rhs.y();
        }
        return lhs.x() < rhs.x();
    } );
    for( const tripoint_bub_ms &anchor : anchors ) {
        if( build_coupled_plan( rescuer, casualty, destination, anchor, plan, reason ) ) {
            return true;
        }
    }
    reason = anchors.empty() ? "no safe rescuer final anchor exists" : reason;
    return false;
}

bool replan_existing_rescue( npc_ai::rescue_state &state, npc &rescuer,
                             npc &casualty, std::string &reason )
{
    map &here = get_map();
    const tripoint_bub_ms destination = here.get_bub( state.casualty_destination );
    const tripoint_bub_ms anchor = here.get_bub( state.rescuer_final_anchor );
    route_plan plan;
    if( !here.inbounds( destination ) || !here.inbounds( anchor ) ||
        !build_coupled_plan( rescuer, casualty, destination, anchor, plan, reason ) ) {
        return false;
    }
    store_route_plan( state, here, plan );
    return true;
}

bool basic_pair_is_valid( const npc &rescuer, const npc &casualty, std::string &reason )
{
    if( &rescuer == &casualty || npc_key( rescuer ) == npc_key( casualty ) ) {
        reason = "rescuer and casualty must be different NPCs";
        return false;
    }
    if( rescuer.is_dead_state() || casualty.is_dead_state() ) {
        reason = "rescuer or casualty is dead";
        return false;
    }
    if( rescuer.is_fake() || casualty.is_fake() || rescuer.is_hallucination() ||
        casualty.is_hallucination() ) {
        reason = "fake or hallucination NPCs cannot participate";
        return false;
    }
    if( rescuer.in_vehicle || casualty.in_vehicle || rescuer.is_mounted() || casualty.is_mounted() ) {
        reason = "mounted or vehicle occupants are not supported";
        return false;
    }
    if( rescuer.posz() != casualty.posz() ) {
        reason = "rescue participants are on different z-levels";
        return false;
    }
    return true;
}

bool rescuer_can_take_drag_step( const npc &rescuer, std::string &reason )
{
    if( rescuer.has_flag( json_flag_CANNOT_MOVE ) || has_forbidden_rescue_effect( rescuer ) ||
        rescuer.has_effect( effect_narcosis ) || rescuer.in_sleep_state() ||
        rescuer.has_effect( effect_stunned ) || rescuer.has_effect( effect_psi_stunned ) ) {
        reason = "rescuer cannot safely move";
        return false;
    }
    return true;
}

bool validate_drag_geometry( npc &rescuer, npc &casualty,
                             const tripoint_bub_ms &rescuer_dest,
                             std::string &reason )
{
    map &here = get_map();
    const tripoint_bub_ms rescuer_from = rescuer.pos_bub( here );
    const tripoint_bub_ms casualty_from = casualty.pos_bub( here );
    if( rescuer_from.z() != casualty_from.z() || rescuer_dest.z() != rescuer_from.z() ) {
        reason = "z-level changes are not supported";
        return false;
    }
    if( !is_adjacent_same_z( rescuer_from, rescuer_dest ) ) {
        reason = "rescuer destination is not adjacent";
        return false;
    }
    if( rescuer_dest == casualty_from ) {
        reason = "drag step cannot swap the NPCs";
        return false;
    }

    const tripoint_rel_ms delta = rescuer_dest - rescuer_from;
    if( casualty_from + delta != rescuer_from ) {
        reason = "rescuer is not leading the casualty";
        return false;
    }

    creature_tracker &creatures = get_creature_tracker();
    if( creatures.creature_at<npc>( rescuer_from ) != &rescuer ||
        creatures.creature_at<npc>( casualty_from ) != &casualty ) {
        reason = "participant position is inconsistent with the creature tracker";
        return false;
    }
    if( creatures.creature_at( rescuer_dest ) != nullptr ) {
        reason = "rescuer destination is occupied";
        return false;
    }

    if( !edge_is_v1_legal( here, rescuer_from, rescuer_dest, reason ) ||
        !edge_is_v1_legal( here, casualty_from, rescuer_from, reason ) ) {
        return false;
    }
    return true;
}

int drag_move_cost( const npc &rescuer, const npc &casualty,
                    const tripoint_bub_ms &rescuer_from,
                    const tripoint_bub_ms &rescuer_dest, bool &too_heavy )
{
    map &here = get_map();
    const int str_req = std::max( 4, static_cast<int>( casualty.get_weight() / 12_kilogram ) );
    const int strength = rescuer.get_arm_str();
    const int deficit = str_req - strength;
    too_heavy = deficit >= 8;
    if( too_heavy ) {
        return 0;
    }

    const bool diagonal = rescuer_from.x() != rescuer_dest.x() &&
                          rescuer_from.y() != rescuer_dest.y();
    int cost = rescuer.run_cost( here.combined_movecost( rescuer_from, rescuer_dest ), diagonal );
    cost += str_req * 10;
    if( deficit > 0 ) {
        const int furniture_style_penalty = str_req * str_req + 100;
        cost += std::max( 3000, furniture_style_penalty * 10 );
    }
    return std::max( 1, cost );
}

void invalidate_claim_after_inconsistency( const npc &rescuer, const std::string &reason )
{
    npc_ai::cancel_rescue_for( rescuer, reason );
    debugmsg( "NPC rescue movement invariant failed: %s", reason );
}

npc_ai::rescue_state *mutable_rescue( const npc_ai::rescue_id id )
{
    const auto found = rescues.find( id );
    return found == rescues.end() ? nullptr : &found->second;
}

npc *find_active_npc( const int id )
{
    if( g == nullptr ) {
        return nullptr;
    }
    for( npc &candidate : g->all_npcs() ) {
        if( npc_key( candidate ) == id && candidate.is_active() ) {
            return &candidate;
        }
    }
    return nullptr;
}

bool rescuer_is_eligible_for_order( const npc &candidate, const npc &casualty )
{
    return &candidate != &casualty && candidate.is_active() && candidate.is_player_ally() &&
           !candidate.is_dead_state() && !candidate.is_fake() && !candidate.is_hallucination() &&
           !candidate.in_vehicle && !candidate.is_mounted() && !candidate.in_sleep_state() &&
           !candidate.has_activity() && candidate.posz() == casualty.posz() &&
           npc_ai::classify_casualty_mobility( candidate ) ==
           npc_ai::casualty_mobility::can_walk;
}

std::uint64_t create_rescue_goal( const npc &rescuer, const std::string &summary )
{
    if( rescue_goal_factory_override ) {
        return rescue_goal_factory_override( rescuer, summary );
    }
    return npc_ai::begin_goal( rescuer, npc_ai::ai_goal_kind::rescue_casualty,
                               npc_ai::ai_goal_priority::emergency, summary );
}

bool rescue_goal_is_active( const npc_ai::rescue_state &state, const npc &rescuer )
{
    if( state.goal_id == 0 ) {
        return false;
    }
    const std::optional<npc_ai::ai_goal> active = npc_ai::active_goal( rescuer );
    return active && active->id == state.goal_id &&
           active->kind == npc_ai::ai_goal_kind::rescue_casualty;
}

bool validate_live_rescue( const npc_ai::rescue_state &state, npc &rescuer,
                           npc &casualty, std::string &reason )
{
    map &here = get_map();
    if( !basic_pair_is_valid( rescuer, casualty, reason ) ||
        !rescuer.is_active() || !casualty.is_active() ) {
        reason = reason.empty() ? "a rescue participant left the reality bubble" : reason;
        return false;
    }
    if( rescuer.has_activity() || casualty.has_activity() ) {
        reason = "a rescue participant has an incompatible activity";
        return false;
    }
    if( !rescuer_can_take_drag_step( rescuer, reason ) ) {
        return false;
    }
    if( rescuer.posz() != casualty.posz() || rescuer.posz() != state.casualty_destination.z() ) {
        reason = "a rescue participant changed z-level";
        return false;
    }
    if( !here.inbounds( here.get_bub( state.casualty_destination ) ) ||
        !here.inbounds( here.get_bub( state.rescuer_final_anchor ) ) ) {
        reason = "rescue destination left the loaded map";
        return false;
    }
    if( !npc_ai::casualty_allows_ordered_drag( casualty ) ) {
        reason = npc_ai::classify_casualty_mobility( casualty ) ==
                 npc_ai::casualty_mobility::must_not_move ?
                 "casualty must no longer be moved" : "casualty can now walk";
        return false;
    }
    if( !rescue_goal_is_active( state, rescuer ) ) {
        reason = "rescue goal is no longer active";
        return false;
    }
    return true;
}

} // namespace

namespace npc_ai
{

casualty_mobility classify_casualty_mobility( const npc &casualty )
{
    if( has_forbidden_rescue_effect( casualty ) ) {
        return casualty_mobility::must_not_move;
    }
    if( casualty.has_flag( json_flag_CANNOT_MOVE ) || casualty.has_effect( effect_narcosis ) ) {
        return casualty_mobility::needs_drag;
    }

    const bool has_crutches = casualty.get_wielded_item() &&
                              casualty.get_wielded_item()->has_flag( flag_CRUTCHES );
    if( casualty.get_working_leg_count() == 0 && !has_crutches ) {
        return casualty_mobility::needs_drag;
    }
    if( !casualty.enough_working_legs() ||
        casualty.get_modifier( character_modifier_limb_run_cost_mod ) >= 4.0f ) {
        return casualty_mobility::impaired;
    }
    return casualty_mobility::can_walk;
}

bool casualty_allows_ordered_drag( const npc &casualty )
{
    const casualty_mobility mobility = classify_casualty_mobility( casualty );
    return mobility == casualty_mobility::impaired || mobility == casualty_mobility::needs_drag;
}

bool parse_rescue_order( const std::string &player_line )
{
    static constexpr std::array<std::string_view, 5> verbs = {
        "arrastra", "arrastrad", "arrastrar", "drag", "haul"
    };
    return std::any_of( verbs.begin(), verbs.end(), [&]( const std::string_view verb ) {
        return lcmatch( player_line, verb );
    } );
}

std::optional<rescue_id> begin_rescue( npc &rescuer, npc &casualty,
                                       const tripoint_bub_ms &casualty_destination,
                                       std::string *failure_reason )
{
    std::string reason;
    if( !basic_pair_is_valid( rescuer, casualty, reason ) ||
        !rescuer_can_take_drag_step( rescuer, reason ) ||
        !casualty_allows_ordered_drag( casualty ) ) {
        if( reason.empty() ) {
            reason = classify_casualty_mobility( casualty ) == casualty_mobility::must_not_move ?
                     "casualty must not be moved" : "casualty can walk";
        }
        if( failure_reason != nullptr ) {
            *failure_reason = reason;
        }
        return std::nullopt;
    }
    route_plan plan;
    if( !choose_initial_plan( rescuer, casualty, casualty_destination, plan, reason ) ) {
        if( failure_reason != nullptr ) {
            *failure_reason = reason;
        }
        return std::nullopt;
    }

    map &here = get_map();
    const std::optional<rescue_id> claimed = claim_rescue(
                rescuer, casualty, here.get_abs( casualty_destination ),
                here.get_abs( plan.anchor ), &reason );
    if( !claimed ) {
        if( failure_reason != nullptr ) {
            *failure_reason = reason;
        }
        return std::nullopt;
    }

    const std::string summary = string_format( "rescue %s", casualty.get_name() );
    const std::uint64_t goal = create_rescue_goal( rescuer, summary );
    if( goal == 0 ) {
        release_rescue( *claimed, rescue_phase::failed, "could not create rescue goal" );
        if( failure_reason != nullptr ) {
            *failure_reason = "could not create rescue goal";
        }
        return std::nullopt;
    }

    rescue_state *state = mutable_rescue( *claimed );
    if( state == nullptr ) {
        fail_goal( rescuer, goal, "rescue claim disappeared during setup" );
        if( failure_reason != nullptr ) {
            *failure_reason = "rescue claim disappeared during setup";
        }
        return std::nullopt;
    }
    state->goal_id = goal;
    store_route_plan( *state, here, plan );
    return claimed;
}

rescue_command_result execute_rescue_order( const std::vector<npc *> &candidate_rescuers,
        const std::string &player_line, const rescue_destination_selector &select_destination )
{
    rescue_command_result result;
    if( !parse_rescue_order( player_line ) || g == nullptr ) {
        return result;
    }

    std::vector<npc *> named_casualties;
    for( npc &candidate : g->all_npcs() ) {
        if( candidate.is_active() && candidate.is_player_ally() &&
            names_casualty_after_drag_verb( player_line, candidate.get_name() ) ) {
            named_casualties.push_back( &candidate );
        }
    }
    // A physical-transfer verb without a named allied person may refer to an
    // item, so it deliberately falls through to the normal dialogue flow.
    if( named_casualties.empty() ) {
        return result;
    }

    result.handled = true;
    if( named_casualties.size() != 1 ) {
        result.message = _( "I can't tell which casualty you mean." );
        return result;
    }
    result.casualty = named_casualties.front();

    std::vector<npc *> eligible;
    for( npc *candidate : candidate_rescuers ) {
        if( candidate != nullptr && rescuer_is_eligible_for_order( *candidate, *result.casualty ) ) {
            eligible.push_back( candidate );
        }
    }
    std::sort( eligible.begin(), eligible.end(), [&]( const npc *lhs, const npc *rhs ) {
        const int lhs_dist = rl_dist( lhs->pos_bub(), result.casualty->pos_bub() );
        const int rhs_dist = rl_dist( rhs->pos_bub(), result.casualty->pos_bub() );
        return lhs_dist != rhs_dist ? lhs_dist < rhs_dist :
               lhs->getID().get_value() < rhs->getID().get_value();
    } );
    if( eligible.empty() ) {
        result.message = _( "No eligible rescuer can take that order." );
        return result;
    }
    result.rescuer = eligible.front();

    // Once an unambiguous rescuer and casualty have been named, the new
    // explicit order supersedes that rescuer's previous rescue even when this
    // replacement is rejected or the destination selector is cancelled.
    cancel_rescue_for( *result.rescuer, "superseded by a new rescue order" );

    const casualty_mobility mobility = classify_casualty_mobility( *result.casualty );
    if( mobility == casualty_mobility::can_walk ) {
        result.message = string_format( _( "%s can still walk." ), result.casualty->get_name() );
        return result;
    }
    if( mobility == casualty_mobility::must_not_move ) {
        result.message = string_format( _( "%s must not be moved." ), result.casualty->get_name() );
        return result;
    }

    const std::optional<tripoint_bub_ms> destination = select_destination ?
            select_destination() : std::nullopt;
    if( !destination ) {
        result.cancelled = true;
        return result;
    }

    std::string reason;
    const std::optional<rescue_id> started = begin_rescue(
                *result.rescuer, *result.casualty, *destination, &reason );
    if( !started ) {
        result.message = reason;
        return result;
    }

    result.started = true;
    result.id = *started;
    result.message = string_format( _( "I'm going for %s." ), result.casualty->get_name() );
    return result;
}

drag_step_result try_open_drag_door( npc &rescuer, npc &casualty,
                                     const tripoint_bub_ms &rescuer_dest )
{
    drag_step_result result;
    std::string reason;
    if( !basic_pair_is_valid( rescuer, casualty, reason ) ||
        !rescuer_can_take_drag_step( rescuer, reason ) ||
        !casualty_allows_ordered_drag( casualty ) ) {
        result.reason = reason.empty() ? "casualty may not be dragged" : reason;
        return result;
    }

    map &here = get_map();
    const tripoint_bub_ms rescuer_from = rescuer.pos_bub( here );
    const tripoint_bub_ms casualty_from = casualty.pos_bub( here );
    if( rescuer_from.z() != casualty_from.z() ||
        !is_adjacent_same_z( rescuer_from, rescuer_dest ) ||
        !tile_is_v1_safe( here, rescuer_from, reason ) ||
        !tile_is_v1_safe( here, casualty_from, reason ) ||
        !tile_is_v1_safe( here, rescuer_dest, reason ) ) {
        result.reason = reason.empty() ? "invalid door step geometry" : reason;
        return result;
    }
    const tripoint_rel_ms delta = rescuer_dest - rescuer_from;
    if( casualty_from + delta != rescuer_from || rescuer_dest == casualty_from ) {
        result.reason = "rescuer is not leading the casualty";
        return result;
    }
    if( get_creature_tracker().creature_at( rescuer_dest ) != nullptr ) {
        result.reason = "door tile is occupied";
        return result;
    }
    if( !here.open_door( rescuer, rescuer_dest, !here.is_outside( rescuer_from ), true ) ) {
        result.reason = "destination is not an unlocked closed door";
        return result;
    }

    here.open_door( rescuer, rescuer_dest, !here.is_outside( rescuer_from ) );
    rescuer.mod_moves( -rescuer.get_speed() );
    result.outcome = drag_step_outcome::opened_door;
    result.move_cost = rescuer.get_speed();
    return result;
}

drag_step_result try_drag_step( npc &rescuer, npc &casualty,
                                const tripoint_bub_ms &rescuer_dest )
{
    drag_step_result result;
    std::string reason;
    if( !basic_pair_is_valid( rescuer, casualty, reason ) ||
        !rescuer_can_take_drag_step( rescuer, reason ) ) {
        result.reason = reason;
        return result;
    }
    if( !casualty_allows_ordered_drag( casualty ) ) {
        result.reason = classify_casualty_mobility( casualty ) == casualty_mobility::must_not_move ?
                        "casualty must not be moved" : "casualty can walk";
        return result;
    }
    if( !validate_drag_geometry( rescuer, casualty, rescuer_dest, reason ) ) {
        result.reason = reason;
        return result;
    }

    map &here = get_map();
    const tripoint_bub_ms rescuer_from = rescuer.pos_bub( here );
    const tripoint_bub_ms casualty_from = casualty.pos_bub( here );
    bool too_heavy = false;
    const int cost = drag_move_cost( rescuer, casualty, rescuer_from, rescuer_dest, too_heavy );
    if( too_heavy ) {
        result.reason = "casualty is too heavy";
        return result;
    }

    // npc::move_to cannot be isolated for this primitive: it calls move_effects,
    // may redirect a stunned NPC, pushes creatures, opens/closes doors, boards and
    // unboards vehicles, and triggers traps/fields.  All of those are forbidden in
    // rescue V1.  This minimal movement preserves footstep noise,
    // Creature::setpos/on_move, gravity checks, facing, trap/field processing,
    // and the vanilla run_cost calculation.
    rescuer.mod_moves( -cost );
    rescuer.make_footstep_noise();
    rescuer.setpos( here, rescuer_dest, true );
    rescuer.facing = rescuer_from.x() - rescuer_dest.x() < 0 ? FacingDirection::RIGHT :
                     FacingDirection::LEFT;
    if( rescuer.pos_bub( here ) != rescuer_dest ) {
        result.reason = "rescuer did not reach the prevalidated destination";
        invalidate_claim_after_inconsistency( rescuer, result.reason );
        return result;
    }
    here.creature_on_trap( rescuer );
    here.creature_in_field( rescuer );

    casualty.setpos( here, rescuer_from, true );

    creature_tracker &creatures = get_creature_tracker();
    const bool valid_postcondition = rescuer.pos_bub( here ) == rescuer_dest &&
                                     casualty.pos_bub( here ) == rescuer_from &&
                                     rescuer.pos_bub( here ) != casualty.pos_bub( here ) &&
                                     is_adjacent_same_z( rescuer.pos_bub( here ), casualty.pos_bub( here ) ) &&
                                     creatures.creature_at<npc>( rescuer_dest ) == &rescuer &&
                                     creatures.creature_at<npc>( rescuer_from ) == &casualty &&
                                     creatures.creature_at<npc>( casualty_from ) == nullptr;
    if( !valid_postcondition ) {
        result.reason = "post-move creature positions are inconsistent";
        invalidate_claim_after_inconsistency( rescuer, result.reason );
        return result;
    }

    result.outcome = drag_step_outcome::moved;
    result.move_cost = cost;
    return result;
}

std::optional<rescue_id> claim_rescue( const npc &rescuer, const npc &casualty,
                                       const tripoint_abs_ms &casualty_destination,
                                       const tripoint_abs_ms &rescuer_final_anchor,
                                       std::string *failure_reason )
{
    std::string reason;
    if( !basic_pair_is_valid( rescuer, casualty, reason ) ) {
        if( failure_reason != nullptr ) {
            *failure_reason = reason;
        }
        return std::nullopt;
    }
    if( !casualty_allows_ordered_drag( casualty ) ) {
        reason = classify_casualty_mobility( casualty ) == casualty_mobility::must_not_move ?
                 "casualty must not be moved" : "casualty can walk";
        if( failure_reason != nullptr ) {
            *failure_reason = reason;
        }
        return std::nullopt;
    }

    const int rescuer_id = npc_key( rescuer );
    const int casualty_id = npc_key( casualty );
    if( rescue_by_rescuer.count( rescuer_id ) != 0 ) {
        reason = "rescuer already has a rescue claim";
    } else if( rescue_by_casualty.count( casualty_id ) != 0 ) {
        reason = "casualty is already claimed";
    }
    if( !reason.empty() ) {
        if( failure_reason != nullptr ) {
            *failure_reason = reason;
        }
        return std::nullopt;
    }

    rescue_state state;
    state.id = next_rescue_id++;
    state.rescuer_id = rescuer_id;
    state.casualty_id = casualty_id;
    state.casualty_destination = casualty_destination;
    state.rescuer_final_anchor = rescuer_final_anchor;
    state.created_turn = calendar::turn;
    state.last_progress_turn = calendar::turn;

    rescues.emplace( state.id, state );
    rescue_by_rescuer.emplace( rescuer_id, state.id );
    rescue_by_casualty.emplace( casualty_id, state.id );
    return state.id;
}

std::optional<rescue_state> rescue_for_rescuer( const npc &rescuer )
{
    const auto indexed = rescue_by_rescuer.find( npc_key( rescuer ) );
    if( indexed == rescue_by_rescuer.end() ) {
        return std::nullopt;
    }
    const auto found = rescues.find( indexed->second );
    return found == rescues.end() ? std::nullopt : std::optional<rescue_state>( found->second );
}

std::optional<rescue_state> rescue_for_casualty( const npc &casualty )
{
    const auto indexed = rescue_by_casualty.find( npc_key( casualty ) );
    if( indexed == rescue_by_casualty.end() ) {
        return std::nullopt;
    }
    const auto found = rescues.find( indexed->second );
    return found == rescues.end() ? std::nullopt : std::optional<rescue_state>( found->second );
}

rescue_tick_result tick_rescue( npc &rescuer )
{
    const std::optional<rescue_state> snapshot = rescue_for_rescuer( rescuer );
    if( !snapshot ) {
        return rescue_tick_result::not_applicable;
    }
    const rescue_id id = snapshot->id;
    npc *casualty = find_active_npc( snapshot->casualty_id );
    if( casualty == nullptr ) {
        finish_rescue( id, rescue_finish_outcome::cancelled,
                       "casualty left the reality bubble or died" );
        return rescue_tick_result::cancelled;
    }

    rescue_state *state = mutable_rescue( id );
    std::string reason;
    if( state == nullptr || !validate_live_rescue( *state, rescuer, *casualty, reason ) ) {
        finish_rescue( id, rescue_finish_outcome::cancelled,
                       reason.empty() ? "rescue state disappeared" : reason );
        return rescue_tick_result::cancelled;
    }
    if( calendar::turn - state->last_progress_turn > rescue_watchdog_timeout ) {
        finish_rescue( id, rescue_finish_outcome::failed, "rescue made no progress" );
        return rescue_tick_result::cancelled;
    }

    map &here = get_map();
    if( casualty->pos_abs() == state->casualty_destination ) {
        rescuer.move_pause();
        finish_rescue( id, rescue_finish_outcome::completed );
        return rescue_tick_result::completed;
    }
    if( ( state->phase == rescue_phase::attached || state->phase == rescue_phase::dragging ) &&
        !is_adjacent_same_z( rescuer.pos_bub( here ), casualty->pos_bub( here ) ) ) {
        finish_rescue( id, rescue_finish_outcome::cancelled,
                       "attached rescue participants separated" );
        return rescue_tick_result::cancelled;
    }

    if( !replan_existing_rescue( *state, rescuer, *casualty, reason ) ) {
        finish_rescue( id, rescue_finish_outcome::failed, reason );
        return rescue_tick_result::cancelled;
    }
    if( state->casualty_path.size() < 2 ) {
        finish_rescue( id, rescue_finish_outcome::failed, "casualty route is empty" );
        return rescue_tick_result::cancelled;
    }
    const tripoint_bub_ms staging = here.get_bub( state->casualty_path[1] );

    // At every turn in the casualty route, the rescuer must first occupy that
    // casualty's next tile.  A bend therefore returns to positioning instead
    // of attempting an invalid diagonal/swap-like drag from the old lead side.
    if( ( state->phase == rescue_phase::attached ||
          state->phase == rescue_phase::dragging ) &&
        rescuer.pos_bub( here ) != staging ) {
        state->phase = rescue_phase::positioning;
    }

    if( state->phase == rescue_phase::approaching ||
        state->phase == rescue_phase::positioning ) {
        if( rescuer.pos_bub( here ) == staging ) {
            state->phase = rescue_phase::attached;
            state->last_progress_turn = calendar::turn;
            rescuer.move_pause();
            return rescue_tick_result::consumed_turn;
        }

        state->phase = is_adjacent_same_z( rescuer.pos_bub( here ), casualty->pos_bub( here ) ) ?
                       rescue_phase::positioning : rescue_phase::approaching;
        std::vector<tripoint_bub_ms> approach_route;
        if( !build_v1_route( rescuer, rescuer.pos_bub( here ), staging,
                             &rescuer, nullptr, approach_route ) || approach_route.size() < 2 ||
            std::find( approach_route.begin() + 1, approach_route.end(),
                       casualty->pos_bub( here ) ) != approach_route.end() ) {
            finish_rescue( id, rescue_finish_outcome::failed,
                           "no staging route avoids the casualty" );
            return rescue_tick_result::cancelled;
        }

        rescuer.path.assign( approach_route.begin() + 1, approach_route.end() );
        const tripoint_bub_ms before = rescuer.pos_bub( here );
        const int moves_before = rescuer.get_moves();
        rescuer.move_to_next();
        if( rescuer.pos_bub( here ) != before || rescuer.get_moves() != moves_before ) {
            state = mutable_rescue( id );
            if( state != nullptr ) {
                state->last_progress_turn = calendar::turn;
                if( rescuer.pos_bub( here ) == staging ) {
                    state->phase = rescue_phase::attached;
                }
            }
        }
        return rescue_tick_result::consumed_turn;
    }

    if( rescuer.pos_bub( here ) != staging ) {
        finish_rescue( id, rescue_finish_outcome::cancelled,
                       "rescuer is no longer in lead geometry" );
        return rescue_tick_result::cancelled;
    }

    const tripoint_rel_ms pull_delta = staging - casualty->pos_bub( here );
    const tripoint_bub_ms next_rescuer_tile = staging + pull_delta;
    if( !here.passable_through( next_rescuer_tile ) ) {
        const drag_step_result opened = try_open_drag_door( rescuer, *casualty,
                                        next_rescuer_tile );
        if( opened.outcome == drag_step_outcome::opened_door ) {
            state = mutable_rescue( id );
            if( state != nullptr ) {
                state->last_progress_turn = calendar::turn;
            }
            return rescue_tick_result::consumed_turn;
        }
    }

    const drag_step_result step = try_drag_step( rescuer, *casualty, next_rescuer_tile );
    if( step.outcome != drag_step_outcome::moved ) {
        finish_rescue( id, rescue_finish_outcome::failed,
                       step.reason.empty() ? "drag step failed" : step.reason );
        return rescue_tick_result::cancelled;
    }

    state = mutable_rescue( id );
    if( state != nullptr ) {
        state->phase = rescue_phase::dragging;
        state->last_progress_turn = calendar::turn;
    }
    if( casualty->pos_abs() == snapshot->casualty_destination ) {
        finish_rescue( id, rescue_finish_outcome::completed );
        return rescue_tick_result::completed;
    }
    return rescue_tick_result::consumed_turn;
}

bool consume_linked_casualty_turn( npc &casualty )
{
    const auto indexed = rescue_by_casualty.find( npc_key( casualty ) );
    if( indexed == rescue_by_casualty.end() ) {
        return false;
    }
    rescue_state *state = mutable_rescue( indexed->second );
    if( state == nullptr ) {
        rescue_by_casualty.erase( indexed );
        return false;
    }
    const rescue_id id = state->id;
    const bool linked = state->phase == rescue_phase::attached ||
                        state->phase == rescue_phase::dragging;
    npc *rescuer = find_active_npc( state->rescuer_id );
    std::string reason;
    if( rescuer == nullptr || !validate_live_rescue( *state, *rescuer, casualty, reason ) ||
        ( linked && !is_adjacent_same_z( rescuer->pos_bub(), casualty.pos_bub() ) ) ) {
        finish_rescue( id, rescue_finish_outcome::cancelled,
                       reason.empty() ? "linked rescue became invalid" : reason );
        return false;
    }
    if( !linked ) {
        return false;
    }
    casualty.move_pause();
    return true;
}

bool finish_rescue( const rescue_id id, const rescue_finish_outcome outcome,
                    const std::string &reason )
{
    rescue_state *found = mutable_rescue( id );
    if( found == nullptr ) {
        return false;
    }
    found->phase = outcome == rescue_finish_outcome::completed ? rescue_phase::complete :
                   outcome == rescue_finish_outcome::failed ? rescue_phase::failed :
                   rescue_phase::cancelled;
    found->failure_reason = reason;
    const rescue_state state = *found;
    npc *rescuer = find_active_npc( state.rescuer_id );
    if( rescuer == nullptr && g != nullptr ) {
        rescuer = g->find_npc( character_id( state.rescuer_id ) );
    }
    if( rescuer != nullptr && state.goal_id != 0 ) {
        if( outcome == rescue_finish_outcome::completed ) {
            complete_goal( *rescuer, state.goal_id );
        } else {
            fail_goal( *rescuer, state.goal_id,
                       reason.empty() ? "rescue cancelled" : reason );
        }
    }
    return release_rescue( id, state.phase, reason );
}

bool release_rescue( const rescue_id id, const rescue_phase, const std::string & )
{
    const auto found = rescues.find( id );
    if( found == rescues.end() ) {
        return false;
    }
    rescue_by_rescuer.erase( found->second.rescuer_id );
    rescue_by_casualty.erase( found->second.casualty_id );
    rescues.erase( found );
    return true;
}

bool cancel_rescue_for( const npc &participant, const std::string &reason )
{
    const int id = npc_key( participant );
    auto found = rescue_by_rescuer.find( id );
    if( found != rescue_by_rescuer.end() ) {
        const rescue_id rescue = found->second;
        const rescue_state *state = mutable_rescue( rescue );
        return state != nullptr && state->goal_id != 0 ?
               finish_rescue( rescue, rescue_finish_outcome::cancelled, reason ) :
               release_rescue( rescue, rescue_phase::cancelled, reason );
    }
    const auto casualty_found = rescue_by_casualty.find( id );
    if( casualty_found == rescue_by_casualty.end() ) {
        return false;
    }
    const rescue_id rescue = casualty_found->second;
    const rescue_state *state = mutable_rescue( rescue );
    return state != nullptr && state->goal_id != 0 ?
           finish_rescue( rescue, rescue_finish_outcome::cancelled, reason ) :
           release_rescue( rescue, rescue_phase::cancelled, reason );
}

std::size_t cancel_rescues_for_new_order( const std::vector<npc *> &targets,
        const std::string &reason )
{
    std::size_t cancelled = 0;
    for( npc *target : targets ) {
        if( target != nullptr && cancel_rescue_for( *target, reason ) ) {
            ++cancelled;
        }
    }
    return cancelled;
}

void set_rescue_goal_factory_for_test( rescue_goal_factory factory )
{
    rescue_goal_factory_override = std::move( factory );
}

void clear_rescues_for_test()
{
    reset_all_rescues();
}

void reset_all_rescues()
{
    rescues.clear();
    rescue_by_rescuer.clear();
    rescue_by_casualty.clear();
    next_rescue_id = 1;
    rescue_goal_factory_override = rescue_goal_factory();
}

} // namespace npc_ai
