#include "lexer.hpp"

namespace calliope {

const char* token_kind_name(TokenKind k) {
    switch (k) {
        case TokenKind::End:        return "End";
        case TokenKind::Newline:    return "Newline";
        case TokenKind::Pitch:      return "Pitch";
        case TokenKind::Rest:       return "Rest";
        case TokenKind::Int:        return "Int";
        case TokenKind::Str:        return "Str";
        case TokenKind::Ident:      return "Ident";
        case TokenKind::Upper:      return "Upper";
        case TokenKind::Equals:     return "Equals";
        case TokenKind::ColonColon: return "ColonColon";
        case TokenKind::Arrow:      return "Arrow";
        case TokenKind::Bar:        return "Bar";
        case TokenKind::Backslash:  return "Backslash";
        case TokenKind::Backtick:   return "Backtick";
        case TokenKind::Hash:       return "Hash";
        case TokenKind::Operator:   return "Operator";
        case TokenKind::LParen:     return "LParen";
        case TokenKind::RParen:     return "RParen";
        case TokenKind::LBracket:   return "LBracket";
        case TokenKind::RBracket:   return "RBracket";
        case TokenKind::LBrace:     return "LBrace";
        case TokenKind::RBrace:     return "RBrace";
        case TokenKind::Less:       return "Less";
        case TokenKind::Greater:    return "Greater";
        case TokenKind::Comma:      return "Comma";
        case TokenKind::Error:      return "Error";
    }
    return "?";
}

namespace lex {
namespace {

bool is_alpha(char c)        { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool is_digit(char c)        { return c >= '0' && c <= '9'; }
bool is_pitch_letter(char c) { return c >= 'a' && c <= 'g'; }

bool is_op_char(char c) {
    switch (c) {
        // Note: '<' and '>' are handled separately (chord delimiters), so they
        // are deliberately not operator chars here.
        case '+': case '-': case '*': case '/': case '.': case '$':
        case ':': case '=': case '!': case '&': case '|': case '^': case '~':
            return true;
        default:
            return false;
    }
}

// Does a maximal run of letters spell a pitch head, i.e.  [a-g] (is|es)* ?
// "c" yes, "cis" yes, "ees" yes, "deses" yes; "es" no, "desert" no.
bool matches_pitch_head(std::string_view s) {
    if (s.empty() || !is_pitch_letter(s[0])) return false;
    std::size_t i = 1;
    while (i < s.size()) {
        if (i + 1 >= s.size()) return false; // accidentals come in pairs
        bool pair = (s[i] == 'i' && s[i + 1] == 's') ||
                    (s[i] == 'e' && s[i + 1] == 's');
        if (!pair) return false;
        i += 2;
    }
    return true;
}

} // namespace

std::vector<Token> tokenize(std::string_view src) {
    std::vector<Token> out;
    Lexer L{src, 0, 1, 1};

    auto at_end = [&]() { return L.pos >= L.src.size(); };
    auto peek   = [&](std::size_t o = 0) -> char {
        std::size_t p = L.pos + o;
        return p < L.src.size() ? L.src[p] : '\0';
    };
    auto adv = [&]() -> char {
        char c = L.src[L.pos++];
        if (c == '\n') { L.line++; L.col = 1; } else { L.col++; }
        return c;
    };
    auto push = [&](TokenKind k, std::size_t start, int ln, int cl) {
        out.push_back(Token{k, src.substr(start, L.pos - start), ln, cl});
    };

    for (;;) {
        while (!at_end() && (peek() == ' ' || peek() == '\t' || peek() == '\r'))
            adv();
        if (at_end()) {
            out.push_back(Token{TokenKind::End, std::string_view{}, L.line, L.col});
            break;
        }

        int ln = L.line, cl = L.col;
        std::size_t start = L.pos;
        char c = peek();

        if (c == '\n') { adv(); push(TokenKind::Newline, start, ln, cl); continue; }

        // line comment: -- to end of line
        if (c == '-' && peek(1) == '-') {
            while (!at_end() && peek() != '\n') adv();
            continue;
        }
        // block comment: {- ... -}
        if (c == '{' && peek(1) == '-') {
            adv(); adv();
            while (!at_end() && !(peek() == '-' && peek(1) == '}')) adv();
            if (!at_end()) { adv(); adv(); }
            continue;
        }

        // arrow: '>' is not an operator char (it closes chords), so spell it here
        if (c == '-' && peek(1) == '>') {
            adv(); adv();
            push(TokenKind::Arrow, start, ln, cl);
            continue;
        }

        // '<'/'>' are chord delimiters, but '<='/'>=' are comparison operators
        if ((c == '<' || c == '>') && peek(1) == '=') {
            adv(); adv();
            push(TokenKind::Operator, start, ln, cl);
            continue;
        }

        // pipe '|>' — '>' is not an operator char, so spell it out here
        if (c == '|' && peek(1) == '>') {
            adv(); adv();
            push(TokenKind::Operator, start, ln, cl);
            continue;
        }

        // string literal
        if (c == '"') {
            adv();
            while (!at_end() && peek() != '"') {
                if (peek() == '\\') adv();
                adv();
            }
            if (!at_end()) adv();
            push(TokenKind::Str, start, ln, cl);
            continue;
        }

        // single-char structural tokens
        switch (c) {
            case '#': adv(); push(TokenKind::Hash,     start, ln, cl); continue;
            case '`': adv(); push(TokenKind::Backtick, start, ln, cl); continue;
            case '\\':adv(); push(TokenKind::Backslash,start, ln, cl); continue;
            case '(': adv(); push(TokenKind::LParen,   start, ln, cl); continue;
            case ')': adv(); push(TokenKind::RParen,   start, ln, cl); continue;
            case '[': adv(); push(TokenKind::LBracket, start, ln, cl); continue;
            case ']': adv(); push(TokenKind::RBracket, start, ln, cl); continue;
            case '{': adv(); push(TokenKind::LBrace,   start, ln, cl); continue;
            case '}': adv(); push(TokenKind::RBrace,   start, ln, cl); continue;
            case ',': adv(); push(TokenKind::Comma,    start, ln, cl); continue;
            case '<': adv(); push(TokenKind::Less,     start, ln, cl); continue;
            case '>': adv(); push(TokenKind::Greater,  start, ln, cl); continue;
            default: break;
        }

        // integer literal
        if (is_digit(c)) {
            while (!at_end() && is_digit(peek())) adv();
            push(TokenKind::Int, start, ln, cl);
            continue;
        }

        // word: pitch / rest / identifier / constructor
        if (is_alpha(c) || c == '_') {
            std::size_t letters_start = L.pos;
            while (!at_end() && is_alpha(peek())) adv();
            std::string_view run = src.substr(letters_start, L.pos - letters_start);

            // single-letter rest / spacer notation, with optional duration
            if (run == "r" || run == "R" || run == "s") {
                while (!at_end() && is_digit(peek())) adv();
                while (!at_end() && peek() == '.') adv();
                push(TokenKind::Rest, start, ln, cl);
                continue;
            }

            // reserved pitch class
            if (matches_pitch_head(run)) {
                if (!at_end() && peek() == '\'') {
                    while (!at_end() && peek() == '\'') adv();
                } else if (!at_end() && peek() == ',') {
                    while (!at_end() && peek() == ',') adv();
                }
                while (!at_end() && is_digit(peek())) adv();   // duration
                while (!at_end() && peek() == '.') adv();      // dots
                push(TokenKind::Pitch, start, ln, cl);
                continue;
            }

            // identifier / constructor: extend with alnum / _ / '
            while (!at_end() && (is_alpha(peek()) || is_digit(peek()) ||
                                 peek() == '_' || peek() == '\''))
                adv();
            TokenKind k = (c >= 'A' && c <= 'Z') ? TokenKind::Upper : TokenKind::Ident;
            push(k, start, ln, cl);
            continue;
        }

        // operator run
        if (is_op_char(c)) {
            while (!at_end() && is_op_char(peek())) adv();
            std::string_view op = src.substr(start, L.pos - start);
            TokenKind k = TokenKind::Operator;
            if      (op == "=")  k = TokenKind::Equals;
            else if (op == "::") k = TokenKind::ColonColon;
            else if (op == "->") k = TokenKind::Arrow;
            else if (op == "|")  k = TokenKind::Bar;
            push(k, start, ln, cl);
            continue;
        }

        adv();
        push(TokenKind::Error, start, ln, cl);
    }

    return out;
}

} // namespace lex
} // namespace calliope
