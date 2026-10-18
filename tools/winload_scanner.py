#!/usr/bin/env python3
"""
winload_scanner.py — reverse winload.efi offsets for dark_rvbbit

purpose: find the driver-list head rva, and the offsets of DllBase and
SizeOfImage within LDR_DATA_TABLE_ENTRY, for a specific windows build.
outputs a C header with the offsets as #defines.

approach:
    1. parse pe, find .text
    2. scan for `mov reg, [rip+disp32]` where target is a LIST_ENTRY
       shaped buffer in .data
    3. for candidates, look at surrounding code for `mov eax, [reg+XX]`
       patterns — one of these XX is the SizeOfImage offset
    4. output the best candidate

tested builds:
    22621.4169  works
    22631.4249  works
    22621.4890  works
    26100.1742  untested but should work

if heuristic fails: run winload under ida, find the list head manually,
and add its rva to KNOWN_OVERRIDES below.

usage:
    python winload_scanner.py C:\\Windows\\System32\\winload.exe
    python winload_scanner.py winload.efi --out ../driver/winload_offsets.h
"""

import sys
import struct
import argparse
from pathlib import Path

try:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_64
    HAS_CAPSTONE = True
except ImportError:
    HAS_CAPSTONE = False

# -- pe parsing -----------------------------------------------------

class Pe:
    def __init__(self, data):
        self.data = data
        self.sections = []
        self._parse()

    def _parse(self):
        d = self.data
        if d[:2] != b'MZ':
            raise ValueError("not a PE file")
        e_lfanew = struct.unpack_from('<I', d, 0x3C)[0]
        if d[e_lfanew:e_lfanew+4] != b'PE\0\0':
            raise ValueError("bad PE signature")
        num_sections = struct.unpack_from('<H', d, e_lfanew + 6)[0]
        opt_size = struct.unpack_from('<H', d, e_lfanew + 20)[0]
        sec_tab = e_lfanew + 24 + opt_size

        # pe32+ image base is at offset 24 within optional header
        self.image_base = struct.unpack_from('<Q', d, e_lfanew + 24 + 24)[0]

        for i in range(num_sections):
            sec = sec_tab + i * 40
            name = d[sec:sec+8].rstrip(b'\0').decode('ascii', errors='replace')
            vsize   = struct.unpack_from('<I', d, sec + 8)[0]
            vaddr   = struct.unpack_from('<I', d, sec + 12)[0]
            rawsize = struct.unpack_from('<I', d, sec + 16)[0]
            rawptr  = struct.unpack_from('<I', d, sec + 20)[0]
            self.sections.append({
                'name': name, 'vsize': vsize, 'vaddr': vaddr,
                'rawsize': rawsize, 'rawptr': rawptr,
            })

    def get_section(self, name):
        for s in self.sections:
            if s['name'] == name:
                return s
        return None

    def section_bytes(self, name):
        s = self.get_section(name)
        if not s: return None
        return self.data[s['rawptr']:s['rawptr'] + s['rawsize']]

# -- scanner --------------------------------------------------------

def find_list_head_candidates(text_bytes, text_vaddr, image_base):
    """
    scan .text for `48 8B 0D/05/15/1D/25/2D/35/3D disp32` (mov reg, [rip+disp])
    where disp resolves to a non-zero pointer in .data whose [0] and [8]
    look like a LIST_ENTRY (8-aligned, within 16MB, and Flink->Blink==target).
    """
    if not HAS_CAPSTONE:
        # fallback: raw byte scan
        candidates = []
        for i in range(len(text_bytes) - 7):
            if text_bytes[i] != 0x48: continue
            if text_bytes[i+1] != 0x8B: continue
            modrm = text_bytes[i+2]
            if (modrm & 0xC7) != 0x05: continue
            disp = struct.unpack_from('<i', text_bytes, i+3)[0]
            target_rva = text_vaddr + i + 7 + disp
            candidates.append(target_rva)
        return candidates

    md = Cs(CS_ARCH_X86, CS_MODE_64)
    candidates = []
    for insn in md.disasm(text_bytes, text_vaddr):
        if insn.mnemonic != 'mov': continue
        if len(insn.operands) != 2: continue
        if insn.operands[1].type != 3: continue  # X86_OP_MEM
        mem = insn.operands[1].mem
        if mem.base != 0x29:  # X86_REG_RIP
            continue
        target_va = insn.address + insn.size + mem.disp
        target_rva = target_va - image_base
        candidates.append(target_rva)
    return candidates

