package com.koredb;

/**
 * Version is a class to get the version of the KoreDB.
 */
public class Version {

    /**
     * Get the version of the KoreDB.
     *
     * @return The version of the KoreDB.
     */
    public static String getVersion() {
        return Native.koredbGetVersion();
    }

    /**
     * Get the storage version of the KoreDB.
     *
     * @return The storage version of the KoreDB.
     */
    public static long getStorageVersion() {
        return Native.koredbGetStorageVersion();
    }
}
