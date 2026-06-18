#include "ast.hpp"

#include <cstdio>
#include <utility>

namespace calliope::ast {

NodeId ast_add(Ast& a, Node n) {
    a.nodes.push_back(std::move(n));
    return static_cast<NodeId>(a.nodes.size() - 1);
}

const char* node_kind_name(NodeKind k) {
    switch (k) {
        case NodeKind::Program:   return "Program";
        case NodeKind::Directive: return "Directive";
        case NodeKind::TypeSig:   return "TypeSig";
        case NodeKind::Binding:   return "Binding";
        case NodeKind::ClassDecl: return "ClassDecl";
        case NodeKind::InstanceDecl: return "InstanceDecl";
        case NodeKind::MethodSig: return "MethodSig";
        case NodeKind::PitchLit:  return "PitchLit";
        case NodeKind::RestLit:   return "RestLit";
        case NodeKind::IntLit:    return "IntLit";
        case NodeKind::StrLit:    return "StrLit";
        case NodeKind::Var:       return "Var";
        case NodeKind::Con:       return "Con";
        case NodeKind::App:       return "App";
        case NodeKind::Seq:       return "Seq";
        case NodeKind::BinOp:     return "BinOp";
        case NodeKind::Lambda:    return "Lambda";
        case NodeKind::Let:       return "Let";
        case NodeKind::If:        return "If";
        case NodeKind::Case:      return "Case";
        case NodeKind::Alt:       return "Alt";
        case NodeKind::ListLit:   return "ListLit";
        case NodeKind::Chord:     return "Chord";
        case NodeKind::PatVar:    return "PatVar";
        case NodeKind::PatWild:   return "PatWild";
        case NodeKind::PatInt:    return "PatInt";
        case NodeKind::PatCon:    return "PatCon";
        case NodeKind::Param:     return "Param";
        case NodeKind::TypeAtom:  return "TypeAtom";
        case NodeKind::Error:     return "Error";
    }
    return "?";
}

namespace {
void put_indent(int n) {
    for (int i = 0; i < n; i++) std::printf("  ");
}
} // namespace

void ast_print(const Ast& a, NodeId id, int indent) {
    if (id < 0 || id >= static_cast<NodeId>(a.nodes.size())) {
        put_indent(indent);
        std::printf("<null>\n");
        return;
    }
    const Node& n = a.nodes[id];
    put_indent(indent);
    std::printf("(%s", node_kind_name(n.kind));
    if (!n.tok.text.empty())
        std::printf(" '%.*s'", static_cast<int>(n.tok.text.size()), n.tok.text.data());
    if (n.kids.empty()) {
        std::printf(")\n");
        return;
    }
    std::printf("\n");
    for (NodeId k : n.kids) ast_print(a, k, indent + 1);
    put_indent(indent);
    std::printf(")\n");
}

} // namespace calliope::ast
