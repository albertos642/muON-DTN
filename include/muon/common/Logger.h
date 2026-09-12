/**
 * @file Logger.h
 * @brief Zero-Malloc diagnostic logger interface for muON-DTN.
 *
 * Provides thread-safe, decoupled diagnostic logging without hardware dependencies
 * or coupling to Arduino/FreeRTOS in core network layers.
 *
 * @author Alberto Soncini <alberto.soncini3@studio.unibo.it>
 * @author Supervisor: Carlo Caini <carlo.caini@unibo.it>
 * @copyright Copyright (c) 2026 Alma Mater Studiorum, University of Bologna.
 */

#ifndef MUON_COMMON_LOGGER_H
#define MUON_COMMON_LOGGER_H

#include <stdint.h>
#include <stddef.h>
#include "autoconf.h"

namespace muon {
namespace log {

typedef void (*LogStrFn)(const char* str);
typedef void (*LogLnFn)(const char* str);
typedef void (*LogU32Fn)(uint32_t val);
typedef void (*LogI32Fn)(int32_t val);
typedef void (*LogFloatFn)(float val, uint8_t decimals);

template <typename T = void>
struct LoggerT {
    static LogStrFn   s_strFn;
    static LogLnFn    s_lnFn;
    static LogU32Fn   s_u32Fn;
    static LogI32Fn   s_i32Fn;
    static LogFloatFn s_floatFn;

    static void setSinks(LogStrFn strFn, LogLnFn lnFn, LogU32Fn u32Fn, LogI32Fn i32Fn, LogFloatFn floatFn) {
#if defined(CONFIG_MUON_DEBUG)
        s_strFn = strFn;
        s_lnFn = lnFn;
        s_u32Fn = u32Fn;
        s_i32Fn = i32Fn;
        s_floatFn = floatFn;
#else
        (void)strFn; (void)lnFn; (void)u32Fn; (void)i32Fn; (void)floatFn;
#endif
    }

    static inline void print(const char* s) {
#if defined(CONFIG_MUON_DEBUG)
        if (s_strFn && s) s_strFn(s);
#else
        (void)s;
#endif
    }

    static inline void println(const char* s) {
#if defined(CONFIG_MUON_DEBUG)
        if (s_lnFn && s) s_lnFn(s);
#else
        (void)s;
#endif
    }

    static inline void print(uint32_t val) {
#if defined(CONFIG_MUON_DEBUG)
        if (s_u32Fn) s_u32Fn(val);
#else
        (void)val;
#endif
    }

    static inline void print(int32_t val) {
#if defined(CONFIG_MUON_DEBUG)
        if (s_i32Fn) s_i32Fn(val);
#else
        (void)val;
#endif
    }

    static inline void print(float val, uint8_t decimals = 2) {
#if defined(CONFIG_MUON_DEBUG)
        if (s_floatFn) s_floatFn(val, decimals);
#else
        (void)val; (void)decimals;
#endif
    }
};

template <typename T> LogStrFn   LoggerT<T>::s_strFn = nullptr;
template <typename T> LogLnFn    LoggerT<T>::s_lnFn = nullptr;
template <typename T> LogU32Fn   LoggerT<T>::s_u32Fn = nullptr;
template <typename T> LogI32Fn   LoggerT<T>::s_i32Fn = nullptr;
template <typename T> LogFloatFn LoggerT<T>::s_floatFn = nullptr;

using Logger = LoggerT<void>;

} // namespace log
} // namespace muon

#if defined(CONFIG_MUON_DEBUG)
  #define MUON_LOG_STR(s)            muon::log::Logger::print(s)
  #define MUON_LOG_LN(s)             muon::log::Logger::println(s)
  #define MUON_LOG_U32(val)          muon::log::Logger::print(static_cast<uint32_t>(val))
  #define MUON_LOG_I32(val)          muon::log::Logger::print(static_cast<int32_t>(val))
  #define MUON_LOG_FLOAT(val, dec)   muon::log::Logger::print(static_cast<float>(val), dec)
#else
  #define MUON_LOG_STR(s)            do {} while (0)
  #define MUON_LOG_LN(s)             do {} while (0)
  #define MUON_LOG_U32(val)          do {} while (0)
  #define MUON_LOG_I32(val)          do {} while (0)
  #define MUON_LOG_FLOAT(val, dec)   do {} while (0)
#endif

#endif // MUON_COMMON_LOGGER_H
