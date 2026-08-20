#pragma once

#include "common/api.h"
#include "storage/buffer_manager/spill_result.h"

namespace koredb {
namespace storage {

// Interface for any component that holds MemoryManager-backed buffers which can be spilled to disk
// under memory pressure. Components register themselves with the Spiller (via
// Spiller::addUnusedComponent) while their data is inactive, and are asked to spill by the buffer
// manager through Spiller::claimNextComponent when memory needs to be reclaimed. A component is
// responsible for reloading its own data before it is accessed again (and for deregistering itself
// via Spiller::clearUnusedComponent so it is not spilled while in use).
class KOREDB_API SpillableComponent {
public:
    virtual ~SpillableComponent() = default;

    // Spills the currently in-memory buffers of this component to disk. Returns the amount of
    // memory that was directly freed and the amount that became evictable as a result.
    virtual SpillResult spillToDisk() = 0;
};

} // namespace storage
} // namespace koredb
