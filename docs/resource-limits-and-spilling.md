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

Exposed across all bindings: C++, C API (`koredb_query_result_is_truncated`,
`koredb_query_result_get_truncation_reason`), Node.js (`result.isTruncated()`,
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

`spill_hash_join` (BOOL, **default `true`**) turns an eligible `HASH_JOIN` into the out-of-core path;
when off, the in-memory path is byte-for-byte unchanged. Only conservatively-eligible joins take the
Grace path and everything else falls back to the in-memory join (see "Eligibility" below). `spill_hash_join_budget` (UINT64 bytes, `0` = derive from `query_memory_limit` else the
buffer-pool size) sets the per-operator spill budget. With the derived budget the operator partitions
in memory and only spills to disk near the buffer-pool ceiling. The wiring (`map_hash_join.cpp`,
`hash_join_build.cpp`, `hash_join_probe.cpp`):

- `PlanMapper::mapHashJoin` computes plan-time `GraceHashJoinInfo` (join key/payload types and the
  probe-side non-key output columns to capture) and marks the join *eligible* only for a conservative
  shape (see below). It is stored on the shared `HashJoinSharedState`.
- `HashJoinBuild`, when the Grace path will run (`willActivateGrace()`: `spill_hash_join` on, eligible
  shape, and the multi-thread activation policy in "Multi-threaded build" below), scatters every build
  row into a `GraceHashJoinExecutor` (`appendBuild`, spilling under the budget) instead of building an
  in-memory hash table — one executor per build thread, merged at finalize.
- `HashJoinProbe` then drains its probe child into `appendProbe` and emits the join in one of two ways
  by output shape, **both bounded to one partition's output at a time** (`computePartitionJoin(p)`
  materializes just partition `p` and frees that partition pair, so the whole join is never resident):
  **single-chunk output** — scan each partition's result back into the one output chunk (`FactorizedTable::scan`,
  a vector of rows per `getNextTuplesInternal` call — vectorized); **multi-chunk (factorized) output** —
  `initFlatStream` / `getNextFlatTuple` stream one flat tuple at a time into the several output chunks
  (each column set to a single value). See "Operator integration — done" under remaining-work item 2 for
  the multi-chunk (unflat-key build) case.

**Eligibility (conservative; anything else silently uses the in-memory path):** (a) INNER or LEFT with
non-empty build payloads, (b) **MARK** (EXISTS / semi / anti-join) with a keys-only build, or (c)
**COUNT** (size / `COUNT{}` subquery) with a pre-aggregated `(key, count)` build (one count payload); in
all cases **no nested/NODE/REL payload or output column**, and each side an *appendable factorization*
(at most one unflat data chunk, all keys in a single chunk). All four join types support **both**
single-chunk output (materialize + vectorized scan) and multi-chunk output (the factorized case — e.g.
an unflat-key build, or an EXISTS/COUNT over a factorized `(a)-[]->(b)` outer — streamed one flat tuple
at a time), and run **single- or multi-threaded** (see "Multi-threaded build" below). A side with more
than one unflat chunk (two crossed `MANY` dimensions) or a composite key spanning chunks falls back.
Silent fallback is safe because the fallback is the proven in-memory join.

**MARK joins.** An `EXISTS { … }` / `NOT EXISTS { … }` subquery compiles to a MARK join whose build
side (the correlated pattern) can be arbitrarily large. `computeMarkJoin` emits exactly one output row
per probe row — `[probeKeys…, probeNonKeys…, mark(BOOL)]` — where `mark` is whether the probe row had
≥1 build match (a NULL join key or an empty build partition ⇒ `false`; Cypher `EXISTS` is two-valued,
so no NULL mark). No build payloads are read. The probe operator drains its child into the (spilling)
probe partitions, then materializes one partition at a time (`computeMarkPartition`) and scans it back
into the single output chunk, so peak memory is bounded to one partition's rows instead of the whole
build side. A **multi-chunk** MARK (EXISTS over a factorized outer, e.g. `(a)-[:knows]->(b)`) streams
via the flat-stream (`FlatStreamMode::MARK`) instead. Verified by `GraceHashJoinMarkDifferential`
(EXISTS/NOT EXISTS, spill off vs on + activation assert), `GraceHashJoinMarkSpillDifferential`
(3000-row build, 4 KiB budget → real disk spill), and `GraceHashJoinMarkCountMultiChunkDifferential`
(factorized outer + multi-chunk activation assert), plus the e2e `generic_hash_join/mark.test` and
`mark_count_multichunk.test`.

