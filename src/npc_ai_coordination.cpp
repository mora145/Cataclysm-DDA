#include "npc_ai_coordination.h"

#include <algorithm>
#include <unordered_map>

#include "game.h"
#include "map.h"
#include "npc.h"
#include "point.h"
#include "type_id.h"
#include "units.h"

namespace
{

static const skill_id skill_firstaid( "firstaid" );
static const skill_id skill_mechanics( "mechanics" );
static const skill_id skill_fabrication( "fabrication" );
static const skill_id skill_gun( "gun" );
static const skill_id skill_melee( "melee" );

std::unordered_map<int, npc_ai::delegated_assignment> assignments;

int npc_key( const npc &who )
{
    return who.getID().get_value();
}

int available_carry_score( const npc &who )
{
    const int capacity = units::to_gram( who.weight_capacity() );
    const int carried = units::to_gram( who.weight_carried() );
    return std::clamp( ( capacity - carried ) / 5000, -10, 20 );
}

int task_score( const npc &who, const npc_ai::cooperative_task_kind task )
{
    const int strength = who.get_str();
    const int intelligence = who.get_int();
    const int carry = available_carry_score( who );
    switch( task ) {
        case npc_ai::cooperative_task_kind::logistics:
            return 3 * who.get_skill_level( skill_fabrication ) + intelligence + carry +
                   who.personality.collector;
        case npc_ai::cooperative_task_kind::heavy_transport:
            return 3 * strength + 2 * carry;
        case npc_ai::cooperative_task_kind::sorting:
            return 2 * who.get_skill_level( skill_fabrication ) + intelligence +
                   2 * who.personality.collector;
        case npc_ai::cooperative_task_kind::medical:
            return 5 * who.get_skill_level( skill_firstaid ) + intelligence +
                   2 * who.personality.altruism;
        case npc_ai::cooperative_task_kind::guard:
            return 3 * std::max( who.get_skill_level( skill_gun ),
                                 who.get_skill_level( skill_melee ) ) +
                   2 * who.personality.bravery + who.personality.aggression;
        case npc_ai::cooperative_task_kind::mechanics:
            return 5 * who.get_skill_level( skill_mechanics ) +
                   2 * intelligence + who.get_skill_level( skill_fabrication );
        case npc_ai::cooperative_task_kind::construction:
            return 4 * who.get_skill_level( skill_fabrication ) + strength + carry;
    }
    return 0;
}

} // namespace

namespace npc_ai
{

helper_evaluation evaluate_helper( const npc &requester, npc &candidate,
                                   const cooperative_task_kind task, const int max_range )
{
    helper_evaluation result;
    result.candidate = &candidate;
    if( &requester == &candidate ) {
        result.rejection_reason = "self";
        return result;
    }
    if( !candidate.is_active() || !candidate.is_player_ally() || candidate.is_enemy() ) {
        result.rejection_reason = "not an active ally";
        return result;
    }
    map &here = get_map();
    if( requester.posz() != candidate.posz() ||
        rl_dist( requester.pos_bub( here ), candidate.pos_bub( here ) ) > max_range ||
        !requester.sees( here, candidate ) ) {
        result.rejection_reason = "not currently reachable for coordination";
        return result;
    }
    if( candidate.has_activity() || candidate.has_companion_mission() ) {
        result.rejection_reason = "already working";
        return result;
    }
    if( assignments.find( npc_key( candidate ) ) != assignments.end() ) {
        result.rejection_reason = "already delegated";
        return result;
    }
    if( const std::optional<ai_goal> current = active_goal( candidate );
        current && current->priority >= ai_goal_priority::high ) {
        result.rejection_reason = "higher priority goal";
        return result;
    }
    if( candidate.get_stamina() < candidate.get_stamina_max() / 3 ||
        candidate.hp_percentage() < 50 || candidate.get_pain() >= 40 ) {
        result.rejection_reason = "not physically fit for extra work";
        return result;
    }

    result.eligible = true;
    result.suitability = task_score( candidate, task );
    return result;
}

std::vector<helper_evaluation> rank_nearby_helpers( const npc &requester,
        const cooperative_task_kind task, const int max_range )
{
    std::vector<helper_evaluation> result;
    for( npc &candidate : g->all_npcs() ) {
        helper_evaluation evaluation = evaluate_helper( requester, candidate, task, max_range );
        if( evaluation.eligible ) {
            result.push_back( std::move( evaluation ) );
        }
    }
    std::sort( result.begin(), result.end(), []( const helper_evaluation &lhs,
    const helper_evaluation &rhs ) {
        if( lhs.suitability != rhs.suitability ) {
            return lhs.suitability > rhs.suitability;
        }
        return lhs.candidate->getID().get_value() < rhs.candidate->getID().get_value();
    } );
    return result;
}

npc *select_best_helper( const npc &requester, const cooperative_task_kind task,
                         const int max_range )
{
    std::vector<helper_evaluation> candidates = rank_nearby_helpers( requester, task, max_range );
    return candidates.empty() ? nullptr : candidates.front().candidate;
}

std::optional<delegated_assignment> delegate_to_helper( const npc &requester, npc &helper,
        const cooperative_task_kind task, const ai_goal_id requester_goal,
        const std::string &summary )
{
    const helper_evaluation evaluation = evaluate_helper( requester, helper, task );
    if( !evaluation.eligible ) {
        return std::nullopt;
    }

    delegated_assignment assignment;
    assignment.requester_id = npc_key( requester );
    assignment.helper_id = npc_key( helper );
    assignment.task = task;
    assignment.requester_goal = requester_goal;
    assignment.summary = summary;
    assignment.helper_goal = begin_goal( helper, ai_goal_kind::assist_ally,
                                         ai_goal_priority::normal, summary, requester_goal );
    assignments.emplace( assignment.helper_id, assignment );
    return assignment;
}

std::optional<delegated_assignment> assignment_for( const npc &helper )
{
    const auto found = assignments.find( npc_key( helper ) );
    return found == assignments.end() ? std::nullopt :
           std::optional<delegated_assignment>( found->second );
}

bool complete_assignment( const npc &helper )
{
    const auto found = assignments.find( npc_key( helper ) );
    if( found == assignments.end() ) {
        return false;
    }
    complete_goal( helper, found->second.helper_goal );
    assignments.erase( found );
    return true;
}

bool fail_assignment( const npc &helper, const std::string &reason )
{
    const auto found = assignments.find( npc_key( helper ) );
    if( found == assignments.end() ) {
        return false;
    }
    fail_goal( helper, found->second.helper_goal, reason );
    assignments.erase( found );
    return true;
}

void clear_assignments_for_test()
{
    reset_all_assignments();
}

void reset_all_assignments()
{
    assignments.clear();
}

} // namespace npc_ai
