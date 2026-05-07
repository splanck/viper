//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/io/rt_io_class_ids.h
// Purpose: Runtime class-id tags for opaque Viper.IO heap handles.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#define RT_BINFILE_CLASS_ID INT64_C(-0x760001)
#define RT_MEMSTREAM_CLASS_ID INT64_C(-0x760002)
#define RT_STREAM_CLASS_ID INT64_C(-0x760003)
#define RT_LINEREADER_CLASS_ID INT64_C(-0x760004)
#define RT_LINEWRITER_CLASS_ID INT64_C(-0x760005)
#define RT_ARCHIVE_CLASS_ID INT64_C(-0x760006)
#define RT_SAVEDATA_CLASS_ID INT64_C(-0x760007)
#define RT_WATCHER_CLASS_ID INT64_C(-0x760008)
#define RT_BINBUF_CLASS_ID INT64_C(-0x760009)
