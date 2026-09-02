#include "npc_ai_climbing.h"
#include "npc_ai_profiler.h"

#include <algorithm>
#include <cmath>

#include "bodypart.h"
#include "character_modifier.h"
#include "climbing.h"
#include "creature_tracker.h"
#include "game.h"
#include "game_constants.h"
#include "item.h"
#include "item_location.h"
#include "map.h"
#include "messages.h"
#include "npc.h"
#include "rng.h"
#include "translations.h"
#include "type_id.h"

namespace
{

static const character_modifier_id character_modifier_slip_prevent_mod( "slip_prevent_mod" );
static const climbing_aid_id climbing_aid_furn_climbable( "furn_CLIMBABLE" );
static const proficiency_id proficiency_parkour( "prof_parkour" );
static const trait_id trait_bad_knees( "BADKNEES" );

float npc_slip_chance( const npc &who )
{
    float slip = 100.0f;
    if( who.has_proficiency( proficiency_parkour ) ) {
        slip /= 2.0f;
    }
    if( who.has_trait( trait_bad_knees ) ) {
        slip *= 2.0f;
    }

    float wet_penalty = 1.0f;
    for( const bodypart_id &bp : who.get_all_body_parts_of_type( body_part_type::type::foot,
            get_body_part_flags::primary_type ) ) {
        if( who.get_part_wetness( bp ) > 0 ) {
            wet_penalty += who.get_part( bp )->get_wetness_percentage() / 2.0f;
        }
    }
    for( const bodypart_id &bp : who.get_all_body_parts_of_type( body_part_type::type::hand,
            get_body_part_flags::primary_type ) ) {
        if( who.get_part_wetness( bp ) > 0 ) {
            wet_penalty += who.get_part( bp )->get_wetness_percentage() / 2.0f;
        }
    }
    slip *= wet_penalty;
    slip /= std::max( 1, who.get_dex() + who.get_str() );

    const float limb_modifier = who.get_modifier( character_modifier_slip_prevent_mod );
    if( limb_modifier <= 0.0f ) {
        return 100.0f;
    }
    slip /= limb_modifier;

    const double capacity = units::to_gram( who.weight_capacity() );
    const double weight_ratio = capacity > 0.0 ?
                                units::to_gram( who.weight_carried() ) / capacity : 2.0;
    slip += static_cast<float>( 8.0 * weight_ratio );

    const float stamina_ratio = static_cast<float>( who.get_stamina() ) /
                                std::max( 1, who.get_stamina_max() );
    if( stamina_ratio < 0.8f ) {
        slip /= std::max( stamina_ratio, 0.1f );
    }

    slip += climbing_aid_furn_climbable.obj().slip_chance_mod;
    return std::clamp( slip, 0.0f, 100.0f );
}

bool is_geometric_climb( const map &here, const tripoint_bub_ms &from,
                         const tripoint_bub_ms &to )
{
    if( std::abs( to.z() - from.z() ) != 1 ||
        std::abs( to.x() - from.x() ) > 1 || std::abs( to.y() - from.y() ) > 1 ) {
        return false;
    }

    if( to.z() > from.z() ) {
        bool aid_nearby = false;
        for( const tripoint_bub_ms &p : here.points_in_radius( from, 1 ) ) {
            if( here.has_flag( ter_furn_flag::TFLAG_CLIMBABLE, p ) ) {
                aid_nearby = true;
                break;
            }
        }
        return aid_nearby && !here.has_flag( ter_furn_flag::TFLAG_GOES_UP, from );
    }
    if( !here.has_flag( ter_furn_flag::TFLAG_CLIMBABLE, from ) ||
        !here.has_flag( ter_furn_flag::TFLAG_CLIMBABLE, to ) ||
        here.has_flag( ter_furn_flag::TFLAG_GOES_DOWN, from ) ) {
        return false;
    }

    if( from.xy() == to.xy() ) {
        return true;
    }

    const tripoint_bub_ms upper_edge( to.xy(), from.z() );
    const climbing_aid::fall_scan fall( upper_edge );
    return here.is_open_air( upper_edge ) &&
           here.valid_move( from, upper_edge, false, true ) &&
           fall.height == 1 && fall.pos_bottom() == to;
}

} // namespace

