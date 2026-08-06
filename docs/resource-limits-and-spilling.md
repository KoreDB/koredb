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

It also supports `merge` (combine per-thread partition tables for a parallel build) and
`freePartition` (discard a consumed partition in O(1) to bound peak memory).

Unit tests (`buffer_manager_test.cpp`): `PartitionedFactorizedTableSpillReload`,
`PartitionedFactorizedTableBudgetSpill`, `PartitionedFactorizedTableLargeMultiPageSpill` (40k rows,
multi-page file I/O), `PartitionedFactorizedTableSinglePartition`, `PartitionedFactorizedTableMerge`,
and `PartitionedFactorizedTableTypesAndNulls` (null + multi-type round-trip).

### 4. Out-of-core join executor — `GraceHashJoinExecutor`

`processor/operator/hash_join/grace_hash_join_executor.{h,cpp}` assembles the pieces above into a
complete out-of-core **inner and left** hash join over inputs that may not fit in memory:

- `appendBuild` / `appendProbe` scatter each side into a `PartitionedFactorizedTable` by
  `hash(join keys)` — the **same** hash `JoinHashTable` computes internally, so the two sides are
  **co-partitioned** (a probe key can only match build keys in its own partition) — and spill under a
  memory budget as they go.
- `computeInnerJoin` / `computeLeftJoin` process one partition at a time: reload the build partition,
  build a real `JoinHashTable`, reload and probe the matching probe partition (`probe` +
  `matchFlatKeys` + `lookup`), and `freePartition` both afterwards. Output columns are
  `[probeKeys…, probePayloads…, buildPayloads…]`, materialized via `FactorizedTable::append`'s
  flatten-on-append (flat probe values replicated across each row's unflat build matches). A left
  join emits a null-padded row for a probe row with no match.

Peak memory is bounded to roughly one partition pair plus the output, instead of the whole build
side. Correctness is proven against a brute-force nested-loop reference under **forced spilling**
(`GraceInnerJoinWithSpilling`, `GraceLeftJoinWithSpilling` validate the algorithm directly;
`GraceHashJoinExecutorInnerJoin`, `GraceHashJoinExecutorLeftJoin` validate the production class with a
4 KiB budget that spills during append).

**Scope / not yet done:** inner & left join only; **flat** payload columns (fixed-layout and
variable-length strings, but not factorized/unflat payloads); output is **materialized** (not
streamed); single-threaded. See remaining work below.

## Remaining work (the large, careful pieces)

The reusable core (`PartitionedFactorizedTable`) and a working out-of-core join
(`GraceHashJoinExecutor`, inner+left, flat payloads) now exist and are proven correct under spilling.
What remains to make out-of-core joins **query-visible** and cover the hard `RETURN *` case:

1. **Wire `GraceHashJoinExecutor` into the live `HASH_JOIN` operator, gated.** With the executor in
   place this is now tractable: in a spilling mode, `HashJoinBuild` feeds rows via `appendBuild`
   instead of building one hash table, and `HashJoinProbe` becomes two-phase — phase 1 drains its
   probe child into `appendProbe`, phase 2 calls `computeInnerJoin`/`computeLeftJoin` and scans the
   materialized output across `getNextTuplesInternal` calls. Requires: a setting (default off) so the
   in-memory path is byte-for-byte unchanged; the shared build/probe state; matching the plan's output
   vector positions; a fallback to the in-memory path for unsupported cases; and e2e `.test` cases
   that force spilling with a low `query_memory_limit`. Correctness-critical (a wrong join silently
   returns wrong rows), so it must land fully verified, not in pieces.

2. **Factorized (unflat) payloads — the `RETURN *` case.** `PlanMapper::createHashBuildInfo` stores a
   payload from a different chunk than the keys as an `overflow_value_t` **factorized** column.
   `PartitionedFactorizedTable` / the executor target **flat** payloads (the serialize path flattens
   factorization, which would materialize the cross-product). Supporting factorized payloads without
   flattening needs pointer *swizzling* of the overflow references on spill/reload (convert absolute
   pointers to relative offsets before writing, back to pointers after). This is what many real
   many-to-many `RETURN *` queries need, and is the largest remaining sub-piece.

3. **Streaming output** for the executor (`getNextJoinChunk`) so a huge join result is produced in
   `DEFAULT_VECTOR_CAPACITY` chunks rather than materialized whole — mirrors
   `HashJoinProbe::getInnerJoinResultForFlatKey`'s resumable `nextMatchedTupleIdx` cursor, generalized
   over partitions. Needed before the operator can stream large outputs.

4. **Remaining join types & keys** — mark / count joins and multi-column / unflat probe keys in the
   executor (inner + left + single flat key are done).

5. **`ORDER BY` external merge sort** — spill sorted runs, k-way merge from disk (independent of the
   join work; reuses the `FactorizedTable` serialization primitive).

6. **Partitioned aggregation** — analogous to the join for the aggregate hash table; reuses
   `PartitionedFactorizedTable` directly (routing rows by group-key hash).

All reuse the `SpillableComponent` registry (for the raw-buffer, buffer-manager-triggered path) or
`PartitionedFactorizedTable` / the `FactorizedTable` serialization primitive (for the operator-
triggered path on variable-length data).
