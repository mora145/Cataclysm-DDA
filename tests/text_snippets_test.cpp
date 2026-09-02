#include <optional>
#include <string>

#include "cata_catch.h"
#include "text_snippets.h"
#include "translation.h"

TEST_CASE( "random_snippet_with_small_seed", "[text_snippets][rng]" )
{
    const int seed_start = -10;
    const int seed_end = 10;
    int snip_change = 0;
    std::optional<translation> prev_snip;
    for( int seed = seed_start; seed <= seed_end; ++seed ) {
        const std::optional<translation> snip = SNIPPET.random_from_category( "lab_notes", seed );
        REQUIRE( snip.has_value() );
        if( prev_snip.has_value() && *prev_snip != *snip ) {
            snip_change++;
        }
        prev_snip = snip;
    }
    // Random snippets change at least 90% of the time when the seed has changed.
    // This is a very weak requirement, but should rule out the possibility of
    // using `std::minstd_rand0` with `std::uniform_int_distribution`.
    CHECK( snip_change >= ( seed_end - seed_start ) * 0.9 );
}

TEST_CASE( "text_snippet_escape", "[text_snippets]" )
{
    CHECK( SNIPPET.expand( "Foo <lt> bar <gt> baz" ) == "Foo < bar > baz" );
    CHECK( SNIPPET.expand( "Foo <lt>lt<gt> baz" ) == "Foo <lt> baz" );
}

TEST_CASE( "text_snippet_translator_mangled_punc_tags", "[text_snippets]" )
{
    REQUIRE( SNIPPET.has_category( "<punc\xE2\x80\xA6!>" ) );
    REQUIRE( SNIPPET.has_category( "<punc.\xE2\x80\xA6>" ) );

    const auto expands_to_punctuation = []( const std::string & input,
    const std::string & prefix ) {
        const std::string out = SNIPPET.expand( input );
        REQUIRE( out.find( '<' ) == std::string::npos );
        REQUIRE( out.compare( 0, prefix.size(), prefix ) == 0 );
        REQUIRE( out.size() > prefix.size() );
        const std::string punct = out.substr( prefix.size() );
        CHECK( ( punct == "." || punct == "!" || punct == "\xE2\x80\xA6" ) );
    };

    // Exact snippet categories from talk_tags.json.
    expands_to_punctuation( "Can't believe it<punc\xE2\x80\xA6!>", "Can't believe it" );
    expands_to_punctuation( "Later<punc.\xE2\x80\xA6>", "Later" );
    // Spanish translations replace U+2026 with ASCII periods; those tags must
    // still expand instead of surviving into parse_tags as a "Bad tag".
    expands_to_punctuation( "No me lo creo<punc...!>", "No me lo creo" );
    expands_to_punctuation( "Later<punc....>", "Later" );
}
