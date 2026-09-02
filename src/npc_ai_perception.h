#pragma once
#ifndef CATA_SRC_NPC_AI_PERCEPTION_H
#define CATA_SRC_NPC_AI_PERCEPTION_H

#include <string>
#include <vector>

class npc;

namespace npc_ai
{

enum class sensory_knowledge {
    unknown,
    remembered,
    currently_perceived
};

struct sensory_bool {
    sensory_knowledge knowledge = sensory_knowledge::unknown;
    bool value = false;
};

enum class sensory_door_state {
    not_a_door,
    open,
    closed
};

struct sensory_field_observation {
    std::string id;
    std::string name;
    int intensity = 0;
    bool dangerous = false;
    bool fire = false;
    bool blood = false;
    bool gore = false;
    bool smoke = false;
};

struct sensory_item_observation {
    std::string id;
    std::string name;
    std::string kind;
    bool corpse = false;
    bool casing = false;
    bool weapon = false;
};

struct sensory_spatial_relationship {
    std::string subject_kind;
    std::string subject_id;
    std::string subject_name;
    int subject_dx = 0;
    int subject_dy = 0;
    int subject_dz = 0;
    std::string relation;
    std::string object_kind;
    std::string object_id;
    std::string object_name;
    int object_dx = 0;
    int object_dy = 0;
    int object_dz = 0;
};

struct sensory_trap_observation {
    sensory_knowledge knowledge = sensory_knowledge::unknown;
    std::string id;
    std::string name;
};

struct sensory_vehicle_observation {
    sensory_knowledge knowledge = sensory_knowledge::unknown;
    std::string name;
    std::string part_name;
    bool part_broken = false;
    bool moving = false;
};

struct sensory_tile_observation {
    sensory_knowledge knowledge = sensory_knowledge::currently_perceived;
    int dx = 0;
    int dy = 0;
    int dz = 0;
    int distance = 0;

    std::string terrain_id;
    std::string terrain_name;
    std::string furniture_id;
    std::string furniture_name;

    bool outside = false;
    bool passable = false;
    bool window = false;
    sensory_door_state door = sensory_door_state::not_a_door;

    bool goes_up = false;
    bool goes_down = false;
    bool ladder = false;
    bool climbable = false;
    bool ramp = false;
    bool fire_container = false;

    sensory_bool fire;
    int fire_intensity = 0;
    bool dangerous_field = false;
    std::vector<sensory_field_observation> fields;
    std::vector<sensory_item_observation> items;
    sensory_trap_observation trap;
    sensory_vehicle_observation vehicle;
};

struct sensory_creature_observation {
    sensory_knowledge knowledge = sensory_knowledge::currently_perceived;
    std::string name;
    std::string attitude;
    std::string held_item;
    int dx = 0;
    int dy = 0;
    int dz = 0;
    int distance = 0;
    bool player = false;
    bool npc = false;
    bool hostile = false;
};

struct npc_sensory_snapshot {
    int self_x = 0;
    int self_y = 0;
    int self_z = 0;
    std::vector<sensory_creature_observation> creatures;
    std::vector<sensory_tile_observation> tiles;
    std::vector<sensory_spatial_relationship> relationships;

    const sensory_tile_observation *current_tile_at( int dx, int dy, int dz ) const;
    sensory_bool current_fire_at( int dx, int dy, int dz ) const;
};

npc_sensory_snapshot build_sensory_snapshot( const npc &who, int tile_scan_radius = -1 );
std::string render_sensory_snapshot( const npc_sensory_snapshot &snapshot,
                                     bool detailed_scene = false );
std::string build_sensory_context( const npc &who, bool detailed_scene = false );
std::string build_perception_context( const npc &who, bool detailed_scene = false );

} // namespace npc_ai

#endif // CATA_SRC_NPC_AI_PERCEPTION_H
