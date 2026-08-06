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

The executor also exposes a resumable **streaming** probe (`initProbeStream` / `getNextChunk`) that
emits the join one factorized chunk at a time (probe columns flat, build payloads unflat) instead of
materializing it whole — validated against the materialized reference
(`GraceHashJoinExecutorStream{Inner,Left}Join`). It is a reusable primitive for a future
factorized-output operator path; the live operator (section 5) currently uses the materialized form.

**Scope / not yet done:** inner & left join; **flat** payload columns (fixed-layout and
variable-length strings, but not factorized/unflat payloads). See remaining work below.

### 5. Live `HASH_JOIN` operator integration (out-of-core, gated)

`spill_hash_join` (BOOL, **default `false`**) turns an eligible `HASH_JOIN` into the out-of-core
path; when off, the in-memory path is byte-for-byte unchanged. `spill_hash_join_budget` (UINT64
bytes, `0` = derive from `query_memory_limit` else the buffer-pool size) sets the per-operator spill
budget. The wiring (`map_hash_join.cpp`, `hash_join_build.cpp`, `hash_join_probe.cpp`):

- `PlanMapper::mapHashJoin` computes plan-time `GraceHashJoinInfo` (join key/payload types and the
  probe-side non-key output columns to capture) and marks the join *eligible* only for a conservative
  shape (see below). It is stored on the shared `HashJoinSharedState`.
- `HashJoinBuild`, when `spill_hash_join` is on, `numThreads == 1`, and the join is eligible, scatters
  every build row into a shared `GraceHashJoinExecutor` (`appendBuild`, spilling under the budget)
  instead of building an in-memory hash table.
- `HashJoinProbe` then drains its probe child into `appendProbe`, calls `computeInnerJoin` once to
  **materialize** the result, and scans it back into the output vectors across `getNextTuplesInternal`
  calls (`FactorizedTable::scan`), one row per call for a flat output chunk or a vector of rows for an
  unflat one.

**Why materialize, not stream, in v1.** The probe operator is not a pipeline breaker: its result set
*is* the output result set. For the eligible shape all output columns live in **one** data chunk, so
the natural emission is to scan a fully-materialized (row-per-tuple) table back into that single
chunk. The factorized streaming primitive assumes probe-flat / build-unflat live in *separate* chunks
— true for other plans but not this one — so it is kept as a library primitive (above) for later.

**Eligibility (conservative; anything else silently uses the in-memory path):** INNER only, no mark,
non-empty build payloads, `numThreads == 1`, the build side is a single data chunk, and **all** output
columns live in a single data chunk. Multi-chunk (factorized) outputs, `RETURN *`-style unflat build
payloads, LEFT/MARK/COUNT joins, and parallel execution all fall back. Silent fallback is safe because
the fallback is the proven in-memory join.

**Verification = differential.** `buffer_manager_test`'s `GraceHashJoinDifferential` runs several
many-to-many self-joins with `spill_hash_join` off vs on and asserts identical result multisets;
`GraceHashJoinSpillDifferential` does the same over 2000 generated rows with a 4 KiB budget that forces
partitions to spill and reload. Both assert the Grace path actually activated (a process-wide
activation counter) so the check is never vacuous. No regression: `api_test` 97/97, `buffer_manager_test`
20/20, e2e `match` and `generic_hash_join` green.

### 6. Out-of-core aggregation executor — `PartitionedAggregateExecutor`

`processor/operator/aggregate/partitioned_aggregate_executor.{h,cpp}` is the aggregation analogue of
the join executor: an out-of-core hash `GROUP BY` over an input that may not fit in memory.

- `append(keyVectors, aggInputVectors)` scatters the **raw input rows** (`[groupKeys…,
  aggInputCols…]`) into a `PartitionedFactorizedTable` by `hash(group keys)`, spilling under a memory
  budget. Because rows sharing a group key hash to the same partition, every group lives entirely
  within one partition.
- `computeAggregates()` processes one partition at a time: reload it, run a **fresh**
  `AggregateHashTable` over its rows (groups are disjoint across partitions), `finalizeAggregateStates`,
  emit `[keys…, aggResults…]`, and free it. Results are simply concatenated — **no cross-partition
  merge**. Peak memory is bounded to roughly one partition's rows plus its group table.

