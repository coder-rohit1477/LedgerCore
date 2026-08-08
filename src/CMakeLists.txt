# Module libraries (domain, ledger, posting, trialbalance, formula,
# computed, cli) are added here one at a time, each in its own phase, via
# add_subdirectory().

add_subdirectory(domain)
add_subdirectory(ledger)
add_subdirectory(posting)
add_subdirectory(trialbalance)
add_subdirectory(formula)
add_subdirectory(computed)
