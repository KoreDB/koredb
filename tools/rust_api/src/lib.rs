//! Bindings to KoreDB: an in-process property graph database management system built for query speed and scalability.
//!
//! ## Example Usage
//! ```
//! use koredb::{Database, SystemConfig, Connection};
//! # use anyhow::Error;
//!
//! # fn main() -> Result<(), Error> {
//! # let temp_dir = tempfile::tempdir()?;
//! # let path = temp_dir.path().join("testdb");
//! let db = Database::new(path, SystemConfig::default())?;
//! let conn = Connection::new(&db)?;
//! conn.query("CREATE NODE TABLE Person(name STRING, age INT64, PRIMARY KEY(name));")?;
//! conn.query("CREATE (:Person {name: 'Alice', age: 25});")?;
//! conn.query("CREATE (:Person {name: 'Bob', age: 30});")?;
//!
//! let mut result = conn.query("MATCH (a:Person) RETURN a.name AS NAME, a.age AS AGE;")?;
//! println!("{}", result);
//! # temp_dir.close()?;
//! # Ok(())
//! # }
//! ```
//! ## Building
//!
//! By default, the KoreDB C++ library will be compiled from source and statically linked.
//!
//! If you want to instead link against a pre-built version of the library, the following environment
//! variables can be used to configure the build process:
//!
//! - `KOREDB_SHARED`: If set, link dynamically instead of statically
//! - `KOREDB_INCLUDE_DIR`: Directory of KoreDB's headers
//! - `KOREDB_LIBRARY_DIR`: Directory containing KoreDB's pre-built libraries.
//!
//! Example:
//! ```bash
//! koredb_prebuilt_dir=/tmp/koredb # pre-built KoreDB from https://docs.kuzudb.com/installation/#cc
//! koredb_prebuilt_dir=/path_to_koredb_source/build/release/src # KoreDB built from source
//! export KOREDB_LIBRARY_DIR="koredb_prebuilt_dir"
//! export KOREDB_INCLUDE_DIR="koredb_prebuilt_dir"
//! export KOREDB_SHARED=1
//! ```
//! On macOS:
//! ```bash
//! brew install koredb
//! export KOREDB_LIBRARY_DIR=/opt/homebrew/lib
//! export KOREDB_INCLUDE_DIR=/opt/homebrew/include
//! export KOREDB_SHARED=1
//! ```
//!
//! ## Using Extensions
//! By default, binaries created using this library will not work with KoreDB's
//! [extensions](https://docs.kuzudb.com/extensions/) (except on Windows/MSVC, where the linker works differently).
//!
//! If you want to use extensions in binaries (binary crates or tests) using this
//! library, you will need to add the following (or a similar command; see
//! [build-scripts](https://doc.rust-lang.org/cargo/reference/build-scripts.html#rustc-link-arg))
//! to your build.rs (or create one) so that the binary
//! produced acts like a library that the extension can link with. Not doing this will produce
//! undefined symbol errors when the extension is loaded:
//!
//! ```ignore
//! println!("cargo:rustc-link-arg=-rdynamic");
//! ```

pub use connection::{Connection, PreparedStatement};
pub use database::{Database, SystemConfig};
pub use error::Error;
pub use logical_type::LogicalType;
#[cfg(feature = "arrow")]
pub use query_result::ArrowIterator;
pub use query_result::{CSVOptions, QueryResult};
pub use value::{InternalID, NodeVal, RelVal, Value};

mod connection;
mod database;
mod error;
mod ffi;
mod logical_type;
mod query_result;
mod value;

/// The version of the KoreDB crate as reported by Cargo's `CARGO_PKG_VERSION` environment variable
pub const VERSION: &str = env!("CARGO_PKG_VERSION");
/// Returns the storage version of the KoreDB library
pub fn get_storage_version() -> u64 {
    crate::ffi::ffi::get_storage_version()
}
