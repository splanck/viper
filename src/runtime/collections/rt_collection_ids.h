//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/collections/rt_collection_ids.h
// Purpose: Stable runtime class identifiers for collection heap objects.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#define RT_SEQ_CLASS_ID INT64_C(-0x430300)
#define RT_LIST_CLASS_ID INT64_C(-0x430301)
#define RT_SET_CLASS_ID INT64_C(-0x430302)
#define RT_STACK_CLASS_ID INT64_C(-0x430303)
#define RT_QUEUE_CLASS_ID INT64_C(-0x430304)
#define RT_RING_CLASS_ID INT64_C(-0x430305)
#define RT_DEQUE_CLASS_ID INT64_C(-0x430306)
#define RT_BAG_CLASS_ID INT64_C(-0x430307)
#define RT_ORDEREDMAP_CLASS_ID INT64_C(-0x430308)
#define RT_TREEMAP_CLASS_ID INT64_C(-0x430309)
#define RT_FROZENMAP_CLASS_ID INT64_C(-0x43030A)
#define RT_FROZENSET_CLASS_ID INT64_C(-0x43030B)
#define RT_SPARSEARRAY_CLASS_ID INT64_C(-0x43030C)
#define RT_INTMAP_CLASS_ID INT64_C(-0x43030D)
#define RT_DEFAULTMAP_CLASS_ID INT64_C(-0x43030E)
#define RT_MULTIMAP_CLASS_ID INT64_C(-0x43030F)
#define RT_LRUCACHE_CLASS_ID INT64_C(-0x430310)
#define RT_TRIE_CLASS_ID INT64_C(-0x430311)
#define RT_BIMAP_CLASS_ID INT64_C(-0x430312)
#define RT_BITSET_CLASS_ID INT64_C(-0x430313)
#define RT_BLOOMFILTER_CLASS_ID INT64_C(-0x430314)
#define RT_COUNTMAP_CLASS_ID INT64_C(-0x430315)
#define RT_PQUEUE_CLASS_ID INT64_C(-0x430316)
#define RT_ITERATOR_CLASS_ID INT64_C(-0x430317)
#define RT_SORTEDSET_CLASS_ID INT64_C(-0x430318)
#define RT_UNIONFIND_CLASS_ID INT64_C(-0x430319)
#define RT_WEAKMAP_CLASS_ID INT64_C(-0x43031A)
#define RT_MAP_CLASS_ID INT64_C(-0x43031B)
#define RT_F64BUFFER_CLASS_ID INT64_C(-0x43031C)
#define RT_I64BUFFER_CLASS_ID INT64_C(-0x43031D)
#define RT_LAZYSEQ_CLASS_ID INT64_C(-0x43031E)
