#include "cata_catch.h"

#include <cstddef>
#include <string>
#include <vector>

#include "map_helpers.h"
#include "npc.h"
#include "npc_ai_context.h"
#include "player_helpers.h"
#include "point.h"

namespace
{

npc &prepare_context_observer()
{
    clear_map();
    clear_avatar();
    clear_npcs();
    npc &who = spawn_npc( point_bub_ms{ 60, 60 }, "test_talker" );
    clear_character( who );
    return who;
}

} // namespace

TEST_CASE( "npc_ai_context_router_includes_only_intent_relevant_context",
           "[npc_ai][npc_ai_context]" )
{
    npc &who = prepare_context_observer();

    const std::string greeting = npc_ai::build_npc_prompt( who, "Hola." );
    const std::string system = npc_ai::build_npc_system_prompt( who );
    CHECK( ( system.find( "PERSONALITY" ) != std::string::npos ||
             system.find( "PERSONALIDAD" ) != std::string::npos ) );
    CHECK( ( system.find( "LANGUAGE LOCK" ) != std::string::npos ||
             system.find( "BLOQUEO DE IDIOMA" ) != std::string::npos ) );
    CHECK( greeting.find( "CDDA PERSONALITY" ) == std::string::npos );
    CHECK( greeting.find( "LANGUAGE LOCK" ) == std::string::npos );
    CHECK( npc_ai::classify_context_intent( "Hola." ) ==
           npc_ai::context_intent::greeting );
    CHECK( greeting.find( "SENSORIAL ACTUAL DEL NPC" ) == std::string::npos );
    CHECK( greeting.find( "ESTADO PROPIO ACTUAL" ) == std::string::npos );
    CHECK( greeting.find( "MEMORIA PERSONAL DEL NPC" ) == std::string::npos );

    const std::string self = npc_ai::build_npc_prompt( who, "Como estas?" );
    CHECK( npc_ai::classify_context_intent( "Como estas?" ) ==
           npc_ai::context_intent::self_state );
    CHECK( self.find( "ESTADO PROPIO ACTUAL" ) != std::string::npos );
    CHECK( self.find( "SENSORIAL ACTUAL DEL NPC" ) == std::string::npos );
    CHECK( self.find( "MEMORIA PERSONAL DEL NPC" ) == std::string::npos );

    const std::string sensory = npc_ai::build_npc_prompt( who, "Que ves?" );
    CHECK( npc_ai::classify_context_intent( "Que ves?" ) ==
           npc_ai::context_intent::perception_brief );
    CHECK( sensory.find( "SENSORIAL ACTUAL DEL NPC" ) != std::string::npos );
    CHECK( sensory.find( "ESTADO PROPIO ACTUAL" ) == std::string::npos );
    CHECK( sensory.find( "MEMORIA PERSONAL DEL NPC" ) == std::string::npos );

    const std::string memory =
        npc_ai::build_npc_prompt( who, "Recuerdas donde dejamos el rifle?" );
    CHECK( npc_ai::classify_context_intent( "Recuerdas donde dejamos el rifle?" ) ==
           npc_ai::context_intent::memory );
    CHECK( memory.find( "MEMORIA PERSONAL DEL NPC" ) != std::string::npos );
    CHECK( memory.find( "SENSORIAL ACTUAL DEL NPC" ) == std::string::npos );
    CHECK( memory.find( "ESTADO PROPIO ACTUAL" ) == std::string::npos );
}

TEST_CASE( "npc_ai_context_router_respects_the_prompt_budget_for_each_intent",
           "[npc_ai][npc_ai_context][npc_ai_performance]" )
{
    npc &who = prepare_context_observer();
    const std::vector<std::string> queries = {"Hola.",
                                              "Como estas?",
                                              "Que llevas?",
                                              "Que ves?",
                                              "Describe todo lo que ves",
                                              "Recuerdas donde dejamos el rifle?",
                                              "Que te pedi que vigilaras?",
                                              "Vamonos ahora."
                                             };

    for( const std::string &query : queries ) {
        const npc_ai::context_intent intent =
            npc_ai::classify_context_intent( query );
        CAPTURE( query, static_cast<int>( intent ) );
        CHECK( npc_ai::build_npc_prompt( who, query ).size() +
               npc_ai::build_npc_system_prompt( who ).size() <=
               npc_ai::context_prompt_budget_bytes( intent ) );
    }
}

TEST_CASE( "npc_ai_context_router_recognizes_fact_categories_across_natural_variants",
           "[npc_ai][npc_ai_context][npc_ai_conversation]" )
{
    using intent = npc_ai::context_intent;
    const std::vector<std::pair<std::string, intent>> queries = {
        { "¿Te duele algo?", intent::self_state },
        { "¿Cómo tienes el brazo?", intent::self_state },
        { "¿Qué te pasó?", intent::self_state },
        { "¿Ves algún zombi?", intent::perception_brief },
        { "¿Hay enemigos?", intent::perception_brief },
        { "¿Ves algo peligroso?", intent::perception_brief },
        { "¿Qué tienes en las manos?", intent::self_inventory },
        { "¿Tienes munición?", intent::self_inventory },
        { "¿Qué arma llevas?", intent::self_inventory },
        { "¿Qué está pasando?", intent::current_situation },
        { "¿Cómo está la cosa?", intent::current_situation },
        { "¿Estamos seguros?", intent::current_situation }
    };

    for( const auto &entry : queries ) {
        CAPTURE( entry.first );
        CHECK( npc_ai::classify_context_intent( entry.first ) == entry.second );
    }
    CHECK( std::string( npc_ai::context_intent_name( intent::self_state ) ) == "HEALTH" );
    CHECK( std::string( npc_ai::context_intent_name( intent::perception_brief ) ) ==
           "PERCEPTION" );
    CHECK( std::string( npc_ai::context_intent_name( intent::self_inventory ) ) ==
           "INVENTORY_EQUIPMENT" );
    CHECK( std::string( npc_ai::context_intent_name( intent::current_situation ) ) ==
           "CURRENT_SITUATION" );
}

TEST_CASE( "npc_ai_group_self_state_prompts_stay_within_budget_at_20_npcs",
           "[npc_ai][npc_ai_context][npc_ai_performance]" )
{
    clear_map();
    clear_avatar();
    clear_npcs();
    constexpr int population = 20;
    constexpr const char *query = "Como estan?";
    REQUIRE( npc_ai::classify_context_intent( query ) ==
             npc_ai::context_intent::self_state );

    std::size_t total_bytes = 0;
    for( int index = 0; index < population; ++index ) {
        npc &who = spawn_npc( point_bub_ms{50 + index, 60}, "test_talker" );
        clear_character( who );
        const std::string prompt = npc_ai::build_npc_prompt( who, query );
        CHECK( prompt.find( "ESTADO PROPIO ACTUAL" ) != std::string::npos );
        CHECK( prompt.find( "SENSORIAL ACTUAL DEL NPC" ) == std::string::npos );
        CHECK( prompt.size() <= npc_ai::context_prompt_budget_bytes(
                   npc_ai::context_intent::self_state ) );
        total_bytes += prompt.size();
    }

    CHECK( total_bytes <= population * npc_ai::context_prompt_budget_bytes(
               npc_ai::context_intent::self_state ) );
}
