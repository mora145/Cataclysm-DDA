#include "npc_ai_batch_pickup.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <deque>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "game.h"
#include "item.h"
#include "item_location.h"
#include "map.h"
#include "map_selector.h"
#include "npc.h"
#include "npc_ai_context.h"
#include "npc_ai_debug.h"
#include "output.h"
#include "point.h"
#include "string_formatter.h"
#include "translations.h"


namespace
{


constexpr int batch_room_radius = 8;
constexpr std::size_t batch_room_tile_limit = 256;
constexpr std::size_t batch_food_limit = 128;
constexpr int batch_target_timeout_ticks = 240;


struct queued_food_target {
    item_location location;
    std::string id;
    std::string name;

    // CDDA-AI BATCH FOOD PICKUP V1.1 PRIORITY BEGIN
    int priority = 80;
    std::string quota_group;
    std::string priority_reason;
    // CDDA-AI BATCH FOOD PICKUP V1.1 PRIORITY END
};


struct food_batch_state {
    bool active = false;
    bool waiting_for_target = false;

    item_location current_target;

    std::string current_id;
    std::string current_name;

    std::deque<queued_food_target> queue;

    std::size_t found = 0;
    std::size_t started = 0;
    std::size_t collected = 0;
    std::size_t skipped = 0;

