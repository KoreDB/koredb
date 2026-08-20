#pragma once

#include <mutex>

#include "binder/expression/expression.h"
#include "common/enums/join_type.h"
#include "common/types/types.h"
#include "join_hash_table.h"
#include "processor/operator/physical_operator.h"
#include "processor/operator/sink.h"
#include "processor/result/factorized_table.h"
#include "processor/result/result_set.h"

namespace koredb {
namespace main {
class ClientContext;
} // namespace main
namespace processor {

class GraceHashJoinExecutor;

// Test-only: how many times the out-of-core (Grace) hash-join path has been activated in this
// process. Lets a differential test confirm the spilling path actually ran (rather than silently
// falling back to the in-memory path and proving nothing).
KOREDB_API uint64_t getGraceHashJoinActivationCount();
// Test-only: activations of specifically the multi-chunk (factorized, flat-streaming output) Grace
// path -- a subset of getGraceHashJoinActivationCount.
KOREDB_API uint64_t getGraceHashJoinMultiChunkActivationCount();

// Static (plan-time) metadata that lets the HASH_JOIN operator run the out-of-core (Grace) path when
// the `spill_hash_join` setting is on and the join shape is supported. `eligible` is the conservative
// shape check (INNER/LEFT with non-empty build payloads, or MARK/EXISTS with keys-only build and a
// single-chunk output; each side an appendable factorization -- at most one unflat group with all keys
// in a single group -- and no nested/NODE/REL column); when false the operator always uses the
// in-memory path. The type lists mirror the executor's schema ([keys...], build payloads, probe
// non-key columns) and `probeNonKeyPos` locates the probe-side non-key output columns (everything the
// probe contributes that is not a join key) in the probe/output result set. For a MARK join
// buildPayloadTypes is empty and the probe writes the bool result into its own mark output vector.
//
// `multiChunkOutput` selects how the probe operator emits the join: false = all output columns live in
// one data chunk, so the join is materialized and scanned back into that chunk (vectorized); true = the
// output spans several chunks (the factorized RETURN *-style case the planner actually produces, e.g.
// an unflat-key build), so the operator streams the join one flat tuple at a time
// (GraceHashJoinExecutor::getNextFlatTuple), which is correct for any chunk structure.
struct GraceHashJoinInfo {
    bool eligible = false;
    bool multiChunkOutput = false;
    common::JoinType joinType = common::JoinType::INNER;
    std::vector<common::LogicalType> keyTypes;
    std::vector<common::LogicalType> buildPayloadTypes;
    std::vector<common::LogicalType> probeNonKeyTypes;
    std::vector<DataPos> probeNonKeyPos;

    GraceHashJoinInfo() = default;
    GraceHashJoinInfo(GraceHashJoinInfo&&) = default;
    GraceHashJoinInfo& operator=(GraceHashJoinInfo&&) = default;
    GraceHashJoinInfo(const GraceHashJoinInfo&) = delete;
    GraceHashJoinInfo& operator=(const GraceHashJoinInfo&) = delete;
};

struct HashJoinBuildPrintInfo final : OPPrintInfo {
    binder::expression_vector keys;
    binder::expression_vector payloads;

    HashJoinBuildPrintInfo(binder::expression_vector keys, binder::expression_vector payloads)
        : keys{std::move(keys)}, payloads(std::move(payloads)) {}

    std::string toString() const override;

    std::unique_ptr<OPPrintInfo> copy() const override {
        return std::unique_ptr<HashJoinBuildPrintInfo>(new HashJoinBuildPrintInfo(*this));
    }

private:
    HashJoinBuildPrintInfo(const HashJoinBuildPrintInfo& other)
        : OPPrintInfo{other}, keys{other.keys}, payloads{other.payloads} {}
};

class HashJoinBuild;

// This is a shared state between HashJoinBuild and HashJoinProbe operators.
// Each clone of these two operators will share the same state.
// Inside the state, we keep the materialized tuples in factorizedTable, which are merged by each
// HashJoinBuild thread when they finished materializing thread-local tuples. Also, the state holds
// a global htDirectory, which will be updated by the last thread in the hash join build side
// task/pipeline, and probed by the HashJoinProbe operators.
class HashJoinSharedState {
public:
    // Constructor and destructor are defined out of line so std::unique_ptr<GraceHashJoinExecutor>
    // only needs a forward declaration here (the inline constructor would otherwise instantiate the
    // member's destructor for exception cleanup, requiring the complete type).
    explicit HashJoinSharedState(std::unique_ptr<JoinHashTable> hashTable);
    ~HashJoinSharedState();

    void mergeLocalHashTable(JoinHashTable& localHashTable);

    JoinHashTable* getHashTable() { return hashTable.get(); }

