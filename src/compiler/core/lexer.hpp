#pragma once

#include "token.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace calliope::lex {

// Mutable scan state. All public; drive it with tokenize() below.
struct Lexer {
    std::string_view src;
    std::size_t pos = 0;
    int line = 1;
    int col = 1;
};

// Tokenize an entire source buffer. The returned tokens' `text` views point into
// `src`, so keep `src` alive for as long as the tokens are used. The final token
// is always TokenKind::End.
std::vector<Token> tokenize(std::string_view src);

} // namespace calliope::lex
