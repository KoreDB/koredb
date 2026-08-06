# Resource limits, partial results, and out-of-core spilling

This document describes the query resource-limit features (timeout and memory) that return
**partial results** instead of failing, and the **out-of-core spilling** foundation they build
toward. It also records the design and the accounting constraints for the remaining spilling work.

## Motivation

Complex queries (especially many-to-many joins) can grow intermediate state until the query either
runs for a very long time or exhausts memory. Two knobs let a caller bound this and degrade
gracefully instead of losing all work:

- a **time budget** — stop after N milliseconds, return what was produced so far;
- a **memory budget** — stop when the query's buffer usage crosses a limit, return what was produced
  so far.

In both cases the result is a valid **prefix/subset** of the full result, flagged so the caller can
decide how to continue (larger budget, or paginate with `SKIP`/`LIMIT`).

## Features (available now)

### Timeout → partial results

- Setting: `enable_partial_result_on_timeout` (BOOL, **default `true`**).
- With it enabled, a **read-only** query that hits its `timeout` stops early and returns the tuples
  produced so far. With it `false`, the query aborts with an `"Interrupted."` error (the historical
  behaviour). Write queries always abort on timeout (returning a partial result would commit a
  partial mutation).

### Memory limit → partial results

- Setting: `query_memory_limit` (UINT64 bytes, default `0` = disabled).
- When a **read-only** query's buffer-manager used memory crosses the limit, it stops early and
  returns partial results instead of failing with `"Unable to allocate memory"`. This is the
  Redis-`maxmemory`-style "degrade gracefully, don't blow up" behaviour. The database-wide
  `bufferPoolSize` still applies as the hard ceiling; `query_memory_limit` is a per-query soft cap
  below it.

### Result inspection

- `QueryResult::isTruncated()` — `true` if the result is a partial prefix.
- `QueryResult::getTruncationReason()` — `"timeout"`, `"memory_limit"`, or `""` (complete).

Exposed across all bindings: C++, C API (`kuzu_query_result_is_truncated`,
`kuzu_query_result_get_truncation_reason`), Node.js (`result.isTruncated()`,
`result.getTruncationReason()`), Python (`result.is_truncated()`,
`result.get_truncation_reason()`), Java (`result.isTruncated()`, `result.getTruncationReason()`).

### Example (Node.js)

```js
await conn.query("CALL query_memory_limit=268435456;");   // 256 MiB per query
conn.setQueryTimeout(5000);                                // 5 s
const result = await conn.query("MATCH (n)-[r]->(m) RETURN * LIMIT 2000");
if (result.isTruncated()) {
  console.log("partial result, reason:", result.getTruncationReason());
  // "timeout"  -> retry with a larger timeout
  // "memory_limit" -> narrow the query or paginate with SKIP/LIMIT
}
```

## Mechanism: cooperative graceful stop

The historical timeout path *threw* `InterruptException`, which unwound the operator stack and
discarded every partially-collected tuple. Partial results require instead a **cooperative stop**:

1. A soft limit fires:
   - timeout — the task scheduler (`task_scheduler.cpp`) sets `ActiveQuery::timedOut` (when armed);
   - memory — `ClientContext::exceededMemoryLimit()` compares `BufferManager::getUsedMemory()` to
     the armed per-query limit inside `PhysicalOperator::getNextTuple`.
2. `PhysicalOperator::getNextTuple` returns `false` (end-of-stream) instead of throwing. Every parent
   operator treats `false` as "no more input" and finishes normally, so the `ResultCollector` merges
   the tuples it already accumulated. Because each row that reached the collector is a fully-computed
   result tuple, the collected rows are always a **valid subset** of the full result.
3. `ClientContext::executeNoLock` records the reason on the `QueryResult`
   (`setTruncationReason(...)`).

"Armed" means the query is read-only **and** the relevant limit is set. The check is gated so it
costs a single comparison when no limit is configured.

### Known limitations

