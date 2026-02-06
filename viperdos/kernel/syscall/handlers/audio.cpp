//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: kernel/syscall/handlers/audio.cpp
// Purpose: Audio syscall handlers (0x130-0x13F).
//
//===----------------------------------------------------------------------===//

#include "../../drivers/virtio/sound.hpp"
#include "handlers_internal.hpp"

namespace syscall {

/// @brief Configure an audio stream's sample rate, channels, and bit depth.
/// @details The third argument (a2) uses packed encoding: low 8 bits hold the
///   channel count, next 8 bits hold the bit depth (e.g. 16 or 24).
SyscallResult sys_audio_configure(u64 a0, u64 a1, u64 a2, u64, u64, u64) {
    u32 stream_id = static_cast<u32>(a0);
    u32 sample_rate = static_cast<u32>(a1);
    // channels in low 8 bits, bits in next 8 bits
    u8 channels = static_cast<u8>(a2 & 0xFF);
    u8 bits = static_cast<u8>((a2 >> 8) & 0xFF);

    auto *dev = virtio::sound_device();
    if (!dev || !dev->is_available()) {
        return err_not_found();
    }

    if (!dev->configure_stream(stream_id, sample_rate, channels, bits)) {
        return err_io();
    }
    return SyscallResult::ok();
}

/// @brief Prepare an audio stream for playback after configuration.
SyscallResult sys_audio_prepare(u64 a0, u64, u64, u64, u64, u64) {
    u32 stream_id = static_cast<u32>(a0);

    auto *dev = virtio::sound_device();
    if (!dev || !dev->is_available()) {
        return err_not_found();
    }

    if (!dev->prepare(stream_id)) {
        return err_io();
    }
    return SyscallResult::ok();
}

/// @brief Start playback on an already-prepared audio stream.
SyscallResult sys_audio_start(u64 a0, u64, u64, u64, u64, u64) {
    u32 stream_id = static_cast<u32>(a0);

    auto *dev = virtio::sound_device();
    if (!dev || !dev->is_available()) {
        return err_not_found();
    }

    if (!dev->start(stream_id)) {
        return err_io();
    }
    return SyscallResult::ok();
}

/// @brief Stop playback on an active audio stream.
SyscallResult sys_audio_stop(u64 a0, u64, u64, u64, u64, u64) {
    u32 stream_id = static_cast<u32>(a0);

    auto *dev = virtio::sound_device();
    if (!dev || !dev->is_available()) {
        return err_not_found();
    }

    if (!dev->stop(stream_id)) {
        return err_io();
    }
    return SyscallResult::ok();
}

/// @brief Release an audio stream, freeing its device-side resources.
SyscallResult sys_audio_release(u64 a0, u64, u64, u64, u64, u64) {
    u32 stream_id = static_cast<u32>(a0);

    auto *dev = virtio::sound_device();
    if (!dev || !dev->is_available()) {
        return err_not_found();
    }

    if (!dev->release(stream_id)) {
        return err_io();
    }
    return SyscallResult::ok();
}

/// @brief Write PCM audio data to a stream.
/// @details Routes through the audio mixer when available for multi-stream
///   support, otherwise writes directly to the device. Returns bytes written.
SyscallResult sys_audio_write(u64 a0, u64 a1, u64 a2, u64, u64, u64) {
    u32 stream_id = static_cast<u32>(a0);
    const void *buf = reinterpret_cast<const void *>(a1);
    usize len = static_cast<usize>(a2);

    if (!validate_user_read(buf, len, false)) {
        return err_invalid_arg();
    }

    auto *dev = virtio::sound_device();
    if (!dev || !dev->is_available()) {
        return err_not_found();
    }

    // Use mixer for multi-stream support; direct write for single-stream
    auto *mixer = virtio::audio_mixer();
    i64 written;
    if (mixer) {
        written = mixer->submit(stream_id, buf, len);
    } else {
        written = dev->write_pcm(stream_id, buf, len);
    }
    if (written < 0) {
        return err_io();
    }
    return ok_u64(static_cast<u64>(written));
}

/// @brief Set the global audio output volume (0-255 in low 8 bits of a0).
SyscallResult sys_audio_set_volume(u64 a0, u64, u64, u64, u64, u64) {
    u8 volume = static_cast<u8>(a0 & 0xFF);

    auto *dev = virtio::sound_device();
    if (!dev) {
        return err_not_found();
    }

    dev->set_volume(volume);
    return SyscallResult::ok();
}

/// @brief Query audio device info: availability, stream count, and volume.
/// @details Returns a triple (available, num_output_streams, volume) packed
///   into the result registers. Returns all zeros if no device is present.
SyscallResult sys_audio_get_info(u64, u64, u64, u64, u64, u64) {
    // Returns packed info: available (bool), num_output_streams, current volume
    auto *dev = virtio::sound_device();
    if (!dev) {
        // No device: return available=0, streams=0, volume=0
        return SyscallResult::ok(0, 0, 0);
    }

    u64 available = dev->is_available() ? 1 : 0;
    u64 num_streams = dev->num_output_streams();
    u64 volume = dev->volume();
    return SyscallResult::ok(available, num_streams, volume);
}

} // namespace syscall
