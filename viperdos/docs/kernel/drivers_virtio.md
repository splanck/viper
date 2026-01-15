# Drivers: Virtio-MMIO and Devices

ViperDOS targets QEMU’s `virt` machine and leans on virtio-mmio devices to get storage, networking, input, and entropy
during bring-up.

This page explains:

- how devices are discovered
- the common virtio device helper used by drivers
- what each driver provides to higher layers (FS cache, network stack, input)

## Device discovery: scanning the MMIO window

Virtio device discovery is implemented in `kernel/drivers/virtio/virtio.cpp`.

At boot, `virtio::init()` scans the QEMU `virt` virtio-mmio range:

- base range: `0x0a000000` to `0x0a004000`
- stride: `0x200` bytes per potential device

For each candidate base address it checks:

- `MAGIC` matches the virtio-mmio magic value
- `DEVICE_ID` is non-zero

Discovered devices are stored in a small registry (`devices[]`), and drivers use `virtio::find_device(type)` to claim a
device of the desired type.

Key files:

- `kernel/drivers/virtio/virtio.hpp`
- `kernel/drivers/virtio/virtio.cpp`

## The common device helper: `virtio::Device`

Drivers share a helper that abstracts:

- MMIO register reads/writes
- device reset and status transitions
- feature negotiation (legacy vs modern)
- config space reads

This keeps drivers focused on:

- queue setup
- request/response formats
- exposing a small kernel-facing API

## Virtqueues: the shared I/O mechanism

Virtio devices communicate via virtqueues; ViperDOS implements a virtqueue helper in:

- `kernel/drivers/virtio/virtqueue.hpp`
- `kernel/drivers/virtio/virtqueue.cpp`

All the “real” device drivers build on this mechanism.

## Block device: `virtio-blk`

The block driver (`kernel/drivers/virtio/blk.*`) provides sector-based reads/writes and is used by:

- the block cache (`kernel/fs/cache.*`)
- the ViperFS filesystem driver (`kernel/fs/viperfs/*`)

The cache treats the disk as 4 KiB blocks and translates those to 512-byte sectors for the driver.

## Network device: `virtio-net`

The net driver (`kernel/drivers/virtio/net.*`) provides:

- packet transmit
- packet receive into a caller-provided buffer

The higher-level stack in `kernel/net/*` polls `net_device()->receive(...)` to drain frames and then demultiplexes them
by Ethernet type.

## Input device: `virtio-input`

The input driver (`kernel/drivers/virtio/input.*`) feeds the higher-level input subsystem in `kernel/input/*`.

The timer tick handler calls `input::poll()` periodically, which in turn pulls events from the virtio input device.

## RNG device: `virtio-rng`

The RNG driver (`kernel/drivers/virtio/rng.*`) provides entropy and is used as a building block for TLS.

## Where to go next

- For the networking stack built on virtio-net: [Networking](networking.md)
- For the filesystem stack built on virtio-blk: [Filesystems](filesystems.md)
- For input polling behavior: [Console and Logging](console.md) (input feedback loop) and `kernel/input/*`

