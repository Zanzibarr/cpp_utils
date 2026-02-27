#include <iostream>

#include "scope_guard.hxx"

void simulate_database_work(bool triggerError) {
    std::cout << "\n--- Starting Transaction (Error: " << (triggerError ? "YES" : "NO") << ") ---\n";

    try {
        // 1. Always runs (Cleanup)
        ScopeGuard cleanup = ScopeGuard([]() -> void { std::cout << "[EXIT] Closing database connection.\n"; }, ScopeStrategy::Exit);

        // 2. Runs only on failure (Rollback)
        ScopeGuard rollback = ScopeGuard([]() -> void { std::cout << "[FAIL] Rolling back changes to stable state!\n"; }, ScopeStrategy::Fail);

        // 3. Runs only on success (Commit)
        ScopeGuard commit = ScopeGuard([]() -> void { std::cout << "[SUCCESS] Transaction logged to permanent storage.\n"; }, ScopeStrategy::Success);

        std::cout << "Step 1: Modifying rows...\n";
        if (triggerError) {
            throw std::runtime_error("Database Disk Full!");
        }
        std::cout << "Step 2: Modifications complete.\n";

    } catch (const std::exception& e) {
        std::cout << "Caught exception: " << e.what() << "\n";
    }
}

int main() {
    simulate_database_work(false);  // Success case
    simulate_database_work(true);   // Failure case
    return 0;
}