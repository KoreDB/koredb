#include "storage/storage_version_info.h"

namespace koredb {
namespace storage {

storage_version_t StorageVersionInfo::getStorageVersion() {
    auto storageVersionInfo = getStorageVersionInfo();
    if (!storageVersionInfo.contains(KOREDB_CMAKE_VERSION)) {
        // If the current KOREDB_CMAKE_VERSION is not in the map,
        // then we must run the newest version of koredb
        // LCOV_EXCL_START
        storage_version_t maxVersion = 0;
        for (auto& [_, versionNumber] : storageVersionInfo) {
            maxVersion = std::max(maxVersion, versionNumber);
        }
        return maxVersion;
        // LCOV_EXCL_STOP
    }
    return storageVersionInfo.at(KOREDB_CMAKE_VERSION);
}

} // namespace storage
} // namespace koredb