- **GDS / recursive algorithms** run tight internal loops that bypass `getNextTuple`. They cannot
  produce partial results, so they *enforce* the timeout by throwing (`interruptedOrTimedOut()`), i.e.
  a GDS query that times out errors rather than returning a partial result.
- **`ORDER BY`** interrupted during its accumulation phase returns a sorted *subset*, not necessarily
  the true top-N (once it reaches the scan phase, partial output is a true prefix).
- The memory limit is checked against **total** buffer-manager used memory (intermediates plus
  cached pages), not a per-query working-set counter, so it is an approximation for concurrent
  workloads.

## Out-of-core spilling foundation

The end goal is DuckDB-style out-of-core execution: when memory is tight, spill intermediate state
to disk and *complete* the query, rather than stopping with a partial result. Two reusable
foundation pieces exist:

### 1. Generic spill registry — `SpillableComponent`

`storage::Spiller` was generalized from a hard-coded set of `ChunkedNodeGroup*` to a set of
`SpillableComponent*` (`storage/buffer_manager/spillable.h`). Any component that holds
MemoryManager-backed buffers can register (`addUnusedComponent`) and be asked to spill under memory
pressure (`claimNextComponent` → `SpillableComponent::spillToDisk`). `ChunkedNodeGroup` implements
the interface; behaviour is unchanged.

### 2. Position-independent FactorizedTable serialization

`FactorizedTable::serialize` / `deserialize` (`processor/result/factorized_table.cpp`) write a
table's rows to a **pointer-free** byte stream and read them back at a different address. Each row is
flattened via `FlatTupleIterator` and each value is written self-describingly through
`Value::serialize`, so strings/lists/structs are stored **by value**, not by the absolute pointers
they use in the in-memory overflow buffer. This is the primitive that makes spilling correct for
variable-length data (the reason the raw-buffer spill used by the COPY partitioner cannot be reused
for query results). Currently intended for flat result tables (it flattens factorized columns).

### 3. Radix-partitioned spillable tuple store — `PartitionedFactorizedTable`

`processor/result/partitioned_factorized_table.{h,cpp}` is the core building block for out-of-core
(Grace) hash join and partitioned aggregation. It holds `2^logNumPartitions` flat `FactorizedTable`
partitions and:

- **Scatters** a batch of (unflat) vectors into partitions by the high bits of a per-row hash
  (`appendVectors`), reusing `FactorizedTable::append` so variable-length columns are materialized
  correctly. The high bits select the partition so they don't collide with the low bits an
  in-partition hash table indexes on.
- **Spills / reloads** individual partitions (`spillPartition` / `reloadPartition`) via the
  serialization primitive above: a partition is written to a temp file as a position-independent byte
  range and its `DataBlocks` freed; reload reads the range back and deserializes. Appending to, or
  reading, a spilled partition transparently reloads it.
- Exposes an **operator-triggered spill driver** (`spillToReduceResidentBytesTo`,
  `spillLargestResidentPartition`, `getResidentTupleBytes`) so a build side can bound its own peak
  footprint to roughly one partition.

**Memory semantics** (the load-bearing detail, verified against `MemoryManager`): freeing a
partition's 256 KiB page buffers returns them to `MemoryManager::freePages`, and a reused free page
is re-`pin`ned on the `UNLOCKED` path *without* another `BufferManager::reserve` — so spilling a
partition lets a later partition/hash-table allocation reuse those pages with **no growth in
`usedMemory`**. The guarantee is therefore a *page-reuse* bound on the operator's own peak, not a
drop in the global counter; spilling is **operator-triggered**, not buffer-manager-triggered (a
`SpillResult` of `{0,0}` would be useless to `reserve`). This is why it does **not** register with
`Spiller`.

Unit tests (`buffer_manager_test.cpp`): `PartitionedFactorizedTableSpillReload` (scatter routing,
spill-all, reload round-trip incl. strings, reload-on-append) and `PartitionedFactorizedTableBudgetSpill`
(budget-driven spill driver preserves all tuples and stays reloadable).

