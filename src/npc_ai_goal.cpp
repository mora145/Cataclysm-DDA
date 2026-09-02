#include "npc_ai_goal.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "npc.h"

namespace
{

std::unordered_map<int, std::vector<npc_ai::ai_goal>> goals;
npc_ai::ai_goal_id next_goal_id = 1;

int npc_key( const npc &who )
{
    return who.getID().get_value();
}

void resume_latest_goal( std::vector<npc_ai::ai_goal> &records )
{
    for( auto iter = records.rbegin(); iter != records.rend(); ++iter ) {
        if( iter->status == npc_ai::ai_goal_status::suspended ) {
            iter->status = npc_ai::ai_goal_status::active;
            return;
        }
    }
    const auto pending = std::max_element( records.begin(), records.end(),
    []( const npc_ai::ai_goal & lhs, const npc_ai::ai_goal & rhs ) {
        const int lhs_priority = lhs.status == npc_ai::ai_goal_status::pending ?
                                 static_cast<int>( lhs.priority ) : -1;
        const int rhs_priority = rhs.status == npc_ai::ai_goal_status::pending ?
                                 static_cast<int>( rhs.priority ) : -1;
        return lhs_priority < rhs_priority;
    } );
    if( pending != records.end() && pending->status == npc_ai::ai_goal_status::pending ) {
        pending->status = npc_ai::ai_goal_status::active;
    }
}

} // namespace

namespace npc_ai
{

ai_goal_id begin_goal( const npc &who, const ai_goal_kind kind,
                       const ai_goal_priority priority, const std::string &summary,
                       const ai_goal_id parent_id )
{
    std::vector<ai_goal> &records = goals[npc_key( who )];
    auto current = std::find_if( records.begin(), records.end(), []( const ai_goal &goal ) {
        return goal.status == ai_goal_status::active;
    } );

    ai_goal goal;
    goal.id = next_goal_id++;
    goal.parent_id = parent_id;
    goal.kind = kind;
    goal.priority = priority;
    goal.summary = summary;

    if( current == records.end() ) {
        goal.status = ai_goal_status::active;
    } else if( static_cast<int>( priority ) >= static_cast<int>( current->priority ) ) {
        current->status = ai_goal_status::suspended;
        goal.status = ai_goal_status::active;
    } else {
        goal.status = ai_goal_status::pending;
    }
    records.push_back( std::move( goal ) );
    return records.back().id;
}

bool complete_goal( const npc &who, const ai_goal_id id )
{
    std::vector<ai_goal> &records = goals[npc_key( who )];
    const auto found = std::find_if( records.begin(), records.end(), [&]( const ai_goal &goal ) {
        return goal.id == id;
    } );
    if( found == records.end() || found->status == ai_goal_status::completed ||
        found->status == ai_goal_status::failed ) {
        return false;
    }
    const bool was_active = found->status == ai_goal_status::active;
    found->status = ai_goal_status::completed;
    if( was_active ) {
        resume_latest_goal( records );
    }
    return true;
}

bool fail_goal( const npc &who, const ai_goal_id id, const std::string &reason )
{
    std::vector<ai_goal> &records = goals[npc_key( who )];
    const auto found = std::find_if( records.begin(), records.end(), [&]( const ai_goal &goal ) {
        return goal.id == id;
    } );
    if( found == records.end() || found->status == ai_goal_status::completed ||
        found->status == ai_goal_status::failed ) {
        return false;
    }
    const bool was_active = found->status == ai_goal_status::active;
    found->status = ai_goal_status::failed;
    found->failure_reason = reason;
    if( was_active ) {
        resume_latest_goal( records );
    }
    return true;
}

std::optional<ai_goal> active_goal( const npc &who )
{
    const auto found_records = goals.find( npc_key( who ) );
    if( found_records == goals.end() ) {
        return std::nullopt;
    }
    const auto found = std::find_if( found_records->second.begin(), found_records->second.end(),
    []( const ai_goal & goal ) {
        return goal.status == ai_goal_status::active;
    } );
    return found == found_records->second.end() ? std::nullopt : std::optional<ai_goal>( *found );
}

std::vector<ai_goal> goal_history( const npc &who )
{
    const auto found = goals.find( npc_key( who ) );
    return found == goals.end() ? std::vector<ai_goal>() : found->second;
}

void clear_goals_for_test( const npc &who )
{
    goals.erase( npc_key( who ) );
}

void reset_all_goals()
{
    goals.clear();
    next_goal_id = 1;
}

} // namespace npc_ai
