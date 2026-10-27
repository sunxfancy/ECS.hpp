#include <ECS.hpp>

#ifdef _WIN32
#define ECS_TEST_EXPORT __declspec(dllexport)
#else
#define ECS_TEST_EXPORT __attribute__((visibility("default")))
#endif
extern "C" ECS_TEST_EXPORT bool
ecs_consumer_current_matches(ecs::Table *table) {
  return ecs::current() == table;
}