**COUNT joins.** A `COUNT { … }` / `size(pattern)` subquery compiles to a COUNT join whose build subtree
is `AGGREGATE(GROUP BY key, COUNT_STAR) → HASH_JOIN_BUILD` — i.e. the build is **pre-aggregated to one
`(key, count)` row per key**, so each probe row has at most one match. This is LEFT-join-shaped (every
probe row emits exactly one output row, `[probeKeys…, probeNonKeys…, count]`), differing only in that a
non-match yields **count 0** instead of a NULL build payload — handled by a `countJoin` flag on the
shared `computeJoin` (only the no-match branch differs). Because the count is an ordinary build payload,
the probe operator reuses the INNER single-chunk emission path verbatim, swapping in
`computeCountPartition`; a **multi-chunk** COUNT (over a factorized outer) streams via the flat-stream
(`FlatStreamMode::COUNT`). Verified by `GraceHashJoinCountDifferential` (tinysnb `COUNT{}` in `RETURN`
and in a `WHERE` filter, spill off vs on + activation assert), `GraceHashJoinCountSpillDifferential`
(2000-key pre-aggregated build, 4 KiB budget → real disk spill), and
`GraceHashJoinMarkCountMultiChunkDifferential` (multi-chunk activation assert), plus the e2e
`generic_hash_join/count.test` and `mark_count_multichunk.test`. (A *node-correlated* `COUNT { MATCH (b) WHERE b.k = a.k }` asserts in the
**in-memory** `getCountJoinResult` under runtime checks — `vectorsToReadInto.size() == 1` — a pre-existing
shape limitation independent of the Grace path; the standard relationship/pattern COUNT is the wired shape.)

**Multi-threaded build.** The build runs in parallel and the probe is forced single-threaded, so the
out-of-core path activates under multi-threaded queries (not only `threads == 1`). Each `HashJoinBuild`
thread builds its **own** executor (its own budget, its own spill files) and hands it to the shared state
(`registerLocalGraceExecutor`); the once-per-pipeline finalize barrier merges them into one
(`mergeGraceExecutors` → `GraceHashJoinExecutor::merge`, partition-by-partition so a key still lands in
one merged partition). The probe declares `isParallel() == false` when the Grace path will run, which
makes `initTask` mark the **probe pipeline** single-threaded while the (separate child-task) build
pipeline stays parallel — so a serial probe drains + emits partition by partition with no mid-pipeline
barrier. This mirrors the aggregate's per-thread-executor + merge pattern. `willActivateGrace()` decides
purely from the *live* config (a `ClientContext*` the mapper stores on the shared state) plus plan-time
eligibility, so build and probe agree even though `isParallel()` is evaluated before execution (when a
runtime activation flag would not yet be set).

*Activation policy (deliberately conservative under multi-threading).* Forcing the probe serial costs
probe parallelism, so on `threads > 1` the Grace path activates **only when the user signals a memory
bound** — an explicit `spill_hash_join_budget` or `query_memory_limit` — keeping the fast in-memory
parallel join as the default for queries that fit; on `threads == 1` (no probe parallelism to lose) it
activates whenever `spill_hash_join` is on. A truly parallel out-of-core probe (a sink that drains +
partitions the probe input, then a parallel offset-addressed scan that emits, like the aggregate scan)
would lift the serial-probe restriction and let multi-threaded spilling be the unconditional default;
that two-pipeline restructure is the main follow-on. Verified by `GraceHashJoinMultiThreadDifferential`
(threads=4, INNER/MARK/COUNT/multi-chunk, spill off vs on + activation assert) and
`GraceHashJoinMultiThreadSpillDifferential` (3000-row build, 4 KiB budget → parallel build spills, merges,
serial probe reloads).

