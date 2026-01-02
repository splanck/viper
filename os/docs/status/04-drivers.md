# Drivers Subsystem

**Status:** Functional for QEMU virt platform (kernel and user-space)
**Location:** `kernel/drivers/` + user-space servers
**SLOC:** ~6,000 (kernel) + server-side

## Overview

The drivers subsystem provides device drivers for VirtIO paravirtual devices, QEMU firmware configuration, and the RAM framebuffer.

In microkernel mode, some drivers run in user-space servers:
- **netd server**: Contains VirtIO-net driver in user-space
- **blkd server**: Contains VirtIO-blk driver in user-space
- **inputd server**: Contains VirtIO-input driver in user-space

User-space drivers use kernel device primitives:
- `SYS_MAP_DEVICE`: Map MMIO regions into user address space
- `SYS_IRQ_REGISTER`: Register for device interrupts
- `SYS_IRQ_WAIT`: Wait for interrupt notification
- `SYS_DMA_ALLOC`: Allocate DMA-capable memory
- `SYS_VIRT_TO_PHYS`: Get physical address for DMA

The kernel still contains full drivers for boot-time initialization and fallback.

---

## Components

### 1. VirtIO Core (`virtio/virtio.cpp`, `virtio.hpp`)

**Status:** Complete device discovery and base support

**Implemented:**
- VirtIO-MMIO device scanning at `0x0a000000-0x0a004000`
- Magic value (`0x74726976` = "virt") verification
- Version detection (legacy v1, modern v2)
- Device ID identification and registry
- Device claiming mechanism (prevents double-init)
- Base `Device` class with:
  - MMIO register read/write
  - Configuration space access (8/16/32/64-bit)
  - Status management (ACKNOWLEDGE, DRIVER, DRIVER_OK, FEATURES_OK)
  - Feature negotiation for legacy and modern modes
  - Interrupt status read and acknowledgment

**Supported Device Types:**
| Type ID | Name | Status |
|---------|------|--------|
| 1 | Network | Implemented |
| 2 | Block | Implemented |
| 3 | Console | Not implemented |
| 4 | RNG | Implemented |
| 16 | GPU | Implemented (2D) |
| 18 | Input | Implemented |

**MMIO Register Map:**
| Offset | Register | Description |
|--------|----------|-------------|
| 0x000 | MAGIC | Magic value ("virt") |
| 0x004 | VERSION | Device version (1 or 2) |
| 0x008 | DEVICE_ID | Device type |
| 0x010 | DEVICE_FEATURES | Device feature bits |
| 0x020 | DRIVER_FEATURES | Driver feature bits |
| 0x030 | QUEUE_SEL | Queue selector |
| 0x034 | QUEUE_NUM_MAX | Max queue size |
| 0x038 | QUEUE_NUM | Queue size |
| 0x070 | STATUS | Device status |
| 0x100 | CONFIG | Device-specific config |

**Not Implemented:**
- VirtIO-PCI transport
- Multiple device instances per type

**Recommendations:**
- Add VirtIO-GPU 3D support (virgl) for hardware acceleration
- Add VirtIO-console for console I/O

---

### 2. Virtqueue (`virtio/virtqueue.cpp`, `virtqueue.hpp`)

**Status:** Complete implementation for both legacy and modern virtio

**Implemented:**
- Vring data structures (descriptor table, avail ring, used ring)
- Legacy mode contiguous vring allocation
- Modern mode separate ring allocations
- Descriptor free list management
- Descriptor chain building
- Available ring submission
- Used ring polling
- Device notification (kick)
- Queue cleanup and destruction

**Descriptor Flags:**
| Flag | Value | Description |
|------|-------|-------------|
| NEXT | 1 | Buffer continues in `next` |
| WRITE | 2 | Device writes to buffer |
| INDIRECT | 4 | Indirect descriptor list |

**Vring Structure:**
```
┌─────────────────────────────────────┐
│       Descriptor Table              │
│  [addr, len, flags, next] × N       │
├─────────────────────────────────────┤
│       Available Ring                │
│  flags, idx, ring[N], used_event    │
├─────────────────────────────────────┤
│       Used Ring                     │
│  flags, idx, ring[N], avail_event   │
└─────────────────────────────────────┘
```

