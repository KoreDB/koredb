#pragma once

// Helpers
#if defined _WIN32 || defined __CYGWIN__
#define KOREDB_HELPER_DLL_IMPORT __declspec(dllimport)
#define KOREDB_HELPER_DLL_EXPORT __declspec(dllexport)
#define KOREDB_HELPER_DLL_LOCAL
#define KOREDB_HELPER_DEPRECATED __declspec(deprecated)
#else
#define KOREDB_HELPER_DLL_IMPORT __attribute__((visibility("default")))
#define KOREDB_HELPER_DLL_EXPORT __attribute__((visibility("default")))
#define KOREDB_HELPER_DLL_LOCAL __attribute__((visibility("hidden")))
#define KOREDB_HELPER_DEPRECATED __attribute__((__deprecated__))
#endif

#ifdef KOREDB_STATIC_DEFINE
#define KOREDB_API
#else
#ifndef KOREDB_API
#ifdef KOREDB_EXPORTS
/* We are building this library */
#define KOREDB_API KOREDB_HELPER_DLL_EXPORT
#else
/* We are using this library */
#define KOREDB_API KOREDB_HELPER_DLL_IMPORT
#endif
#endif
#endif

#ifndef KOREDB_DEPRECATED
#define KOREDB_DEPRECATED KOREDB_HELPER_DEPRECATED
#endif

#ifndef KOREDB_DEPRECATED_EXPORT
#define KOREDB_DEPRECATED_EXPORT KOREDB_API KOREDB_DEPRECATED
#endif