**LEFT joins.** `computeJoin(isLeftJoin)` null-pads any probe row that finds no build match; a NULL join
key (which never matches) takes the same path, so LEFT semantics are preserved for unmatched and
NULL-key probe rows alike. The probe operator drives it via `computeLeftJoin()`, and probe rows are
partitioned/spilled *without* discarding NULL keys (`appendToPartitions` keeps the full selection), so
nothing is lost on the way to disk. `GraceHashJoinLeftDifferential` (a `OPTIONAL MATCH` with a unique
build key over matched / unmatched / NULL-key probe rows, build side spilling at a 4 KiB budget) and e2e
`generic_hash_join/left_spill.test` verify the emission end-to-end.

The nested-type exclusion closes a real bug: a NODE/REL/LIST value occupies a single schema position
(so the single-chunk check passes) but expands to multiple runtime vectors, which the single-chunk
flat emission cannot reproduce — `MATCH (a),(b) WHERE a.ID=b.ID RETURN a, b` returned 1 row of 8 before
the fix. `GraceHashJoinReturnNodeDifferential` locks this down.

**NULL keys (and a pre-existing in-memory bug this surfaced).** A Grace INNER equi-join correctly
skips NULL keys (a NULL never matches). Auditing this against a NULL-keyed self-join exposed a
**pre-existing bug in the in-memory hash join**: on `MATCH (a),(b) WHERE a.k=b.k` with interspersed
NULLs on the build side it *undercounted*, returning 10 of the 20 matching rows per key while Grace
returned all 20. Root cause: `ValueVector::discardNull` compacted the selection vector correctly only
when it was unfiltered; on an already-filtered selection it wrote each surviving position back to its
original index and merely shrank the size, leaving the (possibly NULL-containing) prefix selected and
silently dropping the tail non-null build rows. The two branches are now unified so compaction happens
identically in both cases. Regression: `generic_hash_join/null_build_key.test` (spill-agnostic, checks
the in-memory counts against a join-free ground truth) and `GraceHashJoinNullKeyCorrectness`.

**Nested group keys in the spilling aggregation.** The out-of-core aggregation serializes group and
dependent (payload) keys into its spill partitions and hashes/compares them on re-scan. A nested/
NODE/REL key is not reliably round-tripped there — a `STRUCT`-with-`LIST` group key (e.g. the tinysnb
`RETURN o.state, count(*)`) crashed on re-scan — so `computeSpillAggregateInfo` now marks any nested
group/dependent key ineligible and falls back to the in-memory aggregation (nested aggregate *inputs*
are unaffected: they are re-fed through the aggregate function, not treated as keys). Covered by
`SpillAggregateNestedDifferential` (LIST + STRUCT keys) and the `agg/hash` `StructHashTest` e2e.

**Verification = differential.** `buffer_manager_test`'s `GraceHashJoinDifferential` runs several
many-to-many self-joins with `spill_hash_join` off vs on and asserts identical result multisets;
`GraceHashJoinSpillDifferential` does the same over 2000 generated rows with a 4 KiB budget that forces
partitions to spill and reload; `GraceHashJoinReturnNodeDifferential` covers scalar-vs-nested `RETURN`
shapes; `GraceHashJoinNullKeyCorrectness` checks NULL-key results against the true (join-free) answer.
No regression: `api_test` 97/97, e2e `match` and `generic_hash_join` green.

### 6. Out-of-core aggregation executor — `PartitionedAggregateExecutor`

`processor/operator/aggregate/partitioned_aggregate_executor.{h,cpp}` is the aggregation analogue of
the join executor: an out-of-core hash `GROUP BY` over an input that may not fit in memory.

- `append(keyVectors, aggInputVectors)` scatters the **raw input rows** (`[groupKeys…,
  aggInputCols…]`) into a `PartitionedFactorizedTable` by `hash(group keys)`, spilling under a memory
  budget. Because rows sharing a group key hash to the same partition, every group lives entirely
  within one partition. It branches on the group-by state exactly as the join executor does: an
  **unflat** group-by input scatters each row by hash; a **flat** group-by input (one group per batch —
  e.g. a correlated subquery aggregate like `RETURN a, COUNT { MATCH (a)-->() }`, or a single-group
  aggregate) routes the whole group to `hash(key)`'s partition via `appendFactorizedGroup` (flat key +
  multiplicity broadcast over any unflat aggregate-input column). Missing that branch was a crash:
  `appendVectors` asserts an unflat chunk, so a flat group-by input hit the assertion under the
  default-on `spill_aggregate` (regression-covered by `PartitionedAggregateExecutorFlatKey{Spill,NoSpill}`
  and e2e `subquery/count.test`).
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