**Not Implemented:**
- Indirect descriptor support
- Event suppression (VIRTIO_F_EVENT_IDX)

---

### 3. VirtIO Block Device (`virtio/blk.cpp`, `blk.hpp`)

**Status:** Complete with interrupt-driven and async I/O support

**Implemented:**
- Device discovery and initialization
- Capacity and sector size detection
- Read-only device detection
- Sector read operations (`read_sectors`)
- Sector write operations (`write_sectors`)
- Cache flush operation (`flush`)
- Request header/status management
- Descriptor chain construction (header → data → status)
- **Interrupt-driven I/O with polling fallback**
- **Async I/O API with callbacks:**
  - `read_async(sector, count, buf, callback, user_data)`
  - `write_async(sector, count, buf, callback, user_data)`
  - `is_complete(handle)` - Check completion status
  - `get_result(handle)` - Get operation result
  - `wait_complete(handle)` - Blocking wait
  - `process_completions()` - Process callbacks
- Single global device instance

**Block Request Types:**
| Type | Value | Description |
|------|-------|-------------|
| IN | 0 | Read from device |
| OUT | 1 | Write to device |
| FLUSH | 4 | Flush write cache |

**Block Status Codes:**
| Status | Value | Description |
|--------|-------|-------------|
| OK | 0 | Success |
| IOERR | 1 | I/O error |
| UNSUPP | 2 | Unsupported operation |

**Configuration:**
- Sector size: 512 bytes
- Max pending requests: 8
- Queue size: 128 descriptors
- IRQ: Registered with GIC for completion notifications

**Not Implemented:**
- Discard/TRIM operations
- Write zeroes
- Multi-queue support
- SCSI passthrough

**Recommendations:**
- Implement request batching
- Add I/O scheduler integration

---

### 4. VirtIO Network Device (`virtio/net.cpp`, `net.hpp`)

**Status:** Complete with interrupt-driven receive and checksum offload

**Implemented:**
- Device discovery and initialization
- MAC address reading from config space
- Feature negotiation (VERSION_1 for modern, CSUM, GUEST_CSUM)
- RX virtqueue (queue 0) with buffer pool
- TX virtqueue (queue 1)
- Packet reception with internal queue
- Blocking packet transmission
- Virtio-net header handling (10 bytes)
- Statistics tracking (packets/bytes/drops)
- Link status (assumed always up)
- RX buffer refilling
- **Interrupt-driven receive via GIC IRQ**
- **RX wait queue for blocking receive**
- **IRQ handler with automatic buffer refill**
- **TX checksum offload support:**
  - `transmit_csum(data, len, csum_start, csum_offset)` - Transmit with HW checksum
  - `has_tx_csum()` - Check if TX offload available
  - `has_rx_csum()` - Check if RX validation available
  - Software fallback if hardware offload unavailable

**Network Header:**
```cpp
struct NetHeader {
    u8 flags;         // NEEDS_CSUM, DATA_VALID
    u8 gso_type;
    u16 hdr_len;
    u16 gso_size;
    u16 csum_start;   // Offset to start checksumming
    u16 csum_offset;  // Offset to store checksum
};
```

**Configuration:**
- RX buffer pool: 32 buffers × 2048 bytes
- RX packet queue: 16 entries
- TX/RX virtqueue size: 64 descriptors
- Max Ethernet frame: 1514 bytes
- IRQ: Registered with GIC for used buffer notifications

**Not Implemented:**
- GSO/TSO (segmentation offload)
- Multiqueue
- Control virtqueue
- VLAN support
- Mergeable RX buffers

**Recommendations:**
- Add multiqueue for SMP scalability
- Implement GSO for large packet handling

---

### 5. VirtIO RNG Device (`virtio/rng.cpp`, `rng.hpp`)

**Status:** Fully functional entropy source

**Implemented:**
- Device discovery and initialization
- Single virtqueue setup
- DMA buffer allocation (256 bytes)
- Random byte retrieval with polling
- Multiple request batching for large requests
- Timeout handling on device stall