## Remaining work (the large, careful pieces)

**Operator integration of the Grace hash join is the remaining large piece.** The component above is
the reusable core; wiring it into the live `HASH_JOIN` operator is a dedicated, factorization-aware
effort with no safe partial landing, for the reasons below.

1. **Live result-table spill integration.** Wrap a spillable result `FactorizedTable`: serialize to a
   temp file it owns, free its blocks, reload before consumption, and register for the
   memory-pressure trigger.

   **Accounting is the crux.** There are two spill strategies with different memory semantics:
   - *Raw-buffer spill* (what `ChunkedNodeGroup`/`ColumnChunkData` do via `MemoryBuffer::setSpilledToDisk`):
     frees the physical buffer and returns a `SpillResult{memoryFreed, memoryNowEvictable}` that the
     buffer manager uses to update `usedMemory`/`nonEvictableMemory`. Correct **only** for
     position-independent data (fixed-size columns, no overflow pointers).
   - *Serialized spill* (the `FactorizedTable::serialize` path): correct for all types, but freeing
     the source blocks afterwards goes through the normal `MemoryBuffer` destructor —
     `MemoryManager::updateUsedMemoryForFreedBlock` **decrements `usedMemory` only for malloc'd
     (>256 KiB) buffers**; 256 KiB temp-page buffers are returned to `freePages` (reusable but still
     counted in `usedMemory`). So a serialized spill bounds *future* growth (pages get reused) but
     does not by itself lower `usedMemory`, and it must **not** also report a `SpillResult` to the
     buffer manager or memory would be double-counted.

   Getting this contract right, with a unit test that asserts `usedMemory` behaves correctly across
   spill+reload, is the first task of the integration.

2. **`ORDER BY` external merge sort** — spill sorted runs, k-way merge from disk.
3. **Partitioned (Grace) hash join operator** — the piece that actually fixes many-to-many join
   blow-up. The reusable core (`PartitionedFactorizedTable`) exists; what remains is the operator
   wiring, which is substantial:
   - **Two-phase probe.** Kuzu's probe is a streaming pull (`HashJoinProbe::getNextTuplesInternal`
     pulls one probe chunk and looks it up in a fully-built in-memory hash table). A non-partitioned
     probe requires the *entire* build side resident. Grace requires restructuring the probe into a
     blocking two-phase operator: phase 1 consumes and partitions **all** probe input (spilling probe
     partitions to match the build partitions); phase 2 loops partition-by-partition — build a
     `JoinHashTable` from build partition *i*, then scan probe partition *i* and emit matches. This
     changes the operator from streaming to blocking when spilling is active.
   - **Factorized payloads.** `PlanMapper::createHashBuildInfo` stores an unflat payload (a payload in
     a different chunk from the keys) as an `overflow_value_t` **factorized** column — exactly the
     many-to-many `RETURN *` case. `PartitionedFactorizedTable` targets **flat** tuples (its serialize
     path flattens factorization, which would materialize the cross-product). So the operator either
     restricts to flat payloads first, or the partition store must learn to carry factorized columns
     without flattening (pointer swizzling of the overflow references on spill/reload).
   - **All join types** (inner / left-outer / mark / count) and **flat & unflat probe keys**, plus the
     multi-threaded build-merge, must all be handled or explicitly gated.
   - Gate the whole path behind a setting (default off) so the in-memory path stays byte-for-byte
     unchanged, and add e2e `.test` cases that force spilling with a low `query_memory_limit`.
4. **Partitioned aggregation** — analogous to (3) for the aggregate hash table; reuses
   `PartitionedFactorizedTable` directly.

All reuse the `SpillableComponent` registry (for the raw-buffer, buffer-manager-triggered path) or
`PartitionedFactorizedTable` / the `FactorizedTable` serialization primitive (for the operator-
triggered path on variable-length data).
