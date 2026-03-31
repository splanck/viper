# Plan 16: MP3 ID3v1 Tag Handling

## Context

ID3v1 tags are 128 bytes appended to the end of an MP3 file, starting with the ASCII
string "TAG". The MP3 decoder's frame iteration loop (`rt_mp3.c:400-412`) may attempt to
decode these bytes as audio frames, potentially producing garbage at the end of playback.

## Problem

The frame sync search (`mp3_find_sync`) could match a false sync word within the ID3v1
tag data. More commonly, the loop exits cleanly because the 128 tag bytes don't form a
valid frame header — but in edge cases, random byte patterns could match `0xFF 0xFB` and
cause a decode attempt with garbled data.

## Fix

Before the frame iteration loop, check if the last 128 bytes of the file start with "TAG".
If so, reduce the effective file length by 128 bytes.

### Implementation (~15 LOC)

In `mp3_decode_file()`, after the ID3v2 skip and before the frame loop:

```c
// Skip ID3v1 tag at end of file (128 bytes starting with "TAG")
size_t effective_len = len;
if (len >= 128 && data[len - 128] == 'T' && data[len - 127] == 'A' && data[len - 126] == 'G') {
    effective_len = len - 128;
}
```

Then use `effective_len` instead of `len` in the frame loop's bounds check:

```c
while (pos + 4 <= effective_len) {
    ...
    if (pos + (size_t)hdr.frame_size > effective_len)
        break;
    ...
}
```

### Also handle ID3v1.1

ID3v1.1 is identical to v1 (same 128-byte suffix, same "TAG" prefix) but uses byte 125
as a track number when byte 124 is zero. No additional detection needed — the "TAG"
check covers both versions.

### APEv2 tags

Some MP3 files have APEv2 tags (used by foobar2000, Winamp). These appear before ID3v1
and start with "APETAGEX". Handling these is out of scope for this plan — they're rare
and the frame sync validator will skip over them naturally (no valid MP3 sync words in
tag data).

## Files to Modify

| File | Change |
|------|--------|
| `src/runtime/audio/rt_mp3.c` | Add ID3v1 tail detection in `mp3_decode_file()` |

## Estimated LOC

~15 lines.

## Verification

- MP3 without ID3v1: unchanged behavior
- MP3 with ID3v1: no garbage audio at end of playback
- MP3 with both ID3v2 (start) and ID3v1 (end): both correctly skipped
- Existing `test_mp3_decode` tests pass
