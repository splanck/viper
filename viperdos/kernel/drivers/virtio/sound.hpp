//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/drivers/virtio/sound.hpp
// Purpose: VirtIO-Sound device driver for audio playback.
// Key invariants: Uses controlq for commands; txq for PCM output.
// Ownership/Lifetime: Singleton device; initialized once.
// Links: kernel/drivers/virtio/sound.cpp
//
//===----------------------------------------------------------------------===//

#pragma once

#include "virtio.hpp"
#include "virtqueue.hpp"

/**
 * @file sound.hpp
 * @brief VirtIO-Sound device driver.
 *
 * @details
 * Implements basic PCM audio playback via the VirtIO-Sound specification.
 * Supports:
 * - PCM stream configuration (sample rate, channels, format)
 * - Audio buffer submission for playback
 * - Volume control
 *
 * The driver uses four virtqueues:
 * - controlq (queue 0): Configuration commands
 * - eventq  (queue 1): Async notifications
 * - txq     (queue 2): PCM output (playback)
 * - rxq     (queue 3): PCM input (recording, unused)
 */
namespace virtio {

// VirtIO-Sound request types
namespace snd_cmd {
// Jack control
constexpr u32 R_JACK_INFO = 1;
constexpr u32 R_JACK_REMAP = 2;

// PCM control
constexpr u32 R_PCM_INFO = 0x0100;
constexpr u32 R_PCM_SET_PARAMS = 0x0101;
constexpr u32 R_PCM_PREPARE = 0x0102;
constexpr u32 R_PCM_RELEASE = 0x0103;
constexpr u32 R_PCM_START = 0x0104;
constexpr u32 R_PCM_STOP = 0x0105;

// Channel map
constexpr u32 R_CHMAP_INFO = 0x0200;

// Response codes
constexpr u32 S_OK = 0x8000;
constexpr u32 S_BAD_MSG = 0x8001;
constexpr u32 S_NOT_SUPP = 0x8002;
constexpr u32 S_IO_ERR = 0x8003;
} // namespace snd_cmd

// PCM formats
namespace snd_fmt {
constexpr u8 IMA_ADPCM = 0;
constexpr u8 MU_LAW = 1;
constexpr u8 A_LAW = 2;
constexpr u8 S8 = 3;
constexpr u8 U8 = 4;
constexpr u8 S16 = 5;
constexpr u8 U16 = 6;
constexpr u8 S18_3 = 7;
constexpr u8 U18_3 = 8;
constexpr u8 S20_3 = 9;
constexpr u8 U20_3 = 10;
constexpr u8 S24_3 = 11;
constexpr u8 U24_3 = 12;
constexpr u8 S20 = 13;
constexpr u8 U20 = 14;
constexpr u8 S24 = 15;
constexpr u8 U24 = 16;
constexpr u8 S32 = 17;
constexpr u8 U32 = 18;
constexpr u8 FLOAT = 19;
constexpr u8 FLOAT64 = 20;
} // namespace snd_fmt

// PCM rates (bitmask)
namespace snd_rate {
constexpr u64 R_5512 = 1ULL << 0;
constexpr u64 R_8000 = 1ULL << 1;
constexpr u64 R_11025 = 1ULL << 2;
constexpr u64 R_16000 = 1ULL << 3;
constexpr u64 R_22050 = 1ULL << 4;
constexpr u64 R_32000 = 1ULL << 5;
constexpr u64 R_44100 = 1ULL << 6;
constexpr u64 R_48000 = 1ULL << 7;
constexpr u64 R_64000 = 1ULL << 8;
constexpr u64 R_88200 = 1ULL << 9;
constexpr u64 R_96000 = 1ULL << 10;
constexpr u64 R_176400 = 1ULL << 11;
constexpr u64 R_192000 = 1ULL << 12;
} // namespace snd_rate

// Direction
namespace snd_dir {
constexpr u8 OUTPUT = 0;
constexpr u8 INPUT = 1;
} // namespace snd_dir

// VirtIO-Sound configuration space
struct SndConfig {
    u32 jacks;
    u32 streams;
    u32 chmaps;
} __attribute__((packed));

// Generic control header
struct SndHdr {
    u32 code;
} __attribute__((packed));

// PCM info query
struct SndQueryInfo {
    SndHdr hdr;
    u32 start_id;
    u32 count;
    u32 size; // sizeof(SndPcmInfo)
} __attribute__((packed));

// PCM stream info response
struct SndPcmInfo {
    u32 hdr_code;
    u32 features;
    u64 formats;     // Bitmask of supported formats
    u64 rates;       // Bitmask of supported rates
    u8 direction;
    u8 channels_min;
    u8 channels_max;
    u8 padding[5];
} __attribute__((packed));

// PCM set parameters
struct SndPcmSetParams {
    SndHdr hdr;
    u32 stream_id;
    u32 buffer_bytes;
    u32 period_bytes;
    u32 features;
    u8 channels;
    u8 format;
    u8 rate;
    u8 padding;
} __attribute__((packed));

// PCM stream command (prepare/release/start/stop)
struct SndPcmCmd {
    SndHdr hdr;
    u32 stream_id;
} __attribute__((packed));

// PCM transfer header (prepended to audio data in txq)
struct SndPcmXfer {
    u32 stream_id;
} __attribute__((packed));

// PCM transfer status (device writes back)
struct SndPcmStatus {
    u32 status;
    u32 latency_bytes;
} __attribute__((packed));

/**
 * @brief VirtIO-Sound device driver.
 *
 * @details
 * Provides basic PCM audio playback. Supports configurable sample rate,
 * channel count, and format.
 */
class SoundDevice : public Device {
  public:
    bool init();