**API:**
| Function | Description |
|----------|-------------|
| `init()` | Initialize RNG device |
| `is_available()` | Check if RNG is ready |
| `get_bytes(buf, len)` | Fill buffer with random bytes |

**Usage:**
Primary entropy source for:
- TLS random number generation
- TCP sequence numbers
- Protocol nonces

**Not Implemented:**
- Multiple RNG device support
- Entropy pool with kernel mixing
- Non-blocking interface with backpressure

---

### 6. VirtIO GPU Device (`virtio/gpu.cpp`, `gpu.hpp`)

**Status:** Basic 2D framebuffer support

**Implemented:**
- Device discovery and initialization
- Feature negotiation (VERSION_1 for modern)
- Control virtqueue for commands
- Cursor virtqueue (optional)
- Display info enumeration
- 2D resource creation and management
- Backing memory attachment
- Scanout configuration
- 2D transfer to host
- Resource flush
- Resource destruction

**API:**
| Function | Description |
|----------|-------------|
| `init()` | Initialize GPU device |
| `get_display_info(w, h)` | Get display dimensions |
| `create_resource_2d(id, w, h, fmt)` | Create framebuffer resource |
| `attach_backing(id, addr, size)` | Attach memory to resource |
| `set_scanout(scanout, id, w, h)` | Configure display output |
| `transfer_to_host_2d(id, x, y, w, h)` | Transfer framebuffer to host |
| `flush(id, x, y, w, h)` | Flush region to display |
| `unref_resource(id)` | Destroy resource |

**GPU Command Types:**
| Command | Value | Description |
|---------|-------|-------------|
| GET_DISPLAY_INFO | 0x0100 | Query displays |
| RESOURCE_CREATE_2D | 0x0101 | Create 2D resource |
| SET_SCANOUT | 0x0103 | Set display output |
| RESOURCE_FLUSH | 0x0104 | Flush to display |
| TRANSFER_TO_HOST_2D | 0x0105 | Transfer framebuffer |
| RESOURCE_ATTACH_BACKING | 0x0106 | Attach memory |

**Pixel Formats Supported:**
| Format | Value | Description |
|--------|-------|-------------|
| B8G8R8A8_UNORM | 1 | 32-bit BGRA |
| B8G8R8X8_UNORM | 2 | 32-bit BGRX |
| R8G8B8A8_UNORM | 67 | 32-bit RGBA |
| X8B8G8R8_UNORM | 68 | 32-bit XBGR |

**Configuration:**
- Control queue size: 64 descriptors
- Cursor queue size: 16 descriptors
- Command/response buffers: 4KB each

**Not Implemented:**
- 3D rendering (virgl)
- EDID parsing
- Cursor plane operations
- Multiple scanouts
- Fence synchronization

**Recommendations:**
- Add cursor support for mouse pointer
- Implement 3D virgl for hardware acceleration
- Add EDID parsing for display detection

---

### 7. VirtIO Input Device (`virtio/input.cpp`, `input.hpp`)

**Status:** Complete with keyboard, mouse, and LED control

**Implemented:**
- Device discovery and initialization
- Device name reading from config
- Event type capability detection (EV_KEY, EV_REL, EV_LED)
- Automatic keyboard vs. mouse classification
- Event virtqueue with buffer pool
- Non-blocking event polling
- Input event structure (Linux-compatible)
- Global keyboard and mouse pointers
- Feature negotiation (VERSION_1 for modern)
- **LED control via status queue:**
  - `set_led(led, on)` - Set LED state
  - `has_led_support()` - Check if LEDs available
  - Supports Num Lock, Caps Lock, Scroll Lock

**Input Event Structure:**
```cpp
struct InputEvent {
    u16 type;   // EV_KEY, EV_REL, EV_ABS, EV_LED, etc.
    u16 code;   // Key code, axis, or LED code
    u32 value;  // 1=press/on, 0=release/off, or delta
};
```

