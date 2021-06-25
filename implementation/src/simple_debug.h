#pragma once
#include <cstdio>
#include <iostream>
#include <sstream>
#include <iomanip>

#include "simple_debug_level.h"

#define DEBUG_LEVEL DEBUG_LEVEL_ERROR

#define TRIE_DEBUG DEBUG_LEVEL_ERROR

#define TATONNEMENT_DEBUG DEBUG_LEVEL_INFO

#define TRANSACTION_DEBUG DEBUG_LEVEL_NONE

#define ROUNDING_DEBUG DEBUG_LEVEL_NONE

#define CLEARING_DEBUG DEBUG_LEVEL_INFO

#define PROOF_DEBUG DEBUG_LEVEL_NONE

#define BLOCK_DEBUG DEBUG_LEVEL_INFO

#define DEMAND_CALC_DEBUG DEBUG_LEVEL_NONE

#define INTEGRITY_CHECKS OFF

#define MEMPOOL_DEBUG DEBUG_LEVEL_INFO
/*
class DebugObject {
	FILE* f = stdout;
	bool file_closeable = false;
	DebugObject& get_global_instance() {
		static DebugObject obj;
		return obj;
	}
public:

TODO consider contextual activation of debug functionality.
};*/

//#define LOG(s, ...) std::printf((std::string("%-40s") + s + "\n").c_str(), (std::string(__FILE__) + "." + std::to_string(__LINE__) + ":").c_str(),##__VA_ARGS__)
#define LOG(s, ...) std::printf((std::string("%-40s") + s + "\n").c_str(), (std::string(__FILE__) + "." + std::to_string(__LINE__) + ":").c_str() __VA_OPT__(,) __VA_ARGS__)

struct DebugUtils {
	static std::string __array_to_str(const unsigned char* array, const int len) {
		std::stringstream s;
		s.fill('0');
		for (int i = 0; i < len; i++) {
			s<< std::setw(2) << std::hex << (unsigned short)array[i];
		}
		return s.str();
	}
};

#if DEBUG_LEVEL <= DEBUG_LEVEL_INFO || TRIE_DEBUG <= DEBUG_LEVEL_INFO || ROUNDING_DEBUG <= DEBUG_LEVEL_INFO ||  TATONNEMENT_DEBUG <= DEBUG_LEVEL_INFO || PROOF_DEBUG <= DEBUG_LEVEL_INFO
#define TEST_START() LOG("Starting Test:%s", __FUNCTION__)
#else
#define TEST_START() (void)0
#endif

#if DEBUG_LEVEL <= DEBUG_LEVEL_ERROR
#define ERROR(s, ...) LOG(s, __VA_ARGS__)
#define ERROR_F(s) s
#else
#define ERROR(s, ...) (void)0
#define ERROR_F(s) (void)0
#endif

#if TRANSACTION_DEBUG <= DEBUG_LEVEL_ERROR
#define TX(s, ...) LOG(s, __VA_ARGS__)
#define TX_F(s) s
#else
#define TX(s, ...) (void)0
#define TX_F(s) (void)0
#endif

#if TRANSACTION_DEBUG <= DEBUG_LEVEL_INFO
#define TX_INFO(s, ...) LOG(s, __VA_ARGS__)
#define TX_INFO_F(s) s
#else
#define TX_INFO(s, ...) (void)0
#define TX_INFO_F(s) (void)0
#endif

#if DEBUG_LEVEL <= DEBUG_LEVEL_INFO
#define INFO(s, ...) LOG(s, __VA_ARGS__)
#define INFO_F(s) s
#else
#define INFO(s, ...) (void)0
#define INFO_F(s) (void)0
#endif

#if DEBUG_LEVEL <= DEBUG_LEVEL_WARN
#define WARN(s, ...) LOG(s, __VA_ARGS__)
#define WARN_F(s) s
#else
#define WARN(s, ...) (void)0
#define WARN_F(s) (void)0
#endif

#if DEBUG_LEVEL <= DEBUG_LEVEL_TRACE
#define TRACE(s, ...) LOG(s, __VA_ARGS__)
#define TRACE_F(s) s
#else
#define TRACE(s, ...) (void)0
#define TRACE_F (void)0
#endif

#if TRIE_DEBUG <= DEBUG_LEVEL_ERROR
#define TRIE_ERROR(s, ...) LOG(s, __VA_ARGS__)
#define TRIE_ERROR_F(s) s
#else
#define TRIE_ERROR(s, ...) (void)0
#define TRIE_ERROR_F(s) (void)0
#endif

#if TRIE_DEBUG <= DEBUG_LEVEL_INFO
#define TRIE_INFO(s, ...) LOG(s, __VA_ARGS__)
#define TRIE_INFO_F(s) s
#else
#define TRIE_INFO(s, ...) (void)0
#define TRIE_INFO_F(s) (void)0
#endif

#if TATONNEMENT_DEBUG <= DEBUG_LEVEL_INFO
#define TAT_INFO(s, ...) LOG(s, __VA_ARGS__)
#define TAT_INFO_F(s) s
#else
#define TAT_INFO(s, ...) (void)0
#define TAT_INFO_F(s) (void)0
#endif

#if ROUNDING_DEBUG <= DEBUG_LEVEL_INFO
#define R_INFO(s, ...) LOG(s, __VA_ARGS__)
#define R_INFO_F(s) s
#else
#define R_INFO(s, ...) (void)0
#define R_INFO_F(s) (void)0
#endif

#if CLEARING_DEBUG <= DEBUG_LEVEL_INFO
#define CLEARING_INFO(s, ...) LOG(s, __VA_ARGS__)
#define CLEARING_INFO_F(s) s
#else
#define CLEARING_INFO(s, ...) (void)0
#define CLEARING_INFO_F(s) (void)0
#endif

#if PROOF_DEBUG <= DEBUG_LEVEL_INFO
#define PROOF_INFO(s, ...) LOG(s, __VA_ARGS__)
#define PROOF_INFO_F(s) s
#else
#define PROOF_INFO(s, ...) (void)0
#define PROOF_INFO_F(s) (void)0
#endif

#if BLOCK_DEBUG <= DEBUG_LEVEL_INFO
#define BLOCK_INFO(s, ...) LOG(s, __VA_ARGS__)
#define BLOCK_INFO_F(s) s
#else
#define BLOCK_INFO(s, ...) (void)0
#define BLOCK_INFO_F(s) (void)0
#endif

#if DEMAND_CALC_DEBUG <= DEBUG_LEVEL_INFO
#define DEMAND_CALC_INFO(s, ...) LOG(s, __VA_ARGS__)
#define DEMAND_CALC_INFO_F(s) s
#else
#define DEMAND_CALC_INFO(s, ...) (void)0
#define DEMAND_CALC_INFO_F(s) (void)0
#endif

#if MEMPOOL_DEBUG <= DEBUG_LEVEL_INFO
#define MEMPOOL_INFO(s, ...) LOG(s, __VA_ARGS__)
#define MEMPOOL_INFO_F(s) s
#else
#define MEMPOOL_INFO(s, ...) (void)0
#define MEMPOOL_INFO_F(s) (void)0
#endif

#if INTEGRITY_CHECKS == ON
#define INTEGRITY_CHECK(s,...) LOG(s, __VA_ARGS__)
#define INTEGRITY_CHECK_F(s) s
#else
#define INTEGRITY_CHECK(s,...) (void)0
#define INTEGRITY_CHECK_F(s) (void)0
#endif