    int waiting_ticks = 0;
    std::string last_failure_name;
    std::string last_failure_reason;
};


std::unordered_map<int, food_batch_state> food_batches;


int npc_key(
    const npc &who
)
{
    return who.getID().get_value();
}


std::string lower_ascii(
    std::string text
)
{
    for( char &c : text ) {

        if(
            c >= 'A' &&
            c <= 'Z'
        ) {

            c =
                static_cast<char>(
                    c - 'A' + 'a'
                );
        }
    }

    return text;
}


bool contains_any(
    const std::string &text,
    const std::vector<std::string> &needles
)
{
    for(
        const std::string &needle :
        needles
    ) {

        if(
            text.find(
                needle
            ) != std::string::npos
        ) {

            return true;
        }
    }

    return false;
}



// ============================================================
// CDDA-AI BATCH FOOD PICKUP V1.1 PRIORITY HELPERS BEGIN
// ============================================================

struct food_priority_info {
    int priority = 80;
    std::string quota_group;
    std::string reason;
};


food_priority_info classify_food_priority(
    const std::string &display_name
)
{
    food_priority_info result;


    const std::string name =
        lower_ascii(
            display_name
        );


    // --------------------------------------------------------
    // VENCIDO / PODRIDO
    // --------------------------------------------------------

    if(
        contains_any(
            name,
            {
                "fuera de caducidad",
                "caducado",
                "caducada",
                "vencido",
                "vencida",
                "podrido",
                "podrida",
                "expired",
                "rotten",
                "spoiled"
            }
        )
    ) {

        result.priority =
            900;

        result.quota_group =
            "expired";

        result.reason =
            "EXPIRED_LAST";

        return result;
    }


    // --------------------------------------------------------
    // ACEITE
    // --------------------------------------------------------

    if(
        contains_any(
            name,
            {
                "aceite de cocina",
                "cooking oil",
                "vegetable oil",
                "animal cooking oil"
            }
        )
    ) {

        result.priority =
            700;

        result.quota_group =
            "oil";

        result.reason =
            "COOKING_OIL_LOW_PRIORITY";

        return result;
    }


    // --------------------------------------------------------
    // AGUA
    // --------------------------------------------------------

    if(
        contains_any(
            name,
            {
                "agua potable",
                "clean water",
                "potable water"
            }
        )
    ) {

        result.priority =
            300;

        result.quota_group =
            "water";

        result.reason =
            "WATER_LIMITED";

        return result;
    }


    // --------------------------------------------------------
    // OTRAS BEBIDAS / CAFE
    // --------------------------------------------------------

    if(
        contains_any(
            name,
            {
                "cafe",
                "café",
                "coffee",
                "jugo",
                "juice",
                "refresco",
                "soda",
                "energy drink",
                "bebida energetica",
                "bebida energética"
            }
        )
    ) {

        result.priority =
            500;

        result.quota_group =
            "drink";

        result.reason =
            "OTHER_DRINK_LIMITED";

        return result;
    }


    // --------------------------------------------------------
    // COMIDA DE ALTO VALOR PRACTICO
    //
    // Proteina, conservas y alimentos densos/listos.
    // --------------------------------------------------------

    if(
        contains_any(
            name,
            {
                "pemmican",
                "pollo",
                "chicken",
                "pescado",
                "fish",
                "atun",
                "atún",
                "tuna",
                "queso",
                "pecorino",
                "cheese",
                "gallet",
                "cracker",
                "verduras enlatadas",
                "canned vegetables",
                "tomates enlatados",
                "canned tomatoes",
                "langosta",
                "lobster",
                "encurtid",
                "pickle",
                "carne",
                "meat",
                "jerky",
                "salchicha",
                "sausage"
            }
        )
    ) {

        result.priority =
            20;

        result.reason =
            "HIGH_VALUE_FOOD";

        return result;
    }


    // --------------------------------------------------------
    // INGREDIENTES
    // --------------------------------------------------------

    if(
        contains_any(
            name,
            {
                "harina",
                "flour",
                "gelatina",
                "gelativa",
                "gelatin",
                "pasta",
                "lasaña",
                "lasagna",
                "salsa de tomate",
                "tomato sauce",
                "leche condensada",
                "condensed milk",
                "azucar",
                "azúcar",
                "sugar"
            }
        )
    ) {

        result.priority =
            120;

        result.reason =
            "COOKING_INGREDIENT";

        return result;
    }


    // --------------------------------------------------------
    // RESTO DE COMIDA
    // --------------------------------------------------------

    result.priority =
        80;

    result.reason =
        "GENERAL_FOOD";

    return result;
}


int food_quota_limit(
    const std::string &group
)
{
    if(
        group ==
        "water"
    ) {

        return 4;
    }


    if(
        group ==
        "drink"
    ) {

        return 2;
    }


    if(
        group ==
        "oil"
    ) {

        return 1;
    }


    return 1000000;
}


// ============================================================
// CDDA-AI BATCH FOOD PICKUP V1.1 PRIORITY HELPERS END
// ============================================================

bool is_batch_food_command(
    const std::string &player_line
)
{
    const std::string line =
        lower_ascii(
            player_line
        );


    const bool pickup_verb =
        contains_any(
            line,
            {
                "recoge",
                "recoger",
                "recoja",
                "junta",
                "juntar",
                "agarra",
                "agarrar",
                "recolecta",
                "recolectar",
                "levanta",
                "levantar"
            }
        );


    const bool all_quantifier =
        contains_any(
            line,
            {
                "toda",
                "todo ",
                "todas",
                "todos"
            }
        );


    const bool food_word =
        contains_any(
            line,
            {
                "comida",
                "alimento",
                "alimentos",
                "viveres"
            }
        );


    return
        pickup_verb &&
        all_quantifier &&
        food_word;
}


void reset_debug(
    const npc &who,
    const std::string &player_line
)
{
    npc_ai::debug_stream output( "npc_ai_batch_food_v1_runtime.txt", true );


    if( !output ) {
        return;
    }


    output
        << "CDDA-AI BATCH FOOD PICKUP V1 RUNTIME\n"
        << "NPC="
        << who.get_name()
        << "\n"
        << "REQUEST="
        << player_line
        << "\n";
}


void debug_line(
    const std::string &line
)
{
    npc_ai::append_debug_line( "npc_ai_batch_food_v1_runtime.txt", line );
}


bool contains_point(
    const std::vector<tripoint_bub_ms> &points,
    const tripoint_bub_ms &wanted
)
{
    return
        std::find(
            points.begin(),
            points.end(),
            wanted
        ) != points.end();
}


std::vector<tripoint_bub_ms> build_room_core(
    npc &who,
    map &here
)
{
    const tripoint_bub_ms origin =
        who.pos_bub(
            here
        );


    const bool origin_outside =
        here.is_outside(
            origin
        );


    std::vector<tripoint_bub_ms> room;

    std::deque<tripoint_bub_ms> frontier;


    room.push_back(
        origin
    );

    frontier.push_back(
        origin
    );


    while(
        !frontier.empty() &&
        room.size() <
        batch_room_tile_limit
    ) {

        const tripoint_bub_ms current =
            frontier.front();

        frontier.pop_front();


        const int dx[4] = {
            1,
            -1,
            0,
            0
        };

        const int dy[4] = {
            0,
            0,
            1,
            -1
        };


        for(
            int direction = 0;
            direction < 4;
            ++direction
        ) {

            tripoint_bub_ms next =
                current;


            next.x() +=
                dx[direction];

            next.y() +=
                dy[direction];


            if(
                next.z() !=
                origin.z()
            ) {

                continue;
            }


            if(
                rl_dist(
                    origin,
                    next
                ) >
                batch_room_radius
            ) {

                continue;
            }


            if(
                contains_point(
                    room,
                    next
                )
            ) {

                continue;
            }


            // Una puerta constituye el limite de esta habitacion.
            // Incluso abierta, no seguimos al cuarto siguiente.
            if(
                here.has_flag(
                    "DOOR",
                    next
                )
            ) {

                continue;
            }


            // Tampoco atravesamos ventanas.
            if(
                here.has_flag(
                    "WINDOW",
                    next
                )
            ) {

                continue;
            }


            // No mezclamos interior y exterior.
            if(
                here.is_outside(
                    next
                ) !=
                origin_outside
            ) {

                continue;
            }


            // El nucleo de la habitacion se construye sobre
            // casillas fisicamente transitables.
            if(
                !here.passable(
                    next
                )
            ) {

                continue;
            }


            // No damos conocimiento de zonas que Liam
            // no puede percibir actualmente.
            if(
                !who.sees(
                    here,
                    next
                )
            ) {

                continue;
            }


            room.push_back(
                next
            );

            frontier.push_back(
                next
            );


            if(
                room.size() >=
                batch_room_tile_limit
            ) {

                break;
            }
        }
    }


    return room;
}


bool tile_belongs_to_room(
    map &here,
    const tripoint_bub_ms &origin,
    const bool origin_outside,
    const std::vector<tripoint_bub_ms> &room_core,
    const tripoint_bub_ms &tile
)
{
    if(
        tile.z() !=
        origin.z()
    ) {

        return false;
    }


    if(
        rl_dist(
            origin,
            tile
        ) >
        batch_room_radius
    ) {

        return false;
    }


    if(
        here.has_flag(
            "DOOR",
            tile
        ) ||
        here.has_flag(
            "WINDOW",
            tile
        )
    ) {

        return false;
    }


    if(
        here.is_outside(
            tile
        ) !=
        origin_outside
    ) {

        return false;
    }


    if(
        contains_point(
            room_core,
            tile
        )
    ) {

        return true;
    }


    // Permite objetos sobre mesas, mesones y otros muebles
    // impasables que estan pegados al suelo de la habitacion.
    for(
        const tripoint_bub_ms &room_tile :
        room_core
    ) {

        if(
            rl_dist(
                room_tile,
                tile
            ) <= 1
        ) {

            return true;
        }
    }


    return false;
}


std::vector<queued_food_target> find_room_food(
    npc &who
)
{
    map &here =
        get_map();


    const tripoint_bub_ms origin =
        who.pos_bub(
            here
        );


    const bool origin_outside =
        here.is_outside(
            origin
        );


    const std::vector<tripoint_bub_ms>
    room_core =
        build_room_core(
            who,
            here
        );


    debug_line(
        std::string(
            "ROOM_CORE_TILES="
        ) +
        std::to_string(
            room_core.size()
        )
    );


    std::vector<queued_food_target>
    targets;


    for(
        const tripoint_bub_ms &p :
        here.points_in_radius(
            origin,
            batch_room_radius
        )
    ) {

        if(
            targets.size() >=
            batch_food_limit
        ) {

            break;
        }


        if(
            !tile_belongs_to_room(
                here,
                origin,
                origin_outside,
                room_core,
                p
            )
        ) {

            continue;
        }


        if(
            !who.sees(
                here,
                p
            )
        ) {

            continue;
        }


        if(
            !here.could_see_items(
                p,
                who
            )
        ) {

            continue;
        }


        map_stack items =
            here.i_at(
                p
            );


        for(
            item &it :
            items
        ) {

            if(
                !( ( it.is_food() || it.is_food_container() ) )
            ) {

                continue;
            }


            item_location location(
                map_cursor(
                    p
                ),
                &it
            );


            if(
                !location
            ) {

                continue;
            }


            queued_food_target target;

            target.location =
                location;

            target.id =
                it.typeId().str();

            target.name =
                remove_color_tags( it.tname() );

            const food_priority_info priority_info =
                classify_food_priority(
                    target.name
                );


            target.priority =
                priority_info.priority;

            target.quota_group =
                priority_info.quota_group;

            target.priority_reason =
                priority_info.reason;


            targets.push_back(
                target
            );


            std::ostringstream candidate_debug;

            candidate_debug
                << "CANDIDATE"
                << targets.size()
                << "="
                << target.name
                << " | id="
                << target.id
                << " | pos=("
                << p.x()
                << ","
                << p.y()
                << ","
                << p.z()
                << ")"
                << " | distance="
                << rl_dist(
                       origin,
                       p
                   );


            candidate_debug
                << " | priority="
                << target.priority
                << " | group="
                << (
                    target.quota_group.empty() ?
                    "food" :
                    target.quota_group
                )
                << " | reason="
                << target.priority_reason;

            debug_line(
                candidate_debug.str()
            );


            if(
                targets.size() >=
                batch_food_limit
            ) {

                break;
            }
        }
    }


        // CDDA-AI BATCH FOOD PICKUP V1.1 PRIORITY SORT BEGIN
    //
    // Primero decide QUE merece ocupar la mochila.
    // Distancia solo desempata dentro de una prioridad.

    std::sort(
        targets.begin(),
        targets.end(),
        [&]( const queued_food_target &a,
             const queued_food_target &b ) {

            if(
                a.priority !=
                b.priority
            ) {

                return
                    a.priority <
                    b.priority;
            }


            return
                rl_dist(
                    origin,
                    a.location.pos_bub(
                        here
                    )
                ) <
                rl_dist(
                    origin,
                    b.location.pos_bub(
                        here
                    )
                );
        }
    );


    // --------------------------------------------------------
    // CUOTAS DE ESTA EXPEDICION
    //
    // agua:        4 recipientes
    // otras bebidas: 2
    // aceite:      1
    //
    // IMPORTANTE:
    // estas son cuotas de candidatos, no capacidad artificial.
    // ai_request_pickup() sigue siendo quien decide si
    // fisicamente cabe cada objeto.
    // --------------------------------------------------------

    std::unordered_map<std::string, int>
    quota_counts;


    quota_counts["water"] =
        0;

    quota_counts["drink"] =
        0;

    quota_counts["oil"] =
        0;


    std::vector<queued_food_target>
    filtered_targets;


    filtered_targets.reserve(
        targets.size()
    );


    for(
        const queued_food_target &target :
        targets
    ) {

        if(
            target.quota_group.empty() ||
            target.quota_group ==
            "expired"
        ) {

            filtered_targets.push_back(
                target
            );

            continue;
        }


        const int limit =
            food_quota_limit(
                target.quota_group
            );


        int &current =
            quota_counts[
                target.quota_group
            ];


        if(
            current >=
            limit
        ) {

            std::ostringstream quota_debug;

            quota_debug
                << "SKIP_QUOTA="
                << target.name
                << " | group="
                << target.quota_group
                << " | current="
                << current
                << " | limit="
                << limit;


            debug_line(
                quota_debug.str()
            );


            continue;
        }


        filtered_targets.push_back(
            target
        );


        ++current;
    }


    targets.swap(
        filtered_targets
    );


    debug_line(
        std::string(
            "SELECTED_WATER="
        ) +
        std::to_string(
            quota_counts["water"]
        )
    );


    debug_line(
        std::string(
            "SELECTED_DRINKS="
        ) +
        std::to_string(
            quota_counts["drink"]
        )
    );


    debug_line(
        std::string(
            "SELECTED_OIL="
        ) +
        std::to_string(
            quota_counts["oil"]
        )
    );


    debug_line(
        std::string(
            "CANDIDATES_AFTER_PRIORITY_QUOTAS="
        ) +
        std::to_string(
            targets.size()
        )
    );


    // Imprimir el orden REAL en que Liam intentara recoger.
    for(
        std::size_t index = 0;
        index < targets.size();
        ++index
    ) {

        const queued_food_target &target =
            targets[index];


        std::ostringstream order_debug;

        order_debug
            << "ORDER"
            << (
                index +
                1
            )
            << "="
            << target.name
            << " | priority="
            << target.priority
            << " | group="
            << (
                target.quota_group.empty() ?
                "food" :
                target.quota_group
            );


        debug_line(
            order_debug.str()
        );
    }

    // CDDA-AI BATCH FOOD PICKUP V1.1 PRIORITY SORT END

    return targets;
}


bool start_next_target(
    npc &who,
    food_batch_state &state
)
{
    map &here =
        get_map();


    while(
        !state.queue.empty()
    ) {

        queued_food_target target =
            state.queue.front();


        state.queue.pop_front();


        if(
            !target.location ||
            target.location.where() !=
            item_location::type::map
        ) {

            ++state.skipped;

            debug_line(
                std::string(
                    "SKIP_INVALID="
                ) +
                target.name
            );

            continue;
        }


        const tripoint_bub_ms target_position =
            target.location.pos_bub(
                here
            );


        if(
            !who.sees(
                here,
                target_position
            ) ||
            !here.could_see_items(
                target_position,
                who
            )
        ) {

            ++state.skipped;

            debug_line(
                std::string(
                    "SKIP_NOT_VISIBLE="
                ) +
                target.name
            );

            continue;
        }


        std::string error;


        const bool started =
            who.ai_request_pickup(
                target.location,
                target_position,
                error,
                false,
                "batch pickup"
            );


        if(
            !started
        ) {

            ++state.skipped;
            state.last_failure_name = target.name;
            state.last_failure_reason = error;


            std::ostringstream failure_debug;

            failure_debug
                << "SKIP_REQUEST_REJECTED="
                << target.name
                << " | id="
                << target.id
                << " | reason="
                << error;


            debug_line(
                failure_debug.str()
            );


            // Puede que este objeto ya no quepa,
            // pero otro mas pequeno aun si.
            continue;
        }


        state.waiting_for_target =
            true;

        state.current_target =
            target.location;

        state.current_id =
            target.id;

        state.current_name =
            target.name;

        state.waiting_ticks =
            0;

        ++state.started;


        std::ostringstream started_debug;

        started_debug
            << "TARGET_STARTED="
            << target.name
            << " | id="
            << target.id
            << " | remaining="
            << state.queue.size();


        debug_line(
            started_debug.str()
        );


        return true;
    }


    return false;
}


void finish_batch(
    npc &who,
    food_batch_state &state
)
{
    std::ostringstream debug;

    debug
        << "BATCH_FINISHED"
        << " | found="
        << state.found
        << " | started="
        << state.started
        << " | collected="
        << state.collected
        << " | skipped="
        << state.skipped;


    debug_line(
        debug.str()
    );


    state.active =
        false;

    state.waiting_for_target =
        false;

    state.current_target =
        item_location();


    // Habla mediante el sistema real de CDDA.
    if( state.skipped > 0 && !state.last_failure_reason.empty() ) {
        who.say( string_format( npc_ai::localized_ai_message(
                                    _( "I picked up everything I could, but I couldn't take %1$s: %2$s" ),
                                    "Recogí todo lo que pude, pero no pude llevarme %1$s: %2$s" ),
                                state.last_failure_name, state.last_failure_reason ) );
    } else {
        who.say( npc_ai::localized_ai_message(
                     _( "Done. I picked up all the food I could carry." ),
                     "Listo. Recogí toda la comida que podía cargar." ) );
    }
}


} // namespace