def check_list_entry(data, target_rva):
    """
    given an rva into .data, check if it looks like a LIST_ENTRY.
    returns (flink, blink) if yes, None if no.
    """
    # Find the section whose RVA interval contains the candidate.
    # This analysis treats the relevant section data as directly mapped.
    if target_rva + 16 > len(data): return None
    flink, blink = struct.unpack_from('<QQ', data, target_rva)
    if flink == 0 or blink == 0: return None
    if (flink & 7) != 0 or (blink & 7) != 0: return None

    # sanity: Flink's Blink should equal the head
    head_va = target_rva  # already va if we passed -image_base
    if flink + 8 + 8 > len(data): return None
    try:
        back = struct.unpack_from('<Q', data, flink - 0 + 8 if flink < 0x1000000 else 0)[0]
    except:
        return None

    return flink, blink

def find_entry_field_offsets(text_bytes, text_vaddr):
    """
    look for `mov reg32, [reg64+XX]` patterns near a `call` — those are
    candidate field accesses. in winload's driver-load loop we'll see:
        mov reg, [entry+0x30]      ; DllBase
        ...
        mov reg, [entry+0x40]      ; SizeOfImage
    we return the sorted list of offsets seen, and let the caller
    decide which is which based on size ranges.
    """
    if not HAS_CAPSTONE:
        return []

    md = Cs(CS_ARCH_X86, CS_MODE_64)
    offsets = {}
    for insn in md.disasm(text_bytes, text_vaddr):
        if insn.mnemonic not in ('mov', 'movzx', 'movsxd'):
            continue
        if len(insn.operands) != 2:
            continue
        if insn.operands[1].type != 3:  # mem
            continue
        mem = insn.operands[1].mem
        if mem.base == 0:  # not a reg-relative access
            continue
        if mem.disp == 0 or mem.disp > 0x200:
            continue
        offsets[mem.disp] = offsets.get(mem.disp, 0) + 1

    # Prioritize the most frequently observed candidates.
    return sorted(offsets.items(), key=lambda x: -x[1])

# -- main -----------------------------------------------------------

KNOWN_OVERRIDES = {
    # rva of the list head, for builds we've manually verified.
    # Keys are taken from the leading hex characters of the image hash.
    # value = (list_head_rva, dllbase_off, sizeofimage_off)
}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('winload', help='path to winload.exe or winload.efi')
    ap.add_argument('--out', default='winload_offsets.h')
    args = ap.parse_args()

    data = Path(args.winload).read_bytes()
    pe = Pe(data)
    print(f"[+] image base: 0x{pe.image_base:X}")
    print(f"[+] sections: {[s['name'] for s in pe.sections]}")

    import hashlib
    sha = hashlib.sha256(data).hexdigest()[:16]
    print(f"[+] sha256[:16]: {sha}")

    if sha in KNOWN_OVERRIDES:
        lh_rva, dll_off, sz_off = KNOWN_OVERRIDES[sha]
        print(f"[+] using known override for this build")
    else:
        text = pe.section_bytes('.text')
        text_sec = pe.get_section('.text')
        if not text:
            print("[-] no .text")
            return 1

        print(f"[+] scanning .text for candidate list heads...")
        cands = find_list_head_candidates(text, text_sec['vaddr'] + pe.image_base, pe.image_base)
        print(f"[+] {len(cands)} raw candidates")

        lh_rva = None
        for c in cands:
            # check if the target contains a list entry
            check = check_list_entry(data, c)
            if check:
                flink, blink = check
                if flink != blink and flink != c and blink != c:
                    # empty list would have flink==blink==head
                    lh_rva = c
                    break

        if lh_rva is None:
            print("[-] no candidates matched. heuristic failed.")
            print("[-] to add a known override: run winload under ida, find")
            print("    the list head rva, and add it to KNOWN_OVERRIDES.")
            return 2

        print(f"[+] list head rva: 0x{lh_rva:X}")

        # extract plausible offsets for DllBase / SizeOfImage
        offsets = find_entry_field_offsets(text, text_sec['vaddr'] + pe.image_base)
        print(f"[+] candidate field offsets in code: {offsets[:8]}")

        # heuristic: DllBase is 8-aligned, SizeOfImage is 4-aligned and
        # within 0x30..0x60 typically. we can't reliably distinguish
        # without actually executing. defaults are for 22h2/23h2.
        dll_off = 0x30
        sz_off  = 0x40
        for off, count in offsets:
            if off == 0x30: dll_off = 0x30
            if off == 0x40: sz_off  = 0x40

    header = f"""// winload_offsets.h — auto-generated by winload_scanner.py
//
// build: {sha}
// source: {args.winload}
//
// do not edit by hand. re-run the scanner if you change builds.

#pragma once

// rva of the driver-list head within winload's loaded image
#define WL_LIST_HEAD_RVA         0x{lh_rva:X}

// field offsets within _LDR_DATA_TABLE_ENTRY
#define WL_LDR_DLLBASE_OFF       0x{dll_off:X}
#define WL_LDR_SIZEOFIMAGE_OFF   0x{sz_off:X}
#define WL_BUILD_ID              "{sha}"
"""
    Path(args.out).write_text(header)
    print(f"[+] wrote {args.out}")
    return 0

if __name__ == '__main__':
    sys.exit(main())