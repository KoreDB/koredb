#include "installer/duckdb_installer.h"

extern "C" {
// Because we link against the static library on windows, we implicitly inherit
// KOREDB_STATIC_DEFINE, which cancels out any exporting, so we can't use KOREDB_API.
#if defined(_WIN32)
#define INIT_EXPORT __declspec(dllexport)
#else
#define INIT_EXPORT __attribute__((visibility("default")))
#endif
INIT_EXPORT void install(const std::string& repo, koredb::main::ClientContext& context) {
    koredb::extension::InstallExtensionInfo info{"azure", repo, false /* forceInstall */};
    koredb::duckdb_extension::DuckDBInstaller installer{info, context};
    installer.install();
}
}