    // --- Grace (out-of-core) join state ---
    void setGraceInfo(GraceHashJoinInfo info) { graceInfo = std::move(info); }
    const GraceHashJoinInfo& getGraceInfo() const { return graceInfo; }
    // The mapper stores the ClientContext so willActivateGrace() can read the *live* spill_hash_join
    // setting from HashJoinProbe::isParallel() (evaluated before execution, when a per-thread runtime
    // flag would not yet be set) -- so build and probe agree on whether the out-of-core path runs.
    void setClientContext(main::ClientContext* context) { clientContext = context; }
    // True iff the out-of-core (Grace) path will run: spill_hash_join is on and the shape is eligible.
    // Decided purely from the (live) config + plan-time eligibility, so it is stable across the build and
    // probe pipelines of one execution and needs no runtime barrier. When true the build runs
    // multi-threaded (per-thread executors merged at finalize) and the probe is forced single-threaded
    // (HashJoinProbe::isParallel() == false), so no cross-thread coordination is needed on the probe.
    bool willActivateGrace() const;
    bool isGraceActive() const { return willActivateGrace(); }
    GraceHashJoinExecutor* getGraceExecutor() const { return graceExecutor.get(); }
    // Register a build thread's local executor (its accumulated build partitions), to be merged in
    // finalize. Thread-safe.
    void registerLocalGraceExecutor(std::unique_ptr<GraceHashJoinExecutor> executor);
    // Merge all registered per-thread build executors into a single one (getGraceExecutor()). Called
    // once at the build finalize barrier, before the (single-threaded) probe runs. Out of line: needs
    // the complete GraceHashJoinExecutor type.
    void mergeGraceExecutors();

protected:
    std::mutex mtx;
    std::unique_ptr<JoinHashTable> hashTable;
    // Populated by the mapper; consumed at runtime when spilling is enabled and the shape is eligible.
    GraceHashJoinInfo graceInfo;
    main::ClientContext* clientContext = nullptr;
    // Per-thread build executors registered during the (parallel) build; merged into graceExecutor at
    // the finalize barrier.
    std::vector<std::unique_ptr<GraceHashJoinExecutor>> localGraceExecutors;
    std::unique_ptr<GraceHashJoinExecutor> graceExecutor;
};

struct HashJoinBuildInfo {
    std::vector<DataPos> keysPos;
    std::vector<common::FStateType> fStateTypes;
    std::vector<DataPos> payloadsPos;
    FactorizedTableSchema tableSchema;

    HashJoinBuildInfo(std::vector<DataPos> keysPos, std::vector<common::FStateType> fStateTypes,
        std::vector<DataPos> payloadsPos, FactorizedTableSchema tableSchema)
        : keysPos{std::move(keysPos)}, fStateTypes{std::move(fStateTypes)},
          payloadsPos{std::move(payloadsPos)}, tableSchema{std::move(tableSchema)} {}
    EXPLICIT_COPY_DEFAULT_MOVE(HashJoinBuildInfo);

    common::idx_t getNumKeys() const { return keysPos.size(); }

private:
    HashJoinBuildInfo(const HashJoinBuildInfo& other)
        : keysPos{other.keysPos}, fStateTypes{other.fStateTypes}, payloadsPos{other.payloadsPos},
          tableSchema{other.tableSchema.copy()} {}
};

class HashJoinBuild : public Sink {
public:
    // Constructor and destructor are out of line: localGraceExecutor is a unique_ptr to the
    // forward-declared executor, so the inline constructor's exception-cleanup path (and the
    // destructor) would otherwise need the complete type in every including TU.
    HashJoinBuild(PhysicalOperatorType operatorType,
        std::shared_ptr<HashJoinSharedState> sharedState, HashJoinBuildInfo info,
        std::unique_ptr<PhysicalOperator> child, uint32_t id,
        std::unique_ptr<OPPrintInfo> printInfo);
    ~HashJoinBuild() override;

    std::shared_ptr<HashJoinSharedState> getSharedState() const { return sharedState; }

    void initLocalStateInternal(ResultSet* resultSet, ExecutionContext* context) override;

    void executeInternal(ExecutionContext* context) override;

    void finalizeInternal(ExecutionContext* context) override;

    std::unique_ptr<PhysicalOperator> copy() override {
        return make_unique<HashJoinBuild>(operatorType, sharedState, info.copy(),
            children[0]->copy(), id, printInfo->copy());
    }

protected:
    virtual uint64_t appendVectors() {
        return hashTable->appendVectors(keyVectors, payloadVectors, keyState);
    }

private:
    void setKeyState(common::DataChunkState* state);

protected:
    std::shared_ptr<HashJoinSharedState> sharedState;
    HashJoinBuildInfo info;

    std::vector<common::ValueVector*> keyVectors;
    // State of unFlat key(s). If all keys are flat, it points to any flat key state.
    common::DataChunkState* keyState = nullptr;
    std::vector<common::ValueVector*> payloadVectors;

    std::unique_ptr<JoinHashTable> hashTable; // local state (in-memory path)
    // This build thread's out-of-core executor (Grace path); registered into the shared state and
    // merged at finalize. Null on the in-memory path.
    std::unique_ptr<GraceHashJoinExecutor> localGraceExecutor;
};

} // namespace processor
} // namespace koredb
