// Test main file - Catch2 provides main() function
// This file is intentionally minimal as Catch2WithMain handles everything

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>

// Simple smoke test to verify test framework is working
TEST_CASE("Framework smoke test", "[smoke]") {
    REQUIRE(1 + 1 == 2);
}