**Scope:** non-distinct aggregates; group keys stored flat and sharing one input state with the
dependent-key and aggregate-input columns. **Dependent (payload) keys** and **per-row multiplicity**
(stored as a hidden column and re-applied over contiguous equal-multiplicity runs) are supported. Not
thread-safe (single-thread, mirroring the join executor). Remaining executor gaps: distinct
aggregates and multi-state inputs — broadened alongside the operator wiring (below).

**Verification = differential.** `buffer_manager_test` (all vs a brute-force reference, 5000–6000
rows, 4 KiB budget forcing spill/reload during append unless noted):
`PartitionedAggregateExecutorSpillIntKey` + `…NoSpillIntKey` (fully in memory) prove spilling is
transparent for `COUNT(*)` + `SUM`; `…SpillStringKey` adds a STRING group key so the forced-spill path
exercises overflow serialization of the raw rows; `…MinStringSpill` proves a **stateful** overflow
aggregate (`MIN(STRING)`) is correct under spilling (states never leave memory); `…DependentKeySpill`
carries a STRING payload column through; `…MultiplicitySpill` feeds batches of mixed multiplicity so
partitions see contiguous runs the executor must re-weight. No regression: `buffer_manager_test`
26/26.

### 7. Live hash-aggregation operator integration (out-of-core, gated)

`spill_aggregate` (BOOL, **default `true`**) turns an eligible `GROUP BY` into the out-of-core path;
when off, the in-memory path is byte-for-byte unchanged. `spill_aggregate_budget` (UINT64 bytes,
`0` = derive from `query_memory_limit` else the buffer-pool size) sets the per-operator spill budget
(divided across build threads). The wiring (`map_aggregate.cpp`, `hash_aggregate.{h,cpp}`):

- `PlanMapper::createHashAggregate` computes plan-time `SpillAggregateInfo` (eligibility plus the
  per-aggregate input and result types) and stores it on the shared `HashAggregateSharedState`.
- `HashAggregate::initLocalStateInternal` calls `tryActivateGrace` at **runtime** (not plan-mapping
  time, so it sees the live `spill_aggregate` / `threads` settings — the same reason the join decides
  in `HashJoinBuild`), then, if grace is active, builds this thread's **own** executor
  (`createLocalExecutor`, budget = total / `numThreads`).
- `HashAggregate::executeInternal` scatters every raw input row into **this thread's** executor
  (`append`, spilling under the per-thread budget) instead of the local partitioning hash table, and
  hands the executor to the shared state (`registerLocalExecutor`) once its input is drained. Per-thread
  executors mean the parallel build needs no cross-thread synchronization.
- `HashAggregateFinalize` (which runs on every thread) does the finalize **once** (guarded by
  `graceFinalized` under `graceMtx`): `merge` the per-thread executors into one, then
  `PartitionedAggregateExecutor::finalizeToTables` aggregates each spilled partition into a finalized
  `AggregateHashTable`. Because those tables have the **same schema** as the in-memory
  `globalPartitions`, the scan path is reused unchanged: only `getNumTuples` / `getPartitionForOffset`
  (and `finalizePartitions` / `assertFinalized`) gained a `graceActive`-guarded branch that reads the
  executor's tables instead. Every grace branch is inert when `spill_aggregate` is off.

**Eligibility (conservative; anything else silently uses the in-memory path):** at least one
non-distinct aggregate; no per-aggregate factorized `multiplicityChunks` (the executor applies only the
scalar `ResultSet` multiplicity); and all group keys, dependent keys, and aggregate inputs in a single
data chunk. **Multi-threaded is supported** (unlike the join): each build thread owns an executor and
they merge at the finalize barrier. Distinct aggregates and multi-chunk inputs fall back to the proven
in-memory path.

**Memory bound.** This bounds the **input** side (raw rows spill during append) — the win for a
`GROUP BY` over an exploded join. The output side (`finalizeToTables` holds all group tables at once)
is `O(#groups)`, the same as the in-memory path; streaming the output partition-by-partition is a
follow-on. The merge phase reloads the per-thread spilled partitions to combine them, so it is not
itself budget-bounded — another reason output-side streaming is future work.