**Event Types Supported:**
| Type | Value | Description |
|------|-------|-------------|
| SYN | 0x00 | Synchronization |
| KEY | 0x01 | Key/button press |
| REL | 0x02 | Relative movement |
| ABS | 0x03 | Absolute position |
| LED | 0x11 | LED control |

**LED Codes:**
| Code | Value | Description |
|------|-------|-------------|
| NUML | 0x00 | Num Lock LED |
| CAPSL | 0x01 | Caps Lock LED |
| SCROLLL | 0x02 | Scroll Lock LED |

**Configuration:**
- Event buffer count: 64
- Event size: 8 bytes
- Status queue size: 8 (for LED control)

**Not Implemented:**
- Absolute positioning (touchscreen)
- Force feedback
- Multiple keyboards/mice
- Event coalescing

**Recommendations:**
- Implement touchscreen support (EV_ABS)
- Add force feedback for game controllers

---

### 8. QEMU fw_cfg (`fwcfg.cpp`, `fwcfg.hpp`)

**Status:** Complete interface for firmware configuration

**Implemented:**
- MMIO interface at `0x09020000`
- Signature verification ("QEMU" = `0x554D4551`)
- File directory interface
- File lookup by name
- Selector-based byte read/write
- DMA write interface (required for ramfb)
- Big-endian conversion helpers

**Registers:**
| Offset | Register | Description |
|--------|----------|-------------|
| 0x00 | DATA | Data read/write |
| 0x08 | SELECTOR | Item selector |
| 0x10 | DMA | DMA address (64-bit BE) |

**Well-Known Selectors:**
| Selector | Name | Description |
|----------|------|-------------|
| 0x0000 | SIGNATURE | "QEMU" magic |
| 0x0001 | ID | Feature flags |
| 0x0019 | FILE_DIR | File directory |

**DMA Control Bits:**
| Bit | Name | Description |
|-----|------|-------------|
| 0 | ERROR | DMA error occurred |
| 1 | READ | Read operation |
| 2 | SKIP | Skip bytes |
| 3 | SELECT | Include selector |
| 4 | WRITE | Write operation |

**Not Implemented:**
- DMA read interface
- ACPI table access
- SMBIOS table access
- Kernel/initrd loading

**Recommendations:**
- Add device tree reading for dynamic memory detection

---

### 9. RAM Framebuffer (`ramfb.cpp`, `ramfb.hpp`)

**Status:** Fully functional framebuffer driver

**Implemented:**
- Framebuffer configuration via fw_cfg DMA
- Fixed framebuffer address at `0x41000000`
- 32-bit XRGB8888 pixel format
- Resolution configuration (default 1024×768)
- Basic drawing primitives:
  - `put_pixel(x, y, color)`
  - `fill_rect(x, y, w, h, color)`
  - `clear(color)`
- External framebuffer initialization (for UEFI GOP)
- Framebuffer info structure

**RAMFBCfg Structure (big-endian):**
```cpp
struct RAMFBCfg {
    u64 addr;     // Framebuffer physical address
    u32 fourcc;   // Pixel format (DRM_FORMAT_XRGB8888)
    u32 flags;    // Must be 0
    u32 width;    // Width in pixels
    u32 height;   // Height in pixels
    u32 stride;   // Bytes per line
};
```

**Configuration:**
- Framebuffer base: `0x41000000`
- Maximum size: 8MB
- Bits per pixel: 32
- FourCC code: `0x34325258` ("XR24")

**Not Implemented:**
- Hardware acceleration
- Multiple display support
- Mode enumeration
- VSync synchronization
- Cursor plane

**Recommendations:**
- Add VirtIO-GPU support for acceleration
- Implement mode switching

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Kernel / User Space                       │
└────────────┬────────────────────────────────────────────────┘
             │
┌────────────┴────────────────────────────────────────────────┐
│                    VirtIO Core Layer                         │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐          │
│  │   Device    │  │  Virtqueue  │  │   Feature   │          │
│  │  Discovery  │  │  Management │  │ Negotiation │          │
│  └─────────────┘  └─────────────┘  └─────────────┘          │
└────────────┬────────────────────────────────────────────────┘
             │
     ┌───────┴───────┬───────────────┬───────────────┬───────────────┐
     ▼               ▼               ▼               ▼               ▼
