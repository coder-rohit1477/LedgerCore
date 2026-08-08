#pragma once

#include <string>

#include "ledgercore/formula/Ast.h"
#include "ledgercore/formula/ComputedAccountName.h"

namespace ledgercore::computed {

// An immutable binding of a computed-account name to a formula: the
// original source text (kept for display, error messages, and future
// persistence) and its parsed AST. The AST is parsed exactly once, here,
// via formula::parse() -- never re-parsed on evaluation, and never
// duplicated elsewhere.
class ComputedAccountDefinition {
public:
    // Throws formula::FormulaSyntaxException if formulaSource does not
    // parse.
    ComputedAccountDefinition(formula::ComputedAccountName name, std::string formulaSource);

    const formula::ComputedAccountName& name() const noexcept { return name_; }
    const std::string& formulaSource() const noexcept { return formulaSource_; }
    const formula::AstNode& ast() const noexcept { return *ast_; }

private:
    formula::ComputedAccountName name_;
    std::string formulaSource_;
    formula::AstNodePtr ast_;
};

} // namespace ledgercore::computed
