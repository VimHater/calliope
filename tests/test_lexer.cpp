#include "test.hpp"

#include "lexer.hpp"

using namespace calliope;

namespace {
Token first(std::string_view s) { return lex::tokenize(s).front(); }
} // namespace

void run_lexer_tests() {
    // reserved pitch class
    CHECK(first("c").kind == TokenKind::Pitch);
    CHECK_EQ_STR(first("c").text, "c");
    CHECK(first("cis").kind == TokenKind::Pitch);    // c sharp
    CHECK(first("ees").kind == TokenKind::Pitch);    // e flat
    CHECK(first("deses").kind == TokenKind::Pitch);  // d double-flat
    CHECK(first("c'4").kind == TokenKind::Pitch);    // octave + duration
    CHECK(first("g2.").kind == TokenKind::Pitch);    // dotted duration

    // words that look pitch-ish but aren't, per our -is/-es grammar
    CHECK(first("es").kind == TokenKind::Ident);
    CHECK(first("desert").kind == TokenKind::Ident);
    CHECK(first("subject").kind == TokenKind::Ident);

    // uppercase -> constructor/interval/dynamic
    CHECK(first("C").kind == TokenKind::Upper);
    CHECK(first("P5").kind == TokenKind::Upper);

    // rests / spacer  (single r/R/s are reserved notation, not identifiers)
    CHECK(first("r4").kind == TokenKind::Rest);
    CHECK(first("R").kind == TokenKind::Rest);
    CHECK(first("s8").kind == TokenKind::Rest);
    CHECK(first("s").kind == TokenKind::Rest);    // spacer, not a variable
    CHECK(first("f").kind == TokenKind::Pitch);   // note F, not a variable
    // but multi-letter words starting with those letters are ordinary names
    CHECK(first("subj").kind == TokenKind::Ident);
    CHECK(first("rest").kind == TokenKind::Ident);

    // a word is a pitch/rest only when the WHOLE word is exactly one — a
    // trailing identifier char (letter / '_') makes the whole thing a name,
    // never a pitch + dangling tail. (Applies to all of a..g, r, s.)
    {
        // pitch spelling + identifier tail -> one identifier
        std::vector<Token> t = lex::tokenize("c_foo");
        CHECK(t.size() == 2 && t[0].kind == TokenKind::Ident);
        CHECK_EQ_STR(t[0].text, "c_foo");
    }
    CHECK(first("c_foo").kind == TokenKind::Ident);
    CHECK(first("e_value").kind == TokenKind::Ident);
    CHECK(first("a_b").kind == TokenKind::Ident);        // a/b included too
    CHECK(first("fis_sharp").kind == TokenKind::Ident);  // accidental + tail
    CHECK(first("d3note").kind == TokenKind::Ident);     // octave-ish + letters
    CHECK(first("g2x").kind == TokenKind::Ident);        // duration-ish + letter
    CHECK(first("c'x").kind == TokenKind::Ident);        // octave mark + letter
    // rest / spacer with an identifier tail is likewise one name
    CHECK(first("s_x").kind == TokenKind::Ident);
    CHECK(first("rests").kind == TokenKind::Ident);
    {
        std::vector<Token> t = lex::tokenize("r2d2");   // not Rest r2 + Pitch d2
        CHECK(t.size() == 2 && t[0].kind == TokenKind::Ident);
        CHECK_EQ_STR(t[0].text, "r2d2");
    }
    // genuine pitches/rests are unaffected (whole word is exactly notation)
    CHECK(first("c'").kind == TokenKind::Pitch);
    CHECK(first("fis'8").kind == TokenKind::Pitch);
    CHECK(first("e4").kind == TokenKind::Pitch);
    CHECK(first("r4").kind == TokenKind::Rest);

    // operators and punctuation
    CHECK(first(":+:").kind == TokenKind::Operator);
    CHECK(first(":=:").kind == TokenKind::Operator);
    CHECK(first("+").kind == TokenKind::Operator);
    CHECK(first("::").kind == TokenKind::ColonColon);
    CHECK(first("->").kind == TokenKind::Arrow);
    CHECK(first("=").kind == TokenKind::Equals);
    CHECK(first("|").kind == TokenKind::Bar);
    CHECK(first("#").kind == TokenKind::Hash);
    CHECK(first("\"file\"").kind == TokenKind::Str);
    CHECK(first("123").kind == TokenKind::Int);

    // comments are skipped (token after the leading pitch is End, not the comment)
    CHECK(first("c -- trailing comment").kind == TokenKind::Pitch);
    CHECK(first("{- block -}c").kind == TokenKind::Pitch);

    // a pitch run lexes to three pitches + End
    std::vector<Token> run = lex::tokenize("c d e");
    CHECK(run.size() == 4);
    CHECK(run[0].kind == TokenKind::Pitch);
    CHECK(run[1].kind == TokenKind::Pitch);
    CHECK(run[2].kind == TokenKind::Pitch);
    CHECK(run[3].kind == TokenKind::End);
}
