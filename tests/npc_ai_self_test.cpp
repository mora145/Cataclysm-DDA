#include "cata_catch.h"

#include <algorithm>
#include <string>

#include "bodypart.h"
#include "calendar.h"
#include "effect.h"
#include "item.h"
#include "item_group.h"
#include "map_helpers.h"
#include "npc.h"
#include "npc_ai_context.h"
#include "npc_ai_profiler.h"
#include "npc_ai_self.h"
#include "player_helpers.h"
#include "point.h"
#include "type_id.h"

namespace
{

static const item_group_id group_bottle_water( "test_bottle_water" );
static const itype_id itype_backpack( "backpack" );
static const itype_id itype_knife_hunting( "knife_hunting" );
static const itype_id itype_match( "match" );
static const itype_id itype_matches( "matches" );
static const itype_id itype_sandwich( "sandwich_cheese_grilled" );
static const efftype_id effect_bleed( "bleed" );

struct restore_self_profiling {
    ~restore_self_profiling() {
        npc_ai::set_profiling_enabled_for_test( false );
        npc_ai::reset_profile();
    }
};

npc &prepare_self_observer()
{
    clear_map();
    npc &who = spawn_npc( point_bub_ms{ 65, 60 }, "test_talker" );
    clear_character( who );
    who.worn.wear_item( who, item( itype_backpack, calendar::turn ), false, false );
    return who;
}

void add_loaded_matches( npc &who )
{
    item matches( itype_matches, calendar::turn );
    matches.ammo_set( itype_match, 5 );
    REQUIRE( who.i_add( matches ) );
}

} // namespace

TEST_CASE( "npc_ai_self_snapshot_separates_hunger_from_food_availability",
           "[npc_ai][npc_ai_self]" )
{
    npc &who = prepare_self_observer();
    who.set_hunger( 500 );

    SECTION( "hungry without food" ) {
        const npc_ai::npc_self_snapshot snapshot = npc_ai::build_self_snapshot( who );

        CHECK( snapshot.hunger.active );
        CHECK( snapshot.hunger.value == who.get_hunger() );
        CHECK_FALSE( snapshot.usable_food.available );
    }

    SECTION( "hungry with usable food" ) {
        REQUIRE( who.i_add( item( itype_sandwich, calendar::turn ) ) );
        const npc_ai::npc_self_snapshot snapshot = npc_ai::build_self_snapshot( who );

        CHECK( snapshot.hunger.active );
        CHECK( snapshot.usable_food.available );
        CHECK( snapshot.usable_food.item_count >= 1 );
        CHECK( std::find( snapshot.usable_food.item_ids.begin(), snapshot.usable_food.item_ids.end(),
                         itype_sandwich.str() ) != snapshot.usable_food.item_ids.end() );

        const std::string rendered = npc_ai::render_self_snapshot( snapshot );
        CHECK( rendered.find( "hambre={valor=" + std::to_string( who.get_hunger() ) +
                              "; necesidad=true}" ) != std::string::npos );
        CHECK( rendered.find( "comida_utilizable={disponible=true" ) != std::string::npos );
    }
}

TEST_CASE( "npc_ai_self_snapshot_separates_thirst_from_potable_drink_availability",
           "[npc_ai][npc_ai_self]" )
{
    npc &who = prepare_self_observer();
    who.set_thirst( 700 );

    SECTION( "thirsty without drink" ) {
        const npc_ai::npc_self_snapshot snapshot = npc_ai::build_self_snapshot( who );

        CHECK( snapshot.thirst.active );
        CHECK_FALSE( snapshot.potable_drink.available );
    }

    SECTION( "thirsty with potable water" ) {
        const item_group::ItemList water = item_group::items_from( group_bottle_water );
        REQUIRE( water.size() == 1 );
        REQUIRE( who.i_add( water.front() ) );

        const npc_ai::npc_self_snapshot snapshot = npc_ai::build_self_snapshot( who );

        CHECK( snapshot.thirst.active );
        CHECK( snapshot.potable_drink.available );
        CHECK( snapshot.potable_drink.item_count >= 1 );

        const std::string rendered = npc_ai::render_self_snapshot( snapshot );
        CHECK( rendered.find( "sed={valor=" + std::to_string( who.get_thirst() ) +
                              "; necesidad=true}" ) != std::string::npos );
        CHECK( rendered.find( "bebida_potable={disponible=true" ) != std::string::npos );
    }
}

TEST_CASE( "npc_ai_self_snapshot_only_reports_a_usable_firestarter",
           "[npc_ai][npc_ai_self]" )
{
    npc &who = prepare_self_observer();

    SECTION( "empty matches" ) {
        item matches( itype_matches, calendar::turn );
        matches.ammo_unset();
        REQUIRE( who.i_add( matches ) );

        const npc_ai::npc_self_snapshot snapshot = npc_ai::build_self_snapshot( who );
        CHECK_FALSE( snapshot.firestarters.available );
    }

    SECTION( "loaded matches" ) {
        add_loaded_matches( who );

        const npc_ai::npc_self_snapshot snapshot = npc_ai::build_self_snapshot( who );
        CHECK( snapshot.firestarters.available );
        CHECK( snapshot.firestarters.item_count >= 1 );
    }
}

TEST_CASE( "npc_ai_current_self_queries_override_conversation_memory",
           "[npc_ai][npc_ai_self]" )
{
    CHECK( npc_ai::is_current_self_query( "Como te sientes ahora?" ) );
    CHECK( npc_ai::is_current_self_query( "Tienes sed?" ) );
    CHECK( npc_ai::is_current_self_query( "Llevas agua?" ) );
    CHECK_FALSE( npc_ai::is_current_self_query( "Recuerdas como te sientes normalmente?" ) );
}