**Why this sidesteps the hard problem.** Only *raw input columns* are ever written to disk (via the
`FactorizedTable` serialization primitive, which relocates variable-length/overflow data correctly).
Aggregate **states** — which for `min/max(STRING)`, `collect(LIST)`, etc. carry overflow pointers that
would be expensive to serialize — **never leave memory**, because each partition is aggregated in one
in-memory pass. So this works for *every* aggregate function, including stateful ones, without any
state serialization or pointer swizzling. This is the key difference from the existing in-memory
`HashAggregate`, whose per-partition queues hold partially-aggregated **states**.

**Scope (v1):** non-distinct aggregates; group keys stored flat and sharing one input state with the
aggregate-input columns; `ResultSet` multiplicity of 1; no dependent (payload) keys. Not thread-safe
(single-thread, mirroring the join executor). These match the conservative first cut; broadening is
the operator-wiring increment (below).

**Verification = differential.** `buffer_manager_test`'s `PartitionedAggregateExecutorSpillIntKey`
(4 KiB budget, forced spill/reload during append) and `PartitionedAggregateExecutorNoSpillIntKey`
(fully in memory) both aggregate 5000 rows into 20 groups with `COUNT(*)` + `SUM` and must match the
same brute-force reference, proving spilling is transparent; `PartitionedAggregateExecutorSpillStringKey`
adds a STRING group key so the forced-spill path also exercises overflow serialization of the raw
rows. No regression: `buffer_manager_test` 23/23.

## Remaining work (the large, careful pieces)

The reusable core (`PartitionedFactorizedTable`), a working out-of-core join (`GraceHashJoinExecutor`,
inner+left, flat payloads, materialized + streaming), a **gated live join operator** (section 5, INNER
+ single-chunk output), and an out-of-core **aggregation executor** (section 6, library-level) now
exist and are proven correct under spilling. What remains to broaden coverage and reach the hard
`RETURN *` case:

1. **Broaden operator eligibility.** The live operator (section 5) is deliberately narrow: INNER,
   single-thread, single-chunk output. Extending it means (a) the **LEFT** null-padding path
   end-to-end (the executor already does it; only the operator emission is unverified), (b)
   **multi-threaded** build/probe — the hard part, since the probe side has no cross-thread barrier
   today, so a barrier or per-thread partition merge is needed, and (c) **factorized / multi-chunk
   output** — emit probe-flat / build-unflat across separate output chunks using the executor's
   streaming `getNextChunk` instead of the single-chunk materialize+scan.

2. **Factorized (unflat) payloads — the `RETURN *` case.** `PlanMapper::createHashBuildInfo` stores a
   payload from a different chunk than the keys as an `overflow_value_t` **factorized** column.
   `PartitionedFactorizedTable` / the executor target **flat** payloads (the serialize path flattens
   factorization, which would materialize the cross-product). Supporting factorized payloads without
   flattening needs pointer *swizzling* of the overflow references on spill/reload (convert absolute
   pointers to relative offsets before writing, back to pointers after). This is what many real
   many-to-many `RETURN *` queries need, and is the largest remaining sub-piece. It is also the
   gateway to the multi-chunk operator output in item 1(c).

3. **Remaining join types & keys** — mark / count joins and multi-column / unflat probe keys in the
   executor (inner + left + single/composite flat key are done).

5. **`ORDER BY` external merge sort** — spill sorted runs, k-way merge from disk (independent of the
   join work; reuses the `FactorizedTable` serialization primitive).

6. **Partitioned aggregation — live operator wiring.** The library executor exists and is verified
   (section 6). What remains is the gated operator integration, analogous to the join's section 5: a
   `spill_aggregate` setting, and routing `HashAggregate`'s build → finalize → scan pipeline through
   the executor when eligible + single-threaded. Broadening the executor itself (distinct aggregates,
   dependent/payload keys, `ResultSet` multiplicity > 1, multi-state inputs) is the follow-on.

All reuse the `SpillableComponent` registry (for the raw-buffer, buffer-manager-triggered path) or
`PartitionedFactorizedTable` / the `FactorizedTable` serialization primitive (for the operator-
triggered path on variable-length data).