**A note on the build.** Adding the two `spill_aggregate*` fields to `ClientConfig` exposed that the
build's header-dependency tracking under-reports `client_config.h`, so an incremental build left a
mix of old/new `ClientConfig` layouts (a silent heap corruption that read the new fields as zero, then
crashed once they were read correctly). A full recompile of `src/` **and** the statically-linked
extensions fixed it; after any `client_config.h` change, force-rebuild both.

**Verification = differential.** `buffer_manager_test`'s `SpillAggregateDifferential` runs a range of
`GROUP BY` queries (including `collect` (LIST) and `min` (STRING) stateful aggregates) with
`spill_aggregate` off vs on and asserts identical result rows; `SpillAggregateSpillDifferential` does
the same over 5000 generated rows with a 4 KiB budget that forces raw rows to spill and reload;
`SpillAggregateMultiThreadDifferential` runs `threads=4` spilling against the single-threaded in-memory
reference (exercising per-thread executors + merge). All assert the spilling path actually activated (a
process-wide counter) so the check is never vacuous.

### Defaults

`spill_aggregate` and `spill_hash_join` **both default to `true`**; `spill_order_by` **defaults to
`false`** (opt-in v1, section 5 of "Remaining work"). `SpillDefaults` asserts an eligible `GROUP BY` and
an eligible INNER equi-join each activate grace with no `CALL` setting. Both are on
because their audits pass and both silently fall back to the proven in-memory path for any shape they
do not conservatively support: aggregation excludes nested group/dependent keys (section 5) and reuses
the in-memory scan; the join takes the Grace path only for INNER/LEFT (with payloads), MARK, or COUNT
(single- or multi-chunk), scalar/string, no-nested/NODE/REL shapes. The earlier NULL-keyed self-join divergence was a bug in the *in-memory*
join (`discardNull`, now fixed at the root — section 5), not in Grace. With the derived budget (whole
buffer pool) an on operator partitions in memory and only spills to disk near the ceiling, trading a
partition/materialize overhead for not-OOMing. With both defaults on the whole suite exercises the
grace paths with no regression (`buffer_manager_test`, `api_test` 97/97, e2e
`agg`/`match`/`generic_hash_join`/`subquery`/`projection`/`filter`/`order_by`/`optional_match`);
because the e2e runner sorts non-`CHECK_ORDER` results, grace's partition-order output does not perturb
those comparisons.

## Remaining work (the large, careful pieces)

The reusable core (`PartitionedFactorizedTable`), a working out-of-core join (`GraceHashJoinExecutor`,
inner+left, flat payloads, materialized + streaming), a **gated live join operator** (section 5,
INNER/LEFT + single-chunk output), an out-of-core **aggregation executor** (section 6), and a **gated
live aggregation operator** (section 7) now exist and are proven correct under spilling. What remains to
broaden coverage and reach the hard `RETURN *` case:

1. **Broaden operator eligibility.** **LEFT** null-padding (`GraceHashJoinLeftDifferential`),
   **factorized / multi-chunk output** — including the *unflat-key build* the planner actually produces —
   (item 2 below; `GraceHashJoinMultiChunkDifferential`), **MARK/COUNT** subquery joins (section 5), and
   **multi-threaded build** (parallel build + serial probe; `GraceHashJoinMultiThreadDifferential`) are
   now enabled and verified end-to-end. What remains is (a) a **parallel out-of-core probe** — the probe
   is forced single-threaded today because there is no mid-pipeline barrier, so a sink-drains-+-partitions
   / parallel-scan-emits restructure (like the aggregate) is needed to lift the multi-thread activation
   policy's memory-bound requirement; and (b) **vectorizing** the multi-chunk emission (today correct but
   row-at-a-time — see item 2).

