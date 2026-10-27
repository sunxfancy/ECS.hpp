#include <ECS.hpp>
#include <thread>

#ifdef _WIN32
extern "C" __declspec(dllimport) bool
ecs_consumer_current_matches(ecs::Table *);
#else
extern "C" bool ecs_consumer_current_matches(ecs::Table *);
#endif

int main() {
  ecs::Table first, second;
  if (!ecs_consumer_current_matches(&ecs::default_table()))
    return 1;
  {
    ecs::ScopedTable outer(first);
    if (!ecs_consumer_current_matches(&first))
      return 2;
    {
      ecs::ScopedTable inner(second);
      if (!ecs_consumer_current_matches(&second))
        return 3;
    }
    if (!ecs_consumer_current_matches(&first))
      return 4;
    bool threadMatches = false;
    std::thread worker([&] {
      if (!ecs_consumer_current_matches(&ecs::default_table()))
        return;
      ecs::ScopedTable scope(second);
      threadMatches = ecs_consumer_current_matches(&second);
    });
    worker.join();
    if (!threadMatches || !ecs_consumer_current_matches(&first))
      return 5;
  }
  if (!ecs_consumer_current_matches(&ecs::default_table()))
    return 6;
  return 0;
}
