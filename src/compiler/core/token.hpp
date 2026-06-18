#pragma once

#include <cstdint>
#include <string_view>

// Token stream produced by the lexer (src/compiler/core/lexer.*).
// Plain data, C-style API. A Token's `text` is a view into the original source
// string, which must outlive the token vector.

namespace calliope {

enum class TokenKind : std::uint8_t {
    End,        // end of input
    Newline,    // significant line break (statement separator / layout cue)

    Pitch,      // reserved pitch literal: c, fis, g', ees,, c'4, g2.
    Rest,       // r, R, s  (with optional duration: r4)
    Int,        // integer literal: 3, 16
    Str,        // "..." string literal (e.g. #load "file")

    Ident,      // lowercase identifier or keyword: subject, invert, where
    Upper,      // uppercase-leading: P5, M3, Major, C, F (interval/ctor/dynamic)

    Equals,     // =
    ColonColon, // ::
    Arrow,      // ->
    Bar,        // |
    Backslash,  // \   (lambda)
    Backtick,   // `   (infix application)
    Hash,       // #   (preprocessor directive)
    Operator,   // any other symbolic operator: + - * / . $ :+: :=: ...

    LParen, RParen,
    LBracket, RBracket,
    LBrace, RBrace,
    Less,       // <   (chord open / comparison)
    Greater,    // >   (chord close / comparison)
    Comma,

    Error,      // unrecognized character
};

struct Token {
    TokenKind kind = TokenKind::End;
    std::string_view text;
    int line = 1;
    int col = 1;
};

const char* token_kind_name(TokenKind k);

} // namespace calliope
