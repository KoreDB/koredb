#include "main/version.h"

#include "c_api/helpers.h"
#include "c_api/koredb.h"

char* koredb_get_version() {
    return convertToOwnedCString(koredb::main::Version::getVersion());
}

uint64_t koredb_get_storage_version() {
    return koredb::main::Version::getStorageVersion();
}
