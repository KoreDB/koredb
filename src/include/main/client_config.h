#pragma once

#include <cstdint>
#include <string>

#include "common/enums/path_semantic.h"

namespace koredb {
namespace main {

struct ClientConfigDefault {
    // 0 means timeout is disabled by default.
    static constexpr uint64_t TIMEOUT_IN_MS = 0;
    static constexpr uint32_t VAR_LENGTH_MAX_DEPTH = 30;
    static constexpr uint64_t SPARSE_FRONTIER_THRESHOLD = 1000;
    static constexpr bool ENABLE_SEMI_MASK = true;
    static constexpr bool ENABLE_ZONE_MAP = true;
    static constexpr bool ENABLE_PROGRESS_BAR = false;
    static constexpr uint64_t SHOW_PROGRESS_AFTER = 1000;
    static constexpr common::PathSemantic RECURSIVE_PATTERN_SEMANTIC = common::PathSemantic::WALK;
    static constexpr uint32_t RECURSIVE_PATTERN_FACTOR = 100;
    static constexpr bool DISABLE_MAP_KEY_CHECK = true;
    static constexpr uint64_t WARNING_LIMIT = 8 * 1024;
    static constexpr bool ENABLE_PLAN_OPTIMIZER = true;
    static constexpr bool ENABLE_INTERNAL_CATALOG = false;
    // When true (default), a read-only query that hits its timeout stops early and returns the
    // tuples produced so far, with QueryResult::isTruncated() set to true so the caller knows the
    // result is partial. When false, such a query is instead aborted with an "Interrupted." error.
    // Write queries always abort on timeout regardless of this setting.
    static constexpr bool ENABLE_PARTIAL_RESULT_ON_TIMEOUT = true;
    // 0 means no per-query memory limit (only the database-wide buffer pool size applies).
    static constexpr uint64_t QUERY_MEMORY_LIMIT = 0;
    // When true, an eligible HASH_JOIN runs the out-of-core (Grace) path that radix-partitions both
    // sides and spills to disk under a memory budget, so a join whose build side does not fit in
    // memory can still complete. ON by default: only conservatively-eligible joins take the Grace
    // path (INNER, single-chunk, scalar/string columns, no nested/NODE/REL) and every other shape
    // silently falls back to the in-memory join; the earlier NULL-keyed self-join divergence was a
    // bug in the in-memory path (now fixed) rather than in Grace (see
    // docs/resource-limits-and-spilling.md).
    static constexpr bool SPILL_HASH_JOIN = true;
    // Per-operator memory budget (bytes) for the Grace hash-join path. 0 means "derive it": the
    // per-query memory limit if set, otherwise the whole buffer pool. A small value forces spilling.
    static constexpr uint64_t SPILL_HASH_JOIN_BUDGET = 0;
    // When true, an eligible hash aggregation (GROUP BY) radix-partitions its raw input rows by
    // group-key hash and spills them to disk under a memory budget, so a GROUP BY whose input does
    // not fit in memory can still complete. On by default; with the derived budget (whole buffer
    // pool) it only spills to disk near the memory ceiling.
    static constexpr bool SPILL_AGGREGATE = true;
    // Per-operator memory budget (bytes) for the spilling aggregation path. 0 = derive (as for the
    // hash-join budget). A small value forces spilling.
    static constexpr uint64_t SPILL_AGGREGATE_BUDGET = 0;
    // When true, an eligible plain ORDER BY (no LIMIT) runs the out-of-core external merge sort:
    // it spills sorted runs to disk under a memory budget and k-way merges them, so an ORDER BY whose
    // result does not fit in memory can still complete. OFF by default (opt-in): v1 only supports
    // fixed-width (non-STRING, non-nested) keys and single-threaded execution; everything else falls
    // back to the in-memory sort.
    static constexpr bool SPILL_ORDER_BY = false;
    // Per-operator memory budget (bytes) for the external merge sort. 0 = derive (as for the hash-join
    // budget). A small value forces spilling.
    static constexpr uint64_t SPILL_ORDER_BY_BUDGET = 0;
};

struct ClientConfig {
    // System home directory.
    std::string homeDirectory;
    // File search path.
    std::string fileSearchPath;
    // If using semi mask in join.
    bool enableSemiMask = ClientConfigDefault::ENABLE_SEMI_MASK;
    // If using zone map in scan.
    bool enableZoneMap = ClientConfigDefault::ENABLE_ZONE_MAP;
    // Number of threads for execution.
    uint64_t numThreads = 1;
    // Timeout (milliseconds).
    uint64_t timeoutInMS = ClientConfigDefault::TIMEOUT_IN_MS;
    // Variable length maximum depth.
    uint32_t varLengthMaxDepth = ClientConfigDefault::VAR_LENGTH_MAX_DEPTH;
    // Threshold determines when to switch from sparse frontier to dense frontier
    uint64_t sparseFrontierThreshold = ClientConfigDefault::SPARSE_FRONTIER_THRESHOLD;
    // If using progress bar.
    bool enableProgressBar = ClientConfigDefault::ENABLE_PROGRESS_BAR;
    // time before displaying progress bar
    uint64_t showProgressAfter = ClientConfigDefault::SHOW_PROGRESS_AFTER;
    // Semantic for recursive pattern, can be either WALK, TRAIL, ACYCLIC
    common::PathSemantic recursivePatternSemantic = ClientConfigDefault::RECURSIVE_PATTERN_SEMANTIC;
    // Scale factor for recursive pattern cardinality estimation.
    uint32_t recursivePatternCardinalityScaleFactor = ClientConfigDefault::RECURSIVE_PATTERN_FACTOR;
    // Maximum number of cached warnings
    uint64_t warningLimit = ClientConfigDefault::WARNING_LIMIT;
    bool disableMapKeyCheck = ClientConfigDefault::DISABLE_MAP_KEY_CHECK;
    // If enable plan optimizer
    bool enablePlanOptimizer = ClientConfigDefault::ENABLE_PLAN_OPTIMIZER;
    // If use internal catalog during binding
    bool enableInternalCatalog = ClientConfigDefault::ENABLE_INTERNAL_CATALOG;
    // If a read-only query that times out should return partial results instead of erroring.
    bool enablePartialResultOnTimeout = ClientConfigDefault::ENABLE_PARTIAL_RESULT_ON_TIMEOUT;
    // Per-query soft memory limit in bytes, checked against the buffer manager's total used memory
    // while the query runs. 0 disables it. When a read-only query exceeds it, the query stops early
    // and returns partial results (QueryResult::isTruncated() == true) instead of failing with an
    // out-of-memory error.
    uint64_t queryMemoryLimit = ClientConfigDefault::QUERY_MEMORY_LIMIT;
    // If an eligible HASH_JOIN should use the out-of-core (Grace) spilling path. See
    // ClientConfigDefault::SPILL_HASH_JOIN.
    bool spillHashJoin = ClientConfigDefault::SPILL_HASH_JOIN;
    // Per-operator memory budget (bytes) for the Grace hash-join path; 0 = derive. See
    // ClientConfigDefault::SPILL_HASH_JOIN_BUDGET.
    uint64_t spillHashJoinBudget = ClientConfigDefault::SPILL_HASH_JOIN_BUDGET;
    // If an eligible hash aggregation should use the out-of-core spilling path. See
    // ClientConfigDefault::SPILL_AGGREGATE.
    bool spillAggregate = ClientConfigDefault::SPILL_AGGREGATE;
    // Per-operator memory budget (bytes) for the spilling aggregation path; 0 = derive. See
    // ClientConfigDefault::SPILL_AGGREGATE_BUDGET.
    uint64_t spillAggregateBudget = ClientConfigDefault::SPILL_AGGREGATE_BUDGET;
    // If an eligible plain ORDER BY should use the out-of-core external merge sort. See
    // ClientConfigDefault::SPILL_ORDER_BY.
    bool spillOrderBy = ClientConfigDefault::SPILL_ORDER_BY;
    // Per-operator memory budget (bytes) for the external merge sort; 0 = derive. See
    // ClientConfigDefault::SPILL_ORDER_BY_BUDGET.
    uint64_t spillOrderByBudget = ClientConfigDefault::SPILL_ORDER_BY_BUDGET;
};

} // namespace main
} // namespace koredb