    /// Check if device is initialized and has output streams.
    bool is_available() const { return initialized_ && num_output_streams_ > 0; }

    /// Get number of output streams.
    u32 num_output_streams() const { return num_output_streams_; }

    /**
     * @brief Configure a PCM output stream.
     *
     * @param stream_id Output stream index.
     * @param sample_rate Sample rate in Hz (e.g., 44100, 48000).
     * @param channels Number of channels (1=mono, 2=stereo).
     * @param bits Bits per sample (8 or 16).
     * @return true on success.
     */
    bool configure_stream(u32 stream_id, u32 sample_rate, u8 channels, u8 bits);

    /// Prepare a stream for playback.
    bool prepare(u32 stream_id);

    /// Start playback on a stream.
    bool start(u32 stream_id);

    /// Stop playback on a stream.
    bool stop(u32 stream_id);

    /// Release a stream.
    bool release(u32 stream_id);

    /**
     * @brief Submit a PCM audio buffer for playback.
     *
     * @param stream_id Stream index.
     * @param data Audio sample data.
     * @param len Length in bytes.
     * @return Number of bytes queued, or -1 on error.
     */
    i64 write_pcm(u32 stream_id, const void *data, usize len);

    /// Set volume (0-255, applied in software before submission).
    void set_volume(u8 vol) { volume_ = vol; }

    /// Get current volume.
    u8 volume() const { return volume_; }

  private:
    Virtqueue controlq_;
    Virtqueue eventq_;
    Virtqueue txq_;

    bool initialized_{false};
    u32 num_jacks_{0};
    u32 num_streams_{0};
    u32 num_chmaps_{0};
    u32 num_output_streams_{0};
    u32 first_output_stream_{0};

    u8 volume_{255}; // Max volume

    // DMA buffers
    u8 *cmd_buf_{nullptr};
    u64 cmd_buf_phys_{0};
    u8 *resp_buf_{nullptr};
    u64 resp_buf_phys_{0};
    u8 *pcm_buf_{nullptr};
    u64 pcm_buf_phys_{0};
    u8 *status_buf_{nullptr};
    u64 status_buf_phys_{0};

    static constexpr usize CMD_BUF_SIZE = 4096;
    static constexpr usize PCM_BUF_SIZE = 16384; // 16KB PCM buffer

    bool send_control(usize cmd_size, usize resp_size);
    bool send_stream_cmd(u32 code, u32 stream_id);
    u8 rate_to_index(u32 sample_rate);
};

/**
 * @brief Software audio mixer for multi-stream support.
 *
 * @details
 * Accepts PCM data from multiple virtual streams and mixes them
 * into a single output buffer for the hardware. Uses i32 accumulators
 * with i16 saturation clamping.
 *
 * Usage:
 * - Write to virtual streams 0-MAX_MIX_STREAMS via submit()
 * - Call flush() to mix all pending data and send to hardware
 * - Automatic flush when any stream's buffer is full
 */
class AudioMixer {
  public:
    static constexpr u32 MAX_MIX_STREAMS = 4;
    static constexpr usize MIX_BUF_SAMPLES = 4096; // Samples per channel

    /**
     * @brief Initialize the mixer.
     * @param dev Sound device for output.
     */
    void init(SoundDevice *dev);

    /**
     * @brief Submit PCM data for mixing.
     *
     * @param stream_id Virtual stream index (0-3).
     * @param data 16-bit signed PCM samples.
     * @param len Length in bytes.
     * @return Number of bytes accepted, or -1 on error.
     */
    i64 submit(u32 stream_id, const void *data, usize len);

    /**
     * @brief Flush mixed audio to hardware.
     * @return Number of bytes flushed, or -1 on error.
     */
    i64 flush();

    /**
     * @brief Check if mixing is active (more than one stream has pending data).
     */
    bool is_active() const { return active_streams_ > 1; }

  private:
    struct StreamBuf {
        i16 samples[MIX_BUF_SAMPLES];
        usize count; // Number of samples written
        bool active; // Has pending data
    };

    StreamBuf streams_[MAX_MIX_STREAMS];
    SoundDevice *dev_{nullptr};
    u32 active_streams_{0};
    u32 hw_stream_id_{0};

    /// Clamp i32 to i16 range
    static i16 clamp16(i32 val) {
        if (val > 32767) return 32767;
        if (val < -32768) return -32768;
        return static_cast<i16>(val);
    }
};

// Global sound device initialization and access
void sound_init();
SoundDevice *sound_device();

/// Get the global audio mixer
AudioMixer *audio_mixer();

} // namespace virtio
