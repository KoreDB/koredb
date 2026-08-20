package com.koredb;

import java.util.Map;
import java.io.File;
import java.io.IOException;
import java.net.URL;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;

/**
 * Native is a wrapper class for the native library.
 * It is used to load the native library and call the native functions.
 * This class is not intended to be used by end users.
 */
public class Native {
    static {
        try {
            String os_name = "";
            String os_arch;
            String os_name_detect = System.getProperty("os.name").toLowerCase().trim();
            String os_arch_detect = System.getProperty("os.arch").toLowerCase().trim();
            boolean isAndroid = System.getProperty("java.runtime.name", "").toLowerCase().contains("android")
                || System.getProperty("java.vendor", "").toLowerCase().contains("android")
                || System.getProperty("java.vm.name", "").toLowerCase().contains("dalvik");
            switch (os_arch_detect) {
                case "x86_64":
                case "amd64":
                    os_arch = "amd64";
                    break;
                case "aarch64":
                case "arm64":
                    os_arch = "arm64";
                    break;
                case "i386":
                    os_arch = "i386";
                    break;
                default:
                    throw new IllegalStateException("Unsupported system architecture");
            }
            if (isAndroid){
                os_name = "android";
            }
            else if (os_name_detect.startsWith("windows")) {
                os_name = "windows";
            } else if (os_name_detect.startsWith("mac")) {
                os_name = "osx";
            } else if (os_name_detect.startsWith("linux")) {
                os_name = "linux";
            }
            String lib_res_name = "/libkoredb_java_native.so" + "_" + os_name + "_" + os_arch;

            Path lib_file = Files.createTempFile("libkoredb_java_native", ".so");
            URL lib_res = Native.class.getResource(lib_res_name);
            if (lib_res == null) {
                throw new IOException(lib_res_name + " not found");
            }
            Files.copy(lib_res.openStream(), lib_file, StandardCopyOption.REPLACE_EXISTING);
            new File(lib_file.toString()).deleteOnExit();
            String lib_path = lib_file.toAbsolutePath().toString();
            System.load(lib_path);
            if (os_name.equals("linux")) {
                koredbNativeReloadLibrary(lib_path);
            }
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    // Hack: Reload the native library again in JNI bindings to work around the
    // extension loading issue on Linux as System.load() does not set
    // `RTLD_GLOBAL` flag and there is no way to set it in Java.
    protected static native void koredbNativeReloadLibrary(String libPath);

    // Database
    protected static native long koredbDatabaseInit(String databasePath, long bufferPoolSize,
            boolean enableCompression, boolean readOnly, long maxDbSize, boolean autoCheckpoint,
            long checkpointThreshold);

    protected static native void koredbDatabaseDestroy(Database db);

    protected static native void koredbDatabaseSetLoggingLevel(String loggingLevel);

    // Connection
    protected static native long koredbConnectionInit(Database database);

    protected static native void koredbConnectionDestroy(Connection connection);

    protected static native void koredbConnectionSetMaxNumThreadForExec(
            Connection connection, long numThreads);

    protected static native long koredbConnectionGetMaxNumThreadForExec(Connection connection);

    protected static native QueryResult koredbConnectionQuery(Connection connection, String query);

    protected static native PreparedStatement koredbConnectionPrepare(
            Connection connection, String query);

    protected static native QueryResult koredbConnectionExecute(
            Connection connection, PreparedStatement preparedStatement, Map<String, Value> param);

    protected static native void koredbConnectionInterrupt(Connection connection);

    protected static native void koredbConnectionSetQueryTimeout(
            Connection connection, long timeoutInMs);

    // PreparedStatement
    protected static native void koredbPreparedStatementDestroy(PreparedStatement preparedStatement);

    protected static native boolean koredbPreparedStatementIsSuccess(PreparedStatement preparedStatement);

    protected static native String koredbPreparedStatementGetErrorMessage(
            PreparedStatement preparedStatement);

    // QueryResult
    protected static native void koredbQueryResultDestroy(QueryResult queryResult);

    protected static native boolean koredbQueryResultIsSuccess(QueryResult queryResult);

    protected static native boolean koredbQueryResultIsTruncated(QueryResult queryResult);

    protected static native String koredbQueryResultGetTruncationReason(QueryResult queryResult);

    protected static native String koredbQueryResultGetErrorMessage(QueryResult queryResult);

    protected static native long koredbQueryResultGetNumColumns(QueryResult queryResult);

    protected static native String koredbQueryResultGetColumnName(QueryResult queryResult, long index);

    protected static native DataType koredbQueryResultGetColumnDataType(
            QueryResult queryResult, long index);

    protected static native long koredbQueryResultGetNumTuples(QueryResult queryResult);

    protected static native QuerySummary koredbQueryResultGetQuerySummary(QueryResult queryResult);

    protected static native boolean koredbQueryResultHasNext(QueryResult queryResult);

    protected static native FlatTuple koredbQueryResultGetNext(QueryResult queryResult);

    protected static native boolean koredbQueryResultHasNextQueryResult(QueryResult queryResult);

    protected static native QueryResult koredbQueryResultGetNextQueryResult(QueryResult queryResult);

    protected static native String koredbQueryResultToString(QueryResult queryResult);

    protected static native void koredbQueryResultResetIterator(QueryResult queryResult);

    // FlatTuple
    protected static native void koredbFlatTupleDestroy(FlatTuple flatTuple);

    protected static native Value koredbFlatTupleGetValue(FlatTuple flatTuple, long index);

    protected static native String koredbFlatTupleToString(FlatTuple flatTuple);

    // DataType
    protected static native long koredbDataTypeCreate(
            DataTypeID id, DataType childType, long numElementsInArray);

    protected static native DataType koredbDataTypeClone(DataType dataType);

    protected static native void koredbDataTypeDestroy(DataType dataType);

    protected static native boolean koredbDataTypeEquals(DataType dataType1, DataType dataType2);

    protected static native DataTypeID koredbDataTypeGetId(DataType dataType);

    protected static native DataType koredbDataTypeGetChildType(DataType dataType);

    protected static native long koredbDataTypeGetNumElementsInArray(DataType dataType);

    // Value
    protected static native Value koredbValueCreateNull();

    protected static native Value koredbValueCreateNullWithDataType(DataType dataType);

    protected static native boolean koredbValueIsNull(Value value);

    protected static native void koredbValueSetNull(Value value, boolean isNull);

    protected static native Value koredbValueCreateDefault(DataType dataType);

    protected static native <T> long koredbValueCreateValue(T val);

    protected static native Value koredbValueClone(Value value);

    protected static native void koredbValueCopy(Value value, Value other);

    protected static native void koredbValueDestroy(Value value);

    protected static native Value koredbCreateMap(Value[] keys, Value[] values);

    protected static native Value koredbCreateList(Value[] values);

    protected static native Value koredbCreateList(DataType type, long numElements);

    protected static native long koredbValueGetListSize(Value value);

    protected static native Value koredbValueGetListElement(Value value, long index);

    protected static native DataType koredbValueGetDataType(Value value);

    protected static native <T> T koredbValueGetValue(Value value);

    protected static native String koredbValueToString(Value value);

    protected static native InternalID koredbNodeValGetId(Value nodeVal);

    protected static native String koredbNodeValGetLabelName(Value nodeVal);

    protected static native long koredbNodeValGetPropertySize(Value nodeVal);

    protected static native String koredbNodeValGetPropertyNameAt(Value nodeVal, long index);

    protected static native Value koredbNodeValGetPropertyValueAt(Value nodeVal, long index);

    protected static native String koredbNodeValToString(Value nodeVal);

    protected static native InternalID koredbRelValGetId(Value relVal);

    protected static native InternalID koredbRelValGetSrcId(Value relVal);

    protected static native InternalID koredbRelValGetDstId(Value relVal);

    protected static native String koredbRelValGetLabelName(Value relVal);

    protected static native long koredbRelValGetPropertySize(Value relVal);

    protected static native String koredbRelValGetPropertyNameAt(Value relVal, long index);

    protected static native Value koredbRelValGetPropertyValueAt(Value relVal, long index);

    protected static native String koredbRelValToString(Value relVal);

    protected static native Value koredbCreateStruct(String[] fieldNames, Value[] fieldValues);

    protected static native String koredbValueGetStructFieldName(Value structVal, long index);

    protected static native long koredbValueGetStructIndex(Value structVal, String fieldName);

    protected static native String koredbGetVersion();

    protected static native long koredbGetStorageVersion();
}