2. **Factorized (unflat) payloads — the `RETURN *` case.** `PlanMapper::createHashBuildInfo` stores a
   payload from a different chunk than the keys as an `overflow_value_t` **factorized** column, which
   the plain serialize path flattens into the cross-product on spill.

   **Storage layer — done.** `FactorizedTable::serializePreservingFactorization` /
   `deserializePreservingFactorization` iterate the *raw* tuples and write each unflat column as
   `[numElements, elem-Values…]`; `Value`s serialize position-independently, so no pointer swizzling is
   needed and the round-trip reproduces the exact factorized structure. `PartitionedFactorizedTable`
   gains a schema-taking constructor and routes spill/reload through the preserving path when its schema
   has any unflat column (`FactorizedTablePreserveFactorizationRoundTrip`,
   `PartitionedFactorizedTablePreserveFactorizationSpillReload`).

   **Executor append primitive — done.** `PartitionedFactorizedTable::appendFactorizedGroup` routes a
   whole factorized group (one *flat key* broadcast + one *unflat list payload*) to the single partition
   `hash(key)` selects and flattens it into that partition's flat storage (reusing
   `FactorizedTable::append`'s flatten-on-append); `GraceHashJoinExecutor::appendBuildFactorized` wraps
   it. So the build storage stays flat and the unflat OUTPUT is reconstructed at probe time (one probe
   row × its N matched build rows) via the streaming `getNextChunk` — no factorization-preserving spill
   is even needed on the build side. Verified materialized + streamed, no-spill and forced-spill
   (`GraceHashJoinExecutorFactorizedBuild{InMemory,Spill,StreamSpill}`).

   **A shelved detour worth recording.** A first live-operator wiring targeted a *flat-key +
   unflat-build-payload* shape (build a flat key, carry an unflat build payload, stream probe-flat /
   build-unflat via `getNextChunk`). It **never activated on any real query**: `EXPLAIN` across ~12
   diverse shapes showed the planner never emits that shape — it either builds a **flat node scan** and
   pushes the extension to the probe (tiny build, spilling buys nothing), roots the build so the join key
   lands on the **unflat (extended)** node (an *unflat-key* build), or inserts a `FLATTEN` before the
   build. That wiring was reverted rather than shipped as default-on dead code.

   **Operator integration — done (the unflat-key multi-chunk join, the shape the planner produces).**
   `MATCH (a)-[:knows]->(b) MATCH (a)-[:studyAt]->(o) RETURN a.ID, b.ID, o.ID` builds `studyAt` rooted at
   `organisation` (flat root `o`, **unflat key** `a`) and probes with `knows` (flat key `a`, unflat `b`);
   the output spans several chunks (`a`/`o` flat, `b` unflat). This now runs out-of-core end-to-end:
   - **Append auto-detects factorization at runtime.** `appendToPartitions` branches on the key's vector
     state: an **unflat key** scatters each element to `hash(key)`'s partition (flat payloads broadcast,
     unflat payloads sharing the key's chunk travel with it — the existing batch scatter); a **flat key**
     routes the whole group to one partition and flattens any unflat payload
     (`appendFactorizedGroup`). Either way the partition storage ends up flat, so `HashJoinBuild` needs no
     change and the internal hash-table build/probe stay flat.
   - **Multi-chunk output emission.** `computeJoin` gained an `onlyPartition` parameter (materialize just
     one partition's output, freeing that partition pair — peak memory ≈ one partition's result), and
     `initFlatStream` / `getNextFlatTuple` stream the join **one flat tuple at a time** into arbitrary
     output vectors (each column's state set to a single value, so a downstream cross-product over the
     chunks yields exactly one row). This is correct for *any* output chunk structure, which the
     single-chunk materialize+scan and the probe-flat/build-unflat `getNextChunk` cannot both be. It is
     row-at-a-time (correctness-first, memory-bounded, not vectorized); `HashJoinProbe` uses it when
     `multiChunkOutput`, else keeps the vectorized single-chunk scan.
   - **Eligibility (`computeGraceHashJoinInfo`, relaxed).** INNER/LEFT with non-empty payloads (or, added
     later, single-chunk MARK — see section 5), no nested/NODE/REL column, and **each side an appendable
     factorization** — at most one unflat data chunk
     with all keys in a single chunk (`isAppendableFactorization`); more than one unflat chunk per side is
     a cross-product the single-scatter append cannot represent. `multiChunkOutput = !sameChunk(output)`
     selects the emission path.
   - **Verified.** `GraceHashJoinMultiChunkDifferential` (tinysnb, INNER + `OPTIONAL MATCH`, spill off vs
     on identical + the multi-chunk path asserted to activate); `GraceHashJoinExecutorUnflatKeyBuild{InMemory,Spill}`
     and `…FlatStreamSpill` (library, unflat-key build + flat-stream emission under a 4 KiB forced spill,
     against a brute-force reference). No regression: `buffer_manager_test` 49/49, `api_test` 97/97, e2e
     `match`/`subquery`/`projection`/`filter`/`order_by`/`agg`/`generic_hash_join`/`optional_match`.

   **Still falls back to the in-memory path** (conservative): a side with **>1 unflat chunk** (two unflat
   dimensions crossed — e.g. a probe combining two `MANY` extensions), a **composite key spanning chunks**,
   and nested/NODE/REL columns. **Multi-threaded** execution is supported (parallel build + serial probe;
   see section 5 "Multi-threaded build"), though under multi-threading it activates only when a memory
   bound is set. **Vectorizing** the row-at-a-time multi-chunk emission (keeping the unflat dimension
   factorized instead of flattening one tuple per call) and a **parallel out-of-core probe** are the main
   follow-ons.

