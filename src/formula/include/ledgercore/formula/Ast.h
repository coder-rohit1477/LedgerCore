#pragma once

#include <memory>
#include <utility>
#include <variant>

#include "ledgercore/domain/AccountCode.h"
#include "ledgercore/formula/Rational.h"

namespace ledgercore::formula {

class AstNode;

// A variant cannot directly contain itself, so child nodes are held
// through unique_ptr: an AST is a tree with single ownership and no
// aliasing, so there is no reason for shared/reference-counted
// indirection.
using AstNodePtr = std::unique_ptr<AstNode>;

struct Literal {
    Rational value;
};

struct AccountReference {
    domain::AccountCode code;
};

enum class UnaryOperator { Plus, Minus };

struct UnaryExpression {
    UnaryOperator op;
    AstNodePtr operand;
};

enum class BinaryOperator { Add, Subtract, Multiply, Divide };

struct BinaryExpression {
    BinaryOperator op;
    AstNodePtr left;
    AstNodePtr right;
};

// A formula AST node: exactly one of the four expression forms the
// grammar produces, held in a std::variant rather than an inheritance
// hierarchy -- consistent with the rest of LedgerCore, which favors
// concrete value types over virtual dispatch everywhere except the
// exception hierarchy, and it avoids needing a visitor base class:
// evaluation is a single std::visit call over value().
//
// AstNode is a thin wrapper class around the variant, not a bare `using`
// alias for it, because UnaryExpression/BinaryExpression must hold
// AstNodePtr members before the variant itself can be fully defined --
// the standard technique for a self-referential variant-based tree in
// C++17 (a `using` alias cannot itself be forward-declared the way a
// class name can).
class AstNode {
public:
    using Value = std::variant<Literal, AccountReference, UnaryExpression, BinaryExpression>;

    explicit AstNode(Value value) : value_(std::move(value)) {}

    const Value& value() const noexcept { return value_; }

private:
    Value value_;
};

} // namespace ledgercore::formula
