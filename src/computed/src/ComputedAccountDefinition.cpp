#include "ledgercore/computed/ComputedAccountDefinition.h"

#include <utility>

#include "ledgercore/formula/Parser.h"

namespace ledgercore::computed {

ComputedAccountDefinition::ComputedAccountDefinition(formula::ComputedAccountName name, std::string formulaSource)
    : name_(std::move(name)), formulaSource_(std::move(formulaSource)), ast_(formula::parse(formulaSource_)) {}

} // namespace ledgercore::computed