TEST_CASE( "npc_ai_current_self_query_does_not_scan_the_scene",
           "[npc_ai][npc_ai_self][npc_ai_performance]" )
{
    restore_self_profiling restore;
    npc &who = prepare_self_observer();
    npc_ai::set_profiling_enabled_for_test( true );
    npc_ai::reset_profile();

    const std::string prompt = npc_ai::build_npc_prompt( who, "Como estas?" );
    const npc_ai::profile_report report = npc_ai::profile_snapshot();

    CHECK( prompt.find( "=== ESTADO PROPIO ACTUAL" ) != std::string::npos );
    CHECK( prompt.find( "inventario_consultado=false" ) != std::string::npos );
    CHECK( report[static_cast<std::size_t>( npc_ai::profile_subsystem::perception )].calls == 0 );
}

TEST_CASE( "npc_ai_self_snapshot_uses_vanilla_hp_wounds_and_bleeding",
           "[npc_ai][npc_ai_self]" )
{
    npc &who = prepare_self_observer();
    const int wounded_hp = who.get_part_hp_max( body_part_arm_l ) - 10;
    who.set_part_hp_cur( body_part_arm_l, wounded_hp );
    who.add_effect( effect_bleed, 1_minutes, body_part_arm_l, false, 3, true );

    const npc_ai::npc_self_snapshot snapshot = npc_ai::build_self_snapshot(
                who, npc_ai::self_snapshot_scope::physical_state );
    const auto arm = std::find_if( snapshot.bodyparts.begin(), snapshot.bodyparts.end(),
    []( const npc_ai::self_bodypart_observation &part ) {
        return part.id == body_part_arm_l.str();
    } );

    REQUIRE( arm != snapshot.bodyparts.end() );
    CHECK( arm->hp_current == wounded_hp );
    CHECK( arm->hp_max == who.get_part_hp_max( body_part_arm_l ) );
    CHECK( arm->bleeding_intensity == who.get_effect_int( effect_bleed, body_part_arm_l ) );
    CHECK( snapshot.bleeding );

    const std::string rendered = npc_ai::render_self_snapshot( snapshot );
    CHECK( rendered.find( "id=" + body_part_arm_l.str() ) != std::string::npos );
    CHECK( rendered.find( "hp=" + std::to_string( wounded_hp ) + "/" ) != std::string::npos );
    CHECK( rendered.find( "sangrado=" + std::to_string( arm->bleeding_intensity ) ) !=
           std::string::npos );
}

TEST_CASE( "npc_ai_inventory_query_contains_real_wielded_equipment",
           "[npc_ai][npc_ai_self][npc_ai_context][npc_ai_conversation]" )
{
    npc &who = prepare_self_observer();
    item_location knife = who.i_add( item( itype_knife_hunting, calendar::turn ) );
    REQUIRE( knife );
    REQUIRE( who.wield( knife ) );

    const std::string prompt = npc_ai::build_npc_prompt(
                                   who, "¿Qué tienes en las manos?" );

    CHECK( npc_ai::classify_context_intent( "¿Qué tienes en las manos?" ) ==
           npc_ai::context_intent::self_inventory );
    CHECK( prompt.find( "inventario_consultado=true" ) != std::string::npos );
    CHECK( prompt.find( "id=" + itype_knife_hunting.str() ) != std::string::npos );
    CHECK( prompt.find( "empunado=true" ) != std::string::npos );
}

TEST_CASE( "npc_ai_severely_damaged_bodypart_is_explicitly_labelled_for_the_prompt",
           "[npc_ai][npc_ai_self][npc_ai_context][npc_ai_conversation]" )
{
    npc &who = prepare_self_observer();
    const int severe_hp = std::max( 1, who.get_part_hp_max( body_part_arm_r ) / 10 );
    who.set_part_hp_cur( body_part_arm_r, severe_hp );

    const std::string prompt = npc_ai::build_npc_prompt( who, "¿Te duele algo?" );

    CHECK( prompt.find( "id=" + body_part_arm_r.str() ) != std::string::npos );
    CHECK( prompt.find( "hp=" + std::to_string( severe_hp ) + "/" ) != std::string::npos );
    CHECK( prompt.find( "severidad_dano=grave" ) != std::string::npos );
}

TEST_CASE( "npc_ai_self_snapshot_reads_stamina_without_changing_it",
           "[npc_ai][npc_ai_self]" )
{
    npc &who = prepare_self_observer();
    who.set_stamina( who.get_stamina_max() / 3 );
    const int stamina_before = who.get_stamina();

    const npc_ai::npc_self_snapshot snapshot = npc_ai::build_self_snapshot(
                who, npc_ai::self_snapshot_scope::physical_state );
    const std::string rendered = npc_ai::render_self_snapshot( snapshot );
    const std::string prompt = npc_ai::build_npc_prompt( who, "Como estas?" );

    CHECK( snapshot.stamina == stamina_before );
    CHECK( snapshot.stamina_max == who.get_stamina_max() );
    CHECK( rendered.find( "resistencia={actual=" + std::to_string( stamina_before ) ) !=
           std::string::npos );
    CHECK( prompt.find( "resistencia={actual=" + std::to_string( stamina_before ) ) !=
           std::string::npos );
    CHECK( who.get_stamina() == stamina_before );
}
