#!/usr/bin/env python3
"""Fix PE headers for UEFI executable after objcopy conversion."""
import struct
import sys


def fix_pe_headers(input_file, output_file):
    with open(input_file, 'rb') as f:
        data = bytearray(f.read())

    # Find PE signature offset (at 0x3C in DOS header)
    pe_offset = struct.unpack_from('<I', data, 0x3C)[0]

    # Verify PE signature
    if data[pe_offset:pe_offset + 4] != b'PE\x00\x00':
        print(f"Error: No PE signature at offset {pe_offset}")
        sys.exit(1)

    # PE file header starts at pe_offset + 4
    coff_offset = pe_offset + 4

    # Read number of sections
    num_sections = struct.unpack_from('<H', data, coff_offset + 2)[0]

    # Optional header starts after COFF header (20 bytes)
    opt_offset = coff_offset + 20

    # Check magic - should be 0x20b for PE32+
    magic = struct.unpack_from('<H', data, opt_offset)[0]
    if magic != 0x020b:
        # Write PE32+ magic
        struct.pack_into('<H', data, opt_offset, 0x020b)

    # Calculate sizes from sections
    # Sections start after optional header (size at COFF header + 16)
    opt_header_size = struct.unpack_from('<H', data, coff_offset + 16)[0]
    sections_offset = opt_offset + opt_header_size

    # Find entry point (should be at start of .text section)
    entry_point = 0
    text_size = 0
    data_size = 0
    bss_size = 0
    max_rva = 0

    for i in range(num_sections):
        sec_offset = sections_offset + i * 40
        name = data[sec_offset:sec_offset + 8].rstrip(b'\x00').decode('ascii', errors='ignore')
        virtual_size = struct.unpack_from('<I', data, sec_offset + 8)[0]
        virtual_addr = struct.unpack_from('<I', data, sec_offset + 12)[0]
        raw_size = struct.unpack_from('<I', data, sec_offset + 16)[0]

        if name == '.text':
            entry_point = virtual_addr  # Entry at start of .text
            text_size = raw_size
        elif name in ('.rodata', '.data'):
            data_size += raw_size
        elif name == '.bss':
            bss_size = virtual_size

        max_rva = max(max_rva, virtual_addr + virtual_size)

    # Align to 4KB page
    image_size = (max_rva + 0xFFF) & ~0xFFF

    # Section alignment
    section_alignment = 0x1000  # 4KB
    file_alignment = 0x200  # 512 bytes

    # Fix PE32+ optional header fields
    # Offset from opt_offset:
    # +0: Magic (2 bytes) - already set
    # +2: MajorLinkerVersion (1)
    # +3: MinorLinkerVersion (1)
    # +4: SizeOfCode (4)
    # +8: SizeOfInitializedData (4)
    # +12: SizeOfUninitializedData (4)
    # +16: AddressOfEntryPoint (4)
    # +20: BaseOfCode (4)
    # +24: ImageBase (8) - PE32+ only
    # +32: SectionAlignment (4)
    # +36: FileAlignment (4)
    # +40: MajorOSVersion (2)
    # +42: MinorOSVersion (2)
    # +44: MajorImageVersion (2)
    # +46: MinorImageVersion (2)
    # +48: MajorSubsystemVersion (2)
    # +50: MinorSubsystemVersion (2)
    # +52: Win32VersionValue (4)
    # +56: SizeOfImage (4)
    # +60: SizeOfHeaders (4)
    # +64: CheckSum (4)
    # +68: Subsystem (2) - 10 = EFI Application
    # +70: DllCharacteristics (2)
    # +72: SizeOfStackReserve (8)
    # +80: SizeOfStackCommit (8)
    # +88: SizeOfHeapReserve (8)
    # +96: SizeOfHeapCommit (8)
    # +104: LoaderFlags (4)
    # +108: NumberOfRvaAndSizes (4)

    # Calculate header size (aligned)
    header_size = sections_offset + num_sections * 40
    header_size = (header_size + file_alignment - 1) & ~(file_alignment - 1)

    struct.pack_into('<I', data, opt_offset + 4, text_size)  # SizeOfCode
    struct.pack_into('<I', data, opt_offset + 8, data_size)  # SizeOfInitializedData
    struct.pack_into('<I', data, opt_offset + 12, bss_size)  # SizeOfUninitializedData
    struct.pack_into('<I', data, opt_offset + 16, entry_point)  # AddressOfEntryPoint
    struct.pack_into('<I', data, opt_offset + 20, 0)  # BaseOfCode
    struct.pack_into('<Q', data, opt_offset + 24, 0)  # ImageBase (relocatable)
    struct.pack_into('<I', data, opt_offset + 32, section_alignment)  # SectionAlignment
    struct.pack_into('<I', data, opt_offset + 36, file_alignment)  # FileAlignment
    struct.pack_into('<H', data, opt_offset + 40, 0)  # MajorOSVersion
    struct.pack_into('<H', data, opt_offset + 42, 0)  # MinorOSVersion
    struct.pack_into('<H', data, opt_offset + 44, 0)  # MajorImageVersion
    struct.pack_into('<H', data, opt_offset + 46, 0)  # MinorImageVersion
    struct.pack_into('<H', data, opt_offset + 48, 0)  # MajorSubsystemVersion
    struct.pack_into('<H', data, opt_offset + 50, 0)  # MinorSubsystemVersion
    struct.pack_into('<I', data, opt_offset + 52, 0)  # Win32VersionValue
    struct.pack_into('<I', data, opt_offset + 56, image_size)  # SizeOfImage
    struct.pack_into('<I', data, opt_offset + 60, header_size)  # SizeOfHeaders
    struct.pack_into('<I', data, opt_offset + 64, 0)  # CheckSum
    struct.pack_into('<H', data, opt_offset + 68, 10)  # Subsystem = EFI_APPLICATION
    struct.pack_into('<H', data, opt_offset + 70, 0)  # DllCharacteristics
    struct.pack_into('<Q', data, opt_offset + 72, 0)  # SizeOfStackReserve
    struct.pack_into('<Q', data, opt_offset + 80, 0)  # SizeOfStackCommit
    struct.pack_into('<Q', data, opt_offset + 88, 0)  # SizeOfHeapReserve
    struct.pack_into('<Q', data, opt_offset + 96, 0)  # SizeOfHeapCommit
    struct.pack_into('<I', data, opt_offset + 104, 0)  # LoaderFlags
    struct.pack_into('<I', data, opt_offset + 108, 16)  # NumberOfRvaAndSizes

    with open(output_file, 'wb') as f:
        f.write(data)

    print(f"Fixed PE headers:")
    print(f"  Entry point: 0x{entry_point:X}")
    print(f"  Image size: 0x{image_size:X}")
    print(f"  Header size: 0x{header_size:X}")
    print(f"  Sections: {num_sections}")
    print(f"  Subsystem: EFI_APPLICATION (10)")


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input.efi> <output.efi>")
        sys.exit(1)
    fix_pe_headers(sys.argv[1], sys.argv[2])
