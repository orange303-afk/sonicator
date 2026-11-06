#include <catch2/catch_test_macros.hpp>
namespace sonicator { const char* version(); }
TEST_CASE("Core exposes an application version", "[scaffold]")
{
    REQUIRE(sonicator::version() != nullptr);
}