3. **Remaining join types & keys** — **MARK (EXISTS / semi / anti-join) and COUNT (size / `COUNT{}`
   subquery) are done for both single- and multi-chunk output** (`computeMarkJoin` / `computeCountJoin` +
   the `FlatStreamMode` flat stream, section 5 "MARK joins" / "COUNT joins"); still open: multi-column /
   unflat probe keys in the executor (inner + left + single/composite flat key are done).

5. **`ORDER BY` external merge sort — implemented (v1, opt-in).** `ExternalMergeSort`
   (`src/processor/operator/order_by/external_merge_sort.{h,cpp}`) gives a plain `ORDER BY` (no
   `LIMIT`) an out-of-core path so it no longer needs the whole result resident. The in-memory sort
   holds every row twice with no bound — `SortSharedState::payloadTables` (a `FactorizedTable` per
   thread) and the `sortedKeyBlocks` queue of encoded key tuples; `ORDER BY … LIMIT` is already bounded
   by `TopKBuffer::reduce`, so only the plain path needed this.

   Gated by `spill_order_by` (+`spill_order_by_budget`), plumbed exactly like `spill_aggregate` (section
   7), **off by default**. `OrderBy::initLocalStateInternal` activates the path at runtime when
   `spill_order_by` is on and the sort is eligible; otherwise the in-memory sort runs byte-for-byte
   unchanged. **Eligibility:** fixed-width or `STRING` keys and non-nested payloads. Keys and payloads
   may span **multiple (flat) factorization groups** — different data chunks. The one shape not yet
   supported is an *unflat overflow* payload column (a payload whose group stays unflat across multiple
   groups, stored factorized as one list entry per key tuple); `isExternalSortEligible` detects it via
   `payloadTableSchema` and falls back. **Multi-threaded** (`threads > 1`) is supported — see run
   generation below.
   - **Run generation** (`append`, per thread): each `OrderBy` thread owns its own `ExternalMergeSort`
     generator and spill file, so run generation is embarrassingly parallel and lock-free on the hot
     path (only registration into the shared state takes the mutex, mirroring `getLocalPayloadTable`).
     It reuses `OrderByKeyEncoder` to turn each tuple's keys into a memcmp-comparable byte prefix and
     pairs it with the payload captured as self-describing `Value::serialize` bytes; it buffers these
     `(key, payload)` records until the budget is hit, then sorts the run in memory and spills it. The
     budget is split evenly across `threads` so the aggregate resident footprint stays bounded.
     Decoupling the payload as per-row `Value` bytes (rather than a spilled `FactorizedTable`) means runs
     carry no back-pointers and need no re-basing — which is also what lets runs from *different* threads
     (different spill files) merge together with no fix-up. Each payload is captured at *its own* vector
     position (a flat vector broadcasts one value; an unflat vector is indexed per tuple), so keys and
     payloads may live in different flat data chunks.
   - **Comparison** (`compareRecords`): plain `memcmp` of the encoded key for fixed-width keys;
     otherwise column-by-column, resolving a `STRING` column's 12-byte-prefix tie against the full
     string captured in the payload, then **continuing to the remaining key columns** — the strict total
     order over all keys. This matches the in-memory `RadixSort`, which sorts by the full encoded key;
     the `KeyBlockMerger` habit of ignoring columns after the last string is only valid layered on top of
     that full-key pre-sort, so it is deliberately *not* replicated here (doing so mis-ordered a string
     key followed by further key columns). Used for both the in-run sort and the merge heap.
   - **Merge / scan** (`scanNext`, driven by `OrderByScan`): after the pipeline barrier, the single scan
     thread calls `SortSharedState::prepareExternalMerge`, which gathers every generator's runs into
     one coordinator as file-scoped `RunSource`s and streams a **k-way merge across all threads' runs** —
     one `BufferedFileReader` per run (each addressing its originating spill file), a min-heap on
     `compareRecords` — emitting sorted tuples straight into the output vectors (`Value::deserialize` →
     `copyFromValue`). Reads are positioned (`FileInfo::readFromFile` takes an explicit offset), so many
     cursors over the same or different files never interfere. The output is emitted in one of two shapes,
     chosen from the output vectors' states: a full vector at a time when every output column shares one
     unflat state (the common single-group case), otherwise one tuple at a time (flat outputs, possibly
     across multiple flat groups), mirroring `PayloadScanner`'s single-tuple path. Because each run head's
     full payload is already resident, the string tie-break needs no extra disk reads. Memory is bounded
     to ~the budget during generation and ~one buffered page per open run during the merge; disk reads are
     sequential per run. `OrderByScan` is `isParallel() == false`, so the merge always runs single-threaded.

   `OrderByMerge` naturally no-ops (the executor merges internally, so `sortedKeyBlocks` stays empty).
   Verified by `buffer_manager_test`: `ExternalMergeSortDifferential` (library — 3000 rows, 4 KiB
   budget forcing 6 spilled runs, multiset + key-order checks over multi-column ASC/DESC/NULL keys),
   `SpillOrderByDifferential` (operator — 5000 rows, total-order queries including long `STRING` keys
   that share a 12-char prefix and a `STRING` key *followed by* further key columns, spill on vs off must
   match, activation asserted), `SpillOrderByMultiThreadedDifferential` (operator — 8000 rows, `threads=4`
   external vs `threads=1` in-memory must match, tiny per-generator budget forcing many cross-file runs),
   and `SpillOrderByMultiGroupDifferential` (operator — a comma cross-product whose keys/payloads span two
   factorization groups), plus e2e `order_by/spill_order_by.test` (which includes `threads=4` and
   multi-group cases).

   *Out of scope — nested keys.* Ordering by a nested value (`LIST`/`ARRAY`/`STRUCT`/`MAP`/`UNION`) is
   rejected engine-wide at bind time (`isOrderByKeyTypeSupported` in `bind_projection_clause.cpp`), so no
   sort path — in-memory or external — ever sees a nested key. The whole encode/radix machinery is
   byte-comparable-only (`OrderByKeyEncoder::getEncodingFunction` is `KU_UNREACHABLE` for nested physical
   types), and lifting that is an engine-level feature (binder + encoder + `KeyBlockMerger`), not an
   external-sort gap. `isExternalSortEligible`'s nested-*key* check is therefore defensive only. Nested
   *payloads* (a scalar key carrying a nested return column, e.g. `RETURN p.scores ORDER BY p.id`) are a
   real fallback the external path could add later — `Value::serialize` already handles nested values.

   *Remaining sub-pieces:* **unflat overflow payload columns** (the one factorized shape still excluded —
   an unflat list carried alongside a flat key must be captured and re-expanded factorized), which is
   also the general form of the — smaller — **nested-payload** case (above). Multi-threaded run
   generation and multiple *flat* factorization groups are now implemented (above).

6. **Partitioned aggregation — broadening.** The library executor and the gated live operator now
   exist (sections 6–7). What remains: distinct aggregates, multi-state / multi-chunk inputs,
   multi-threaded build, and **streaming the output** partition-by-partition (to bound the output side,
   not just the input side).

All reuse the `SpillableComponent` registry (for the raw-buffer, buffer-manager-triggered path) or
`PartitionedFactorizedTable` / the `FactorizedTable` serialization primitive (for the operator-
triggered path on variable-length data).
