#pragma once

#include "connector/duckdb_connector.h"

namespace koredb {
namespace unity_catalog_extension {

class UnityCatalogConnector : public duckdb_extension::DuckDBConnector {
public:
    void connect(const std::string& dbPath, const std::string& catalogName,
        const std::string& schemaName, main::ClientContext* context) override;
};

} // namespace unity_catalog_extension
} // namespace koredb
