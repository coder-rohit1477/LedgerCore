#include <exception>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

#include "CommandExecution.h"
#include "LedgerSession.h"

#include "ledgercore/domain/Currency.h"

namespace ledgercore::cli {
namespace {

int runRepl(LedgerSession& session) {
    std::cout << "LedgerCore CLI -- in-memory session; state is lost when this process exits unless you 'save' it "
                  "first.\n";
    std::cout << "Commands: account, post, trial-balance, balance-sheet, income-statement, formula, computed, "
                  "close, period, journal, save, load.\n";
    std::cout << "Type 'exit' or 'quit' to leave, or send EOF (Ctrl-D).\n";

    std::string line;
    while (true) {
        std::cout << "ledgercore> " << std::flush;
        if (!std::getline(std::cin, line)) {
            std::cout << "\n";
            return 0;
        }
        const std::optional<int> stop = runOneLine(line, session, std::cout, std::cerr, /*exitOnFailure=*/false);
        if (stop.has_value()) {
            return *stop;
        }
    }
}

int runScript(const std::string& path, LedgerSession& session) {
    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "error: cannot open script file: " << path << "\n";
        return 1;
    }

    std::string line;
    while (std::getline(in, line)) {
        const std::optional<int> stop = runOneLine(line, session, std::cout, std::cerr, /*exitOnFailure=*/true);
        if (stop.has_value()) {
            return *stop;
        }
    }
    return 0;
}

} // namespace
} // namespace ledgercore::cli

#ifndef LEDGERCORE_VERSION
#error "LEDGERCORE_VERSION must be defined by CMake (the project() version)"
#endif

int main(int argc, char** argv) {
    try {
        // Handled before any session exists: prints only the version, so
        // scripts can consume the output directly.
        if (argc == 2 && std::string(argv[1]) == "--version") {
            std::cout << LEDGERCORE_VERSION << "\n";
            return 0;
        }

        ledgercore::cli::LedgerSession session(ledgercore::domain::Currency("USD"));

        if (argc == 1) {
            return ledgercore::cli::runRepl(session);
        }
        if (argc == 3 && std::string(argv[1]) == "--script") {
            return ledgercore::cli::runScript(argv[2], session);
        }

        std::cerr << "usage: ledgercore [--script <path> | --version]\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "internal error: " << e.what() << "\n";
        return 3;
    } catch (...) {
        std::cerr << "internal error: unknown exception\n";
        return 3;
    }
}
