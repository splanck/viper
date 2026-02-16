//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
#include "sound.hpp"
#include "../../console/serial.hpp"
#include "../../lib/mem.hpp"
#include "../../mm/pmm.hpp"

/**
 * @file sound.cpp
 * @brief VirtIO-Sound driver implementation.
 *
 * @details
 * Implements PCM audio playback via the VirtIO-Sound specification (device type 25).
 * Supports stream configuration, buffer submission, and basic volume control.
 */
namespace virtio {

namespace {
constexpr u32 SOUND_DEVICE_TYPE = 25;
} // namespace

// Global sound device instance
static SoundDevice g_sound_device;
static bool g_sound_initialized = false;

SoundDevice *sound_device() {
    return g_sound_initialized ? &g_sound_device : nullptr;
}

bool SoundDevice::init() {
    u64 base = find_device(SOUND_DEVICE_TYPE);
    if (!base) {
        serial::puts("[virtio-snd] No sound device found\n");
        return false;
    }

    if (!basic_init(base)) {
        serial::puts("[virtio-snd] Device init failed\n");
        return false;
    }

    serial::puts("[virtio-snd] Initializing sound device at 0x");
    serial::put_hex(base);
    serial::puts(" version=");
    serial::put_dec(version());
    serial::puts(is_legacy() ? " (legacy)\n" : " (modern)\n");

    // Read config
    num_jacks_ = read_config32(0);
    num_streams_ = read_config32(4);
    num_chmaps_ = read_config32(8);

    serial::puts("[virtio-snd] jacks=");
    serial::put_dec(num_jacks_);
    serial::puts(" streams=");
    serial::put_dec(num_streams_);
    serial::puts(" chmaps=");
    serial::put_dec(num_chmaps_);
    serial::puts("\n");

    // Negotiate features
    u64 required = 0;
    if (!is_legacy()) {
        required |= features::VERSION_1;
    }
    if (!negotiate_features(required)) {
        serial::puts("[virtio-snd] Feature negotiation failed\n");
        set_status(status::FAILED);
        return false;
    }

    // Initialize virtqueues: controlq(0), eventq(1), txq(2), rxq(3)
    if (!controlq_.init(this, 0, 64)) {
        serial::puts("[virtio-snd] Failed to init controlq\n");
        set_status(status::FAILED);
        return false;
    }

    // eventq is optional, try but don't fail
    eventq_.init(this, 1, 16);

    if (!txq_.init(this, 2, 64)) {
        serial::puts("[virtio-snd] Failed to init txq\n");
        set_status(status::FAILED);
        return false;
    }

    // rxq (queue 3) - skip for playback-only driver

    // Allocate DMA buffers using helper (Issue #36-38)
    cmd_dma_ = alloc_dma_buffer(1);
    resp_dma_ = alloc_dma_buffer(1);
    pcm_dma_ = alloc_dma_buffer(4); // 16KB
    status_dma_ = alloc_dma_buffer(1);

    if (!cmd_dma_.is_valid() || !resp_dma_.is_valid() || !pcm_dma_.is_valid() ||
        !status_dma_.is_valid()) {
        serial::puts("[virtio-snd] Failed to allocate DMA buffers\n");
        free_dma_buffer(cmd_dma_);
        free_dma_buffer(resp_dma_);
        free_dma_buffer(pcm_dma_);
        free_dma_buffer(status_dma_);
        set_status(status::FAILED);
        return false;
    }

    // Set up convenience pointers for existing code
    cmd_buf_phys_ = cmd_dma_.phys;
    resp_buf_phys_ = resp_dma_.phys;
    pcm_buf_phys_ = pcm_dma_.phys;
    status_buf_phys_ = status_dma_.phys;
    cmd_buf_ = cmd_dma_.virt;
    resp_buf_ = resp_dma_.virt;
    pcm_buf_ = pcm_dma_.virt;
    status_buf_ = status_dma_.virt;

    add_status(status::DRIVER_OK);

    // Query PCM stream info to find output streams
    if (num_streams_ > 0) {
        SndQueryInfo *query = reinterpret_cast<SndQueryInfo *>(cmd_buf_);
        query->hdr.code = snd_cmd::R_PCM_INFO;
        query->start_id = 0;
        query->count = num_streams_ > 8 ? 8 : num_streams_;
        query->size = sizeof(SndPcmInfo);

        usize resp_size = sizeof(SndHdr) + query->count * sizeof(SndPcmInfo);
        // Clamp response to buffer capacity
        if (resp_size > pmm::PAGE_SIZE)
            resp_size = pmm::PAGE_SIZE;
        u32 max_infos = (resp_size - sizeof(SndHdr)) / sizeof(SndPcmInfo);
        if (send_control(sizeof(SndQueryInfo), resp_size)) {
            SndHdr *resp_hdr = reinterpret_cast<SndHdr *>(resp_buf_);
            if (resp_hdr->code == snd_cmd::S_OK) {
                SndPcmInfo *infos = reinterpret_cast<SndPcmInfo *>(resp_buf_ + sizeof(SndHdr));
                for (u32 i = 0; i < max_infos && i < query->count; i++) {
                    if (infos[i].direction == snd_dir::OUTPUT) {
                        if (num_output_streams_ == 0)
                            first_output_stream_ = i;
                        num_output_streams_++;
                        serial::puts("[virtio-snd] Output stream ");
                        serial::put_dec(i);
                        serial::puts(": ch_min=");
                        serial::put_dec(infos[i].channels_min);
                        serial::puts(" ch_max=");
                        serial::put_dec(infos[i].channels_max);
                        serial::puts("\n");
                    }
                }
            }
        }
    }

    initialized_ = true;
    serial::puts("[virtio-snd] Driver initialized (");
    serial::put_dec(num_output_streams_);
    serial::puts(" output streams)\n");
    return true;
}

bool SoundDevice::send_control(usize cmd_size, usize resp_size) {
    i32 cmd_desc = controlq_.alloc_desc();
    i32 resp_desc = controlq_.alloc_desc();

    if (cmd_desc < 0 || resp_desc < 0) {
        if (cmd_desc >= 0)
            controlq_.free_desc(cmd_desc);
        if (resp_desc >= 0)
            controlq_.free_desc(resp_desc);
        return false;
    }

    asm volatile("dsb sy" ::: "memory");

    controlq_.set_desc(cmd_desc, cmd_buf_phys_, cmd_size, desc_flags::NEXT);
    controlq_.chain_desc(cmd_desc, resp_desc);
    controlq_.set_desc(resp_desc, resp_buf_phys_, resp_size, desc_flags::WRITE);

    controlq_.submit(cmd_desc);
    controlq_.kick();

    bool completed = poll_for_completion(controlq_, cmd_desc);

    controlq_.free_desc(cmd_desc);
    controlq_.free_desc(resp_desc);

    if (!completed) {
        serial::puts("[virtio-snd] Control command timeout\n");
        return false;
    }
    return true;
}

bool SoundDevice::send_stream_cmd(u32 code, u32 stream_id) {
    SndPcmCmd *cmd = reinterpret_cast<SndPcmCmd *>(cmd_buf_);
    cmd->hdr.code = code;
    cmd->stream_id = stream_id;

    if (!send_control(sizeof(SndPcmCmd), sizeof(SndHdr))) {
        return false;
    }

    SndHdr *resp = reinterpret_cast<SndHdr *>(resp_buf_);
    return resp->code == snd_cmd::S_OK;
}

/// @brief Map a sample rate in Hz to the virtio-snd rate index.
/// @details The indices correspond to the VIRTIO_SND_PCM_RATE_* enumeration
///   in the virtio-sound specification. Returns index 7 (48 kHz) as the
///   default fallback for unrecognised rates, since 48 kHz is the most
///   widely supported PCM rate across hardware and emulators.
u8 SoundDevice::rate_to_index(u32 sample_rate) {
    switch (sample_rate) {
        case 5512:
            return 0;
        case 8000:
            return 1;
        case 11025:
            return 2;
        case 16000:
            return 3;
        case 22050:
            return 4;
        case 32000:
            return 5;
        case 44100:
            return 6;
        case 48000:
            return 7;
        case 64000:
            return 8;
        case 88200:
            return 9;
        case 96000:
            return 10;
        case 176400:
            return 11;
        case 192000:
            return 12;
        default:
            return 7; // Default to 48000
    }
}

bool SoundDevice::configure_stream(u32 stream_id, u32 sample_rate, u8 channels, u8 bits) {
    if (!initialized_)
        return false;

    SndPcmSetParams *cmd = reinterpret_cast<SndPcmSetParams *>(cmd_buf_);
    cmd->hdr.code = snd_cmd::R_PCM_SET_PARAMS;
    cmd->stream_id = stream_id;
    cmd->buffer_bytes = PCM_BUF_SIZE;
    cmd->period_bytes = PCM_BUF_SIZE / PERIODS_PER_BUFFER;
    cmd->features = 0;
    cmd->channels = channels;
    cmd->format = (bits == 8) ? snd_fmt::U8 : snd_fmt::S16;
    cmd->rate = rate_to_index(sample_rate);
    cmd->padding = 0;

    if (!send_control(sizeof(SndPcmSetParams), sizeof(SndHdr))) {
        serial::puts("[virtio-snd] Failed to set stream params\n");
        return false;
    }

    SndHdr *resp = reinterpret_cast<SndHdr *>(resp_buf_);
    if (resp->code != snd_cmd::S_OK) {
        serial::puts("[virtio-snd] Set params rejected: 0x");
        serial::put_hex(resp->code);
        serial::puts("\n");
        return false;
    }

    return true;
}

bool SoundDevice::prepare(u32 stream_id) {
    return send_stream_cmd(snd_cmd::R_PCM_PREPARE, stream_id);
}

bool SoundDevice::start(u32 stream_id) {
    return send_stream_cmd(snd_cmd::R_PCM_START, stream_id);
}

bool SoundDevice::stop(u32 stream_id) {
    return send_stream_cmd(snd_cmd::R_PCM_STOP, stream_id);
}

bool SoundDevice::release(u32 stream_id) {
    return send_stream_cmd(snd_cmd::R_PCM_RELEASE, stream_id);
}

i64 SoundDevice::write_pcm(u32 stream_id, const void *data, usize len) {
    if (!initialized_ || !data || len == 0)
        return -1;

    // Clamp to buffer size minus header
    usize max_data = PCM_BUF_SIZE - sizeof(SndPcmXfer);
    if (len > max_data)
        len = max_data;

    // Copy audio data to DMA buffer with volume scaling
    SndPcmXfer *xfer = reinterpret_cast<SndPcmXfer *>(pcm_buf_);
    xfer->stream_id = stream_id;

    u8 *dst = pcm_buf_ + sizeof(SndPcmXfer);
    const u8 *src = reinterpret_cast<const u8 *>(data);

    if (volume_ == 255) {
        // Full volume: direct copy
        lib::memcpy(dst, src, len);
    } else if (volume_ == 0) {
        // Muted: silence
        lib::memset(dst, 0, len);
    } else {
        // Scale 16-bit samples
        const i16 *s16 = reinterpret_cast<const i16 *>(src);
        i16 *d16 = reinterpret_cast<i16 *>(dst);
        usize samples = len / 2;
        for (usize i = 0; i < samples; i++) {
            i32 scaled = (static_cast<i32>(s16[i]) * volume_) / 255;
            d16[i] = static_cast<i16>(scaled);
        }
    }

    // Submit via txq: xfer header + data, then status response
    i32 data_desc = txq_.alloc_desc();
    i32 status_desc = txq_.alloc_desc();

    if (data_desc < 0 || status_desc < 0) {
        if (data_desc >= 0)
            txq_.free_desc(data_desc);
        if (status_desc >= 0)
            txq_.free_desc(status_desc);
        return -1;
    }

    asm volatile("dsb sy" ::: "memory");

    // Data descriptor: xfer header + PCM data (device reads)
    usize xfer_size = sizeof(SndPcmXfer) + len;
    txq_.set_desc(data_desc, pcm_buf_phys_, xfer_size, desc_flags::NEXT);
    txq_.chain_desc(data_desc, status_desc);

    // Status descriptor (device writes)
    txq_.set_desc(status_desc, status_buf_phys_, sizeof(SndPcmStatus), desc_flags::WRITE);

    txq_.submit(data_desc);
    txq_.kick();

    // Wait for completion
    bool completed = poll_for_completion(txq_, data_desc);

    txq_.free_desc(data_desc);
    txq_.free_desc(status_desc);

    if (!completed)
        return -1;

    SndPcmStatus *st = reinterpret_cast<SndPcmStatus *>(status_buf_);
    if (st->status != snd_cmd::S_OK)
        return -1;
    return static_cast<i64>(len);
}

// =============================================================================
// Audio Mixer
// =============================================================================

static AudioMixer g_audio_mixer;
static bool g_mixer_initialized = false;

AudioMixer *audio_mixer() {
    return g_mixer_initialized ? &g_audio_mixer : nullptr;
}

void AudioMixer::init(SoundDevice *dev) {
    dev_ = dev;
    active_streams_ = 0;
    hw_stream_id_ = 0;
    for (u32 i = 0; i < MAX_MIX_STREAMS; i++) {
        streams_[i].count = 0;
        streams_[i].active = false;
    }
}

i64 AudioMixer::submit(u32 stream_id, const void *data, usize len) {
    if (!dev_ || stream_id >= MAX_MIX_STREAMS || !data || len == 0)
        return -1;

    usize samples = len / sizeof(i16);
    if (samples > MIX_BUF_SAMPLES)
        samples = MIX_BUF_SAMPLES;

    const i16 *src = reinterpret_cast<const i16 *>(data);
    StreamBuf &buf = streams_[stream_id];

    // Copy samples into stream buffer
    lib::memcpy(buf.samples, src, samples * sizeof(i16));
    buf.count = samples;

    if (!buf.active) {
        buf.active = true;
        active_streams_++;
    }

    // Auto-flush when we have data from at least one stream
    if (active_streams_ >= 1) {
        i64 result = flush();
        if (result < 0)
            return result;
    }

    return static_cast<i64>(samples * sizeof(i16));
}

i64 AudioMixer::flush() {
    if (!dev_ || active_streams_ == 0)
        return 0;

    // Find the maximum sample count across all active streams
    usize max_count = 0;
    for (u32 i = 0; i < MAX_MIX_STREAMS; i++) {
        if (streams_[i].active && streams_[i].count > max_count)
            max_count = streams_[i].count;
    }

    if (max_count == 0)
        return 0;

    // Mix all active streams into a temporary buffer
    // Use i32 accumulators for headroom
    i16 mix_out[MIX_BUF_SAMPLES];
    for (usize s = 0; s < max_count; s++) {
        i32 acc = 0;
        for (u32 ch = 0; ch < MAX_MIX_STREAMS; ch++) {
            if (streams_[ch].active && s < streams_[ch].count) {
                acc += static_cast<i32>(streams_[ch].samples[s]);
            }
        }
        mix_out[s] = clamp16(acc);
    }

    // Clear all stream buffers
    for (u32 i = 0; i < MAX_MIX_STREAMS; i++) {
        streams_[i].count = 0;
        streams_[i].active = false;
    }
    active_streams_ = 0;

    // Send mixed output to hardware
    usize bytes = max_count * sizeof(i16);
    return dev_->write_pcm(hw_stream_id_, mix_out, bytes);
}

void sound_init() {
    serial::puts("[virtio-snd] Starting sound_init()...\n");
    if (g_sound_device.init()) {
        g_sound_initialized = true;
        g_audio_mixer.init(&g_sound_device);
        g_mixer_initialized = true;
        serial::puts("[virtio-snd] Sound device ready (mixer enabled)\n");
    } else {
        serial::puts("[virtio-snd] Sound device not present or init failed\n");
    }
}

} // namespace virtio