namespace npc_ai
{

climb_assessment assess_climb( const npc &who, const tripoint_bub_ms &from,
                               const tripoint_bub_ms &to )
{
    climb_assessment result;
    map &here = get_map();
    if( !is_geometric_climb( here, from, to ) ) {
        return result;
    }

    result.vertical_climb = true;
    if( !here.inbounds( to ) || !here.passable_through( to ) || !here.has_floor_or_water( to ) ||
        g->is_dangerous_tile( to ) || get_creature_tracker().creature_at( to ) ) {
        result.refusal = climb_refusal::blocked_landing;
        return result;
    }
    if( who.get_working_arm_count() < 1 ) {
        result.refusal = climb_refusal::damaged_arms;
        return result;
    }
    if( who.get_dex() <= 1 ) {
        result.refusal = climb_refusal::excessive_risk;
        return result;
    }
    if( who.get_stamina() < who.get_stamina_max() / 4 ) {
        result.refusal = climb_refusal::exhausted;
        return result;
    }
    if( who.weight_carried() > who.weight_capacity() ) {
        result.refusal = climb_refusal::overloaded;
        return result;
    }

    if( to.z() > from.z() ) {
        const tripoint_bub_ms directly_above = from + tripoint::above;
        if( here.has_floor_or_support( directly_above ) ||
            who.climbing_cost( from, directly_above ) <= 0 ) {
            result.refusal = climb_refusal::invalid_transition;
            return result;
        }
        result.move_cost = who.climbing_cost( from, directly_above ) + 500;
    } else {
        result.move_cost = 50 + here.climb_difficulty( to ) * 100 + 500;
    }

    result.slip_chance = npc_slip_chance( who );
    if( result.slip_chance > 35.0f ) {
        result.refusal = climb_refusal::excessive_risk;
        return result;
    }

    result.possible = true;
    result.refusal = climb_refusal::none;
    return result;
}

climb_attempt_result attempt_climb( npc &who, const tripoint_bub_ms &from,
                                    const tripoint_bub_ms &to )
{
    scoped_profile profile( profile_subsystem::climbing_pathfinding );
    climb_assessment assessment = assess_climb( who, from, to );
    if( !assessment.vertical_climb ) {
        return climb_attempt_result::not_a_climb;
    }
    if( !assessment.possible ) {
        add_msg_if_player_sees( who, m_warning, "%s",
                                climb_refusal_message( assessment.refusal ) );
        return climb_attempt_result::refused;
    }

    const item_location weapon = who.get_wielded_item();
    if( weapon && weapon->is_two_handed( who ) && !who.unwield() ) {
        add_msg_if_player_sees( who, m_warning, "%s",
                                climb_refusal_message( climb_refusal::cannot_stow_weapon ) );
        return climb_attempt_result::refused;
    }

    who.set_activity_level( EXTRA_EXERCISE );
    who.mod_moves( -assessment.move_cost );
    who.burn_energy_all( -assessment.move_cost );
    if( x_in_y( assessment.slip_chance, 100.0f ) ) {
        add_msg_if_player_sees( who, m_warning, _( "%s slips while climbing." ), who.disp_name() );
        return climb_attempt_result::slipped;
    }

    add_msg_if_player_sees( who, m_neutral, _( "%s climbs to another level." ), who.disp_name() );
    return climb_attempt_result::succeeded;
}

std::string climb_refusal_message( const climb_refusal reason )
{
    switch( reason ) {
        case climb_refusal::blocked_landing:
            return _( "The climbing destination is blocked." );
        case climb_refusal::damaged_arms:
            return _( "I can't climb with my arms in this condition." );
        case climb_refusal::exhausted:
            return _( "I am too exhausted to climb safely." );
        case climb_refusal::overloaded:
            return _( "I can't climb while carrying all this weight." );
        case climb_refusal::excessive_risk:
            return _( "That climb is too dangerous for me right now." );
        case climb_refusal::cannot_stow_weapon:
            return _( "I can't put my weapon away to climb." );
        case climb_refusal::invalid_transition:
        case climb_refusal::none:
            return _( "I can't climb there." );
    }
    return _( "I can't climb there." );
}

} // namespace npc_ai
