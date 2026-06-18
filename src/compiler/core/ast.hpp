#pragma once

#include "token.hpp"

#include <cstdint>
#include <vector>

// Index-pooled AST. No inheritance, no variants: every node is a `Node` with a
// `NodeKind` tag and a list of child ids. Nodes live in `Ast.nodes`; references
// are `NodeId` indices into that vector (NoNode = absent).

namespace calliope::ast {

using NodeId = std::int32_t;
constexpr NodeId NoNode = -1;

enum class NodeKind : std::uint8_t {
    Program,    // kids: top-level declarations
    Directive,  // tok: name (relative/absolute/load); kids: argument exprs
    TypeSig,    // tok: name; kids: TypeAtom tokens of the signature
    Binding,    // tok: name; kids: Param* then body expr; extra: param count

    PitchLit,   // tok: pitch literal
    RestLit,    // tok: r/R/s
    IntLit,     // tok: integer
    StrLit,     // tok: string

    Var,        // tok: lowercase identifier
    Con,        // tok: uppercase-leading (interval/constructor/dynamic)
    App,        // kids[0]: function, kids[1..]: args
    Seq,        // kids: adjacent pitch literals composed sequentially (:+:)
    BinOp,      // tok: operator; kids[0]: lhs, kids[1]: rhs
    Lambda,     // kids: Param* then body; extra: param count
    Let,        // kids: Binding* then body; extra: binding count
    If,         // kids: cond, then, else
    ListLit,    // kids: elements
    Chord,      // kids: pitch notes sounding together

    Param,      // tok: parameter name
    TypeAtom,   // tok: one token of a (currently unparsed) type signature
    Error,      // parse error placeholder
};

struct Node {
    NodeKind kind = NodeKind::Error;
    Token tok;
    std::vector<NodeId> kids;
    int extra = 0;
};

struct Ast {
    std::vector<Node> nodes;
    NodeId root = NoNode;
};

NodeId ast_add(Ast& a, Node n);
const char* node_kind_name(NodeKind k);

// Debug: print the subtree rooted at `id` as indented S-expressions.
void ast_print(const Ast& a, NodeId id, int indent);

} // namespace calliope::ast
