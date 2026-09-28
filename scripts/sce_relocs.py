"""Relative relocations of a PS4 PRX, for finding vtables and pointer tables.

PS4 modules keep their relocation table in the PT_SCE_DYNLIBDATA segment
(DT_SCE_RELA / DT_SCE_RELASZ in PT_DYNAMIC), which generic ELF tools do not
read. Vtables and other pointer tables are zero in the file and filled by
R_X86_64_RELATIVE entries, so reading those entries is how a function's vtable
slot is found offline.

    python scripts/sce_relocs.py <module.prx> <target va> [<target va> ...]

prints every relocated address whose value is one of the targets. The module
maps at file offset = VA + 0x4000 for this game's PRXs.
"""
import struct
import sys

PT_DYNAMIC = 2
PT_SCE_DYNLIBDATA = 0x61000000
DT_SCE_RELA = 0x6100002F
DT_SCE_RELASZ = 0x61000031
R_X86_64_RELATIVE = 8


def relative_relocations(data):
    """Return {address: target} for every R_X86_64_RELATIVE entry."""
    phoff, = struct.unpack_from('<Q', data, 0x20)
    phentsize, phnum = struct.unpack_from('<HH', data, 0x36)
    dynamic = dynlib = None
    for i in range(phnum):
        p_type, _, p_offset, _, _, p_filesz, _, _ = struct.unpack_from('<IIQQQQQQ', data, phoff + i * phentsize)
        if p_type == PT_DYNAMIC:
            dynamic = (p_offset, p_filesz)
        elif p_type == PT_SCE_DYNLIBDATA:
            dynlib = p_offset
    if dynamic is None or dynlib is None:
        raise ValueError('no PT_DYNAMIC or PT_SCE_DYNLIBDATA segment')
    rela = relasz = None
    for off in range(dynamic[0], dynamic[0] + dynamic[1], 16):
        tag, value = struct.unpack_from('<qQ', data, off)
        if tag == DT_SCE_RELA:
            rela = value
        elif tag == DT_SCE_RELASZ:
            relasz = value
    out = {}
    for off in range(dynlib + rela, dynlib + rela + relasz, 24):
        r_offset, r_info, r_addend = struct.unpack_from('<QQq', data, off)
        if r_info & 0xffffffff == R_X86_64_RELATIVE:
            out[r_offset] = r_addend
    return out


def main():
    data = open(sys.argv[1], 'rb').read()
    targets = {int(t, 16) for t in sys.argv[2:]}
    relocs = relative_relocations(data)
    print(f'{len(relocs)} relative relocations')
    for address, target in sorted(relocs.items()):
        if target in targets:
            print(f'{address:#x} -> {target:#x}')


if __name__ == '__main__':
    main()
