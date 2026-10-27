#include <ECS.hpp>

namespace ecs {
EVE_ECS_DEFAULT_TABLE_API Table &engine_default_table() {
  static Table table;
  return table;
}
EVE_ECS_DEFAULT_TABLE_API Table *&engine_current_slot() {
  thread_local Table *table = nullptr;
  return table;
}
} // namespace ecs
