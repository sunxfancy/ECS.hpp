#include "zeroerr.hpp"
#include "ECS.hpp"

TEST_CASE("default current is default_table")
{
  REQUIRE(ecs::current() == &ecs::default_table());
}

TEST_CASE("set_current and ScopedTable restore")
{
  ecs::Table a;
  ecs::Table b;
  ecs::set_current(&a);
  REQUIRE(ecs::current() == &a);
  {
    ecs::ScopedTable guard(b);
    REQUIRE(ecs::current() == &b);
  }
  REQUIRE(ecs::current() == &a);
  ecs::set_current(nullptr);
  REQUIRE(ecs::current() == &ecs::default_table());
}
