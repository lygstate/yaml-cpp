#pragma once

#include <list>
#include <memory>

#include "yaml-cpp/dll.h"
#include "yaml-cpp/node/ptr.h"

namespace YAML {
namespace detail {
class node;
struct node_bucket;
}  // namespace detail
}  // namespace YAML

namespace YAML {
namespace detail {

class YAML_CPP_API memory : public ref_counted {
 public:
  node& create_node();
  void merge(memory& rhs);

  memory();
  ~memory();

 private:
  std::unique_ptr<node_bucket> m_nodes;
};

typedef ref_holder<memory> shared_memory;

}
}
