import koredb from "./index.js";

// Re-export everything from the CommonJS module
export const Database = koredb.Database;
export const Connection = koredb.Connection;
export const PreparedStatement = koredb.PreparedStatement;
export const QueryResult = koredb.QueryResult;
export const VERSION = koredb.VERSION;
export const STORAGE_VERSION = koredb.STORAGE_VERSION;
export default koredb;