namespace npc_ai
{


batch_pickup_command_result try_handle_batch_pickup_command(
    npc &who,
    const std::string &player_line
)
{
    batch_pickup_command_result result;


    if(
        !is_batch_food_command(
            player_line
        )
    ) {

        return result;
    }


    result.handled =
        true;


    reset_debug(
        who,
        player_line
    );


    debug_line(
        "INTENT=BATCH_FOOD_PICKUP"
    );


    const std::vector<queued_food_target>
    food =
        find_room_food(
            who
        );


    if(
        food.empty()
    ) {

        result.message =
            "No veo comida suelta que pueda recoger en esta habitacion.";


        debug_line(
            "RESULT=NO_VISIBLE_FOOD"
        );


        return result;
    }


    food_batch_state state;

    state.active =
        true;

    state.found =
        food.size();


    for(
        const queued_food_target &target :
        food
    ) {

        state.queue.push_back(
            target
        );
    }


    food_batches[
        npc_key(
            who
        )
    ] =
        state;


    food_batch_state &stored =
        food_batches[
            npc_key(
                who
            )
        ];


    const bool started =
        start_next_target(
            who,
            stored
        );


    if(
        !started
    ) {

        stored.active =
            false;


        result.message = stored.last_failure_reason.empty() ?
                         npc_ai::localized_ai_message(
                             _( "I can see food, but I can't pick any of it up right now." ),
                             "Veo comida, pero ahora mismo no puedo recogerla." ) :
                         string_format( _( "I can't pick up %1$s: %2$s" ),
                                        stored.last_failure_name, stored.last_failure_reason );


        debug_line(
            "RESULT=NO_TARGET_COULD_START"
        );


        return result;
    }


    result.success =
        true;


    result.message =
        "Vale. Voy a recoger toda la comida de aqui que pueda cargar.";


    std::ostringstream result_debug;

    result_debug
        << "RESULT=STARTED"
        << " | candidates="
        << food.size();


    debug_line(
        result_debug.str()
    );


    return result;
}


void process_batch_pickup(
    npc &who
)
{
    const int key =
        npc_key(
            who
        );


    const auto found =
        food_batches.find(
            key
        );


    if(
        found ==
        food_batches.end()
    ) {

        return;
    }


    food_batch_state &state =
        found->second;


    if(
        !state.active
    ) {

        return;
    }


    if(
        state.waiting_for_target
    ) {

        // Mientras el item siga realmente en el mapa,
        // la accion dirigida todavia esta en curso.
        if(
            state.current_target &&
            state.current_target.where() ==
            item_location::type::map
        ) {

            ++state.waiting_ticks;


            if(
                state.waiting_ticks <=
                batch_target_timeout_ticks
            ) {

                return;
            }


            ++state.skipped;


            std::ostringstream timeout_debug;

            timeout_debug
                << "TARGET_TIMEOUT="
                << state.current_name
                << " | id="
                << state.current_id;


            debug_line(
                timeout_debug.str()
            );


            state.waiting_for_target =
                false;

            state.current_target =
                item_location();

            state.current_id.clear();

            state.current_name.clear();

            state.waiting_ticks =
                0;
        }
        else {

            ++state.collected;


            std::ostringstream collected_debug;

            collected_debug
                << "TARGET_COMPLETED="
                << state.current_name
                << " | id="
                << state.current_id
                << " | collected="
                << state.collected;


            debug_line(
                collected_debug.str()
            );


            state.waiting_for_target =
                false;

            state.current_target =
                item_location();

            state.current_id.clear();

            state.current_name.clear();

            state.waiting_ticks =
                0;
        }
    }


    if(
        start_next_target(
            who,
            state
        )
    ) {

        return;
    }


    finish_batch(
        who,
        state
    );
}


void reset_all_food_batches()
{
    food_batches.clear();
}


} // namespace npc_ai