┌─────────┐   ┌─────────┐   ┌─────────┐   ┌─────────┐   ┌─────────┐
│  Block  │   │   GPU   │   │   Net   │   │   RNG   │   │  Input  │
│ Driver  │   │ Driver  │   │ Driver  │   │ Driver  │   │ Driver  │
└────┬────┘   └────┬────┘   └────┬────┘   └────┬────┘   └────┬────┘
     │             │             │             │             │
     ▼             ▼             ▼             ▼             ▼
┌─────────────────────────────────────────────────────────────┐
│                  VirtIO-MMIO Transport                       │
│            (0x0a000000 - 0x0a004000, 0x200 apart)           │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                       Other Drivers                          │
│  ┌─────────────────┐         ┌─────────────────────────┐    │
│  │     fw_cfg      │◄───────►│        ramfb            │    │
│  │  (0x09020000)   │  (DMA)  │    (0x41000000 FB)      │    │
│  └─────────────────┘         └─────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

---

## Device Initialization Sequence

### VirtIO Device:
```
1. Device probing (scan MMIO range)
2. Read magic/version/device_id
3. Write STATUS = 0 (reset)
4. Add ACKNOWLEDGE status bit
5. Add DRIVER status bit
6. (Legacy) Set GUEST_PAGE_SIZE
7. Negotiate features
8. (Modern) Add FEATURES_OK, verify
9. Configure virtqueues
10. Add DRIVER_OK status bit
```

### VirtIO Operation:
```
1. Allocate descriptors
2. Fill descriptor chain (addr, len, flags)
3. Add head to available ring
4. Notify device (kick)
5. Poll used ring for completion
6. Process completed buffers
7. Free descriptors
```

---

## Testing

The drivers subsystem is tested via:
- `qemu_kernel_boot` - Verifies virtio discovery and RNG init
- `qemu_storage_tests` - Tests block device read/write
- Network tests use virtio-net for packet I/O
- Input devices tested manually via graphics console

---

## Files

| File | Lines | Description |
|------|-------|-------------|
| `virtio/virtio.cpp` | ~292 | Core device discovery |
| `virtio/virtio.hpp` | ~294 | Core definitions |
| `virtio/virtqueue.cpp` | ~390 | Vring implementation |
| `virtio/virtqueue.hpp` | ~299 | Vring structures |
| `virtio/blk.cpp` | ~745 | Block device driver |
| `virtio/blk.hpp` | ~379 | Block device interface |
| `virtio/gpu.cpp` | ~390 | GPU device driver |
| `virtio/gpu.hpp` | ~260 | GPU device interface |
| `virtio/net.cpp` | ~750 | Network device driver |
| `virtio/net.hpp` | ~395 | Network device interface |
| `virtio/rng.cpp` | ~189 | RNG device driver |
| `virtio/rng.hpp` | ~56 | RNG interface |
| `virtio/input.cpp` | ~435 | Input device driver |
| `virtio/input.hpp` | ~256 | Input device interface |
| `fwcfg.cpp` | ~259 | fw_cfg implementation |
| `fwcfg.hpp` | ~97 | fw_cfg interface |
| `ramfb.cpp` | ~234 | RAM framebuffer |
| `ramfb.hpp` | ~119 | Framebuffer interface |

---

## Priority Recommendations

1. ~~**High:** Add interrupt-driven I/O for virtio-blk~~ ✓ Implemented
2. ~~**High:** Implement async I/O API for better throughput~~ ✓ Implemented
3. ~~**Medium:** Add VirtIO-GPU for hardware-accelerated graphics~~ ✓ Implemented (2D)
4. ~~**Medium:** Add checksum offload for virtio-net~~ ✓ Implemented
5. **Medium:** Add VirtIO-GPU 3D support (virgl)
6. **Low:** Implement VirtIO-console for console I/O
7. **Low:** Add device tree parsing for dynamic configuration
8. **Low:** Add GSO/TSO for virtio-net large packet handling
9. **Low:** Add multiqueue support for SMP scalability
