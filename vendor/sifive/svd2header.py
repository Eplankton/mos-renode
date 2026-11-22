#!/usr/bin/env python3
"""Generate a CMSIS-style C device header from a SiFive design.svd.

SiFive's BSPs ship the register description as SVD (and the C side only as
freedom-metal sources). This turns the SVD into what CMSIS device headers
provide for STM32: one struct per peripheral type, register arrays, field
_Pos/_Msk macros and instance pointers. Standard library only.

    python3 svd2header.py hifive1-revb/design.svd > hifive1-revb/hifive1_revb.h

Conventions
  type     sifive_uart0_0 / sifive_uart0_1  -> UART_Type (shared layout)
  instance UART0, UART1 (index only when a type has several instances)
  arrays   priority_1..52 -> PRIORITY[53] (index = number in the name);
           enable_<w>_<ctx> -> ENABLE_<ctx>[w]
  fields   UART_TXCTRL_TXEN_Pos / UART_TXCTRL_TXEN_Msk
The header carries static assertions that every member sits at its SVD
offset, so a layout mistake fails to compile.
"""

import re
import sys
import xml.etree.ElementTree as ET
from collections import OrderedDict


def text(node, tag, default=None):
    value = node.findtext(tag)
    return value.strip() if value is not None else default


def num(value):
    return int(value, 0)


def field_range(field):
    """(lsb, width) of an SVD field, in any of the three notations."""
    if field.find("bitRange") is not None:
        msb, lsb = re.match(r"\[(\d+):(\d+)\]", text(field, "bitRange")).groups()
        return int(lsb), int(msb) - int(lsb) + 1
    if field.find("lsb") is not None:
        lsb, msb = num(text(field, "lsb")), num(text(field, "msb"))
        return lsb, msb - lsb + 1
    return num(text(field, "bitOffset")), num(text(field, "bitWidth"))


def short_name(type_key):
    """sifive_fe310_g000_prci -> PRCI, sifive_uart0 -> UART."""
    last = type_key.split("_")[-1]
    return re.sub(r"\d+$", "", last).upper()


class Register:
    def __init__(self, node, default_size):
        self.name = text(node, "name")
        self.offset = num(text(node, "addressOffset"))
        self.size = num(text(node, "size", str(default_size)))
        self.desc = " ".join((text(node, "description", "") or "").split())
        self.fields = [
            (text(f, "name"), *field_range(f), " ".join((text(f, "description", "") or "").split()))
            for f in node.findall("fields/field")
        ]

    def signature(self):
        return (self.name, self.offset, self.size, tuple(f[:3] for f in self.fields))


class Member:
    """A struct member: one register or an array of same-shaped registers."""

    def __init__(self, name, offset, size, count, fields, desc):
        self.name, self.offset, self.size = name, offset, size
        self.count, self.fields, self.desc = count, fields, desc

    @property
    def end(self):
        return self.offset + self.count * self.size // 8


def build_members(registers):
    """Fold name_<i>[_<rest>] registers with a constant stride into arrays."""
    groups = OrderedDict()
    singles = []
    for reg in registers:
        m = re.match(r"^(?P<base>[A-Za-z]+(?:_[A-Za-z]+)*)_(?P<idx>\d+)(?P<rest>(?:_\d+)*)$", reg.name)
        if m:
            key = (m["base"] + m["rest"], reg.size)
            groups.setdefault(key, []).append((int(m["idx"]), reg))
        else:
            singles.append(reg)

    members = []
    for (name, size), items in groups.items():
        items.sort()
        stride = size // 8
        first_idx, first = items[0]
        base = first.offset - first_idx * stride
        consistent = all(r.offset == base + i * stride for i, r in items)
        if len(items) > 1 and consistent and base >= 0:
            count = items[-1][0] + 1
            members.append(Member(name.upper(), base, size, count, first.fields, first.desc))
        else:
            singles.extend(r for _, r in items)

    for reg in singles:
        members.append(Member(reg.name.upper(), reg.offset, reg.size, 1, reg.fields, reg.desc))

    members.sort(key=lambda m: m.offset)

    # An array that starts at a virtual index 0 must not overlap the member
    # before it; fall back to separate registers if it would.
    for prev, cur in zip(members, members[1:]):
        if cur.offset < prev.end:
            raise SystemExit(f"overlapping members {prev.name} and {cur.name}")
    return members


def emit(svd_path):
    root = ET.parse(svd_path).getroot()
    device = text(root, "name")
    default_size = num(text(root, "size", "32"))

    types = OrderedDict()  # type_key -> (registers, [(instance_index, base)])
    for p in root.iter("peripheral"):
        name = text(p, "name")
        m = re.match(r"^(.*)_(\d+)$", name)
        type_key, index = (m[1], int(m[2])) if m else (name, 0)
        regs = [Register(r, num(text(p, "size", str(default_size)))) for r in p.findall("registers/register")]
        base = num(text(p, "baseAddress"))
        if type_key in types:
            known = [r.signature() for r in types[type_key][0]]
            if known != [r.signature() for r in regs]:
                raise SystemExit(f"{name}: layout differs from other {type_key} instances")
            types[type_key][1].append((index, base))
        else:
            types[type_key] = (regs, [(index, base)])

    guard = f"_{device.upper()}_H_"
    out = []
    w = out.append
    w(f"/* Device header for {device}.")
    w(f" * Generated by vendor/sifive/svd2header.py from {svd_path.split('/')[-1]}; do not edit.")
    w(" * Source: SiFive freedom-e-sdk (Apache-2.0 / MIT), see vendor/sifive/README.md. */")
    w("")
    w(f"#ifndef {guard}")
    w(f"#define {guard}")
    w("")
    w("#include <stddef.h>")
    w("#include <stdint.h>")
    w("")
    w("#ifdef __cplusplus")
    w("#define SVD_STATIC_ASSERT(expr, msg) static_assert(expr, msg)")
    w("#else")
    w("#define SVD_STATIC_ASSERT(expr, msg) _Static_assert(expr, msg)")
    w("#endif")

    for type_key, (regs, instances) in types.items():
        short = short_name(type_key)
        tname = f"{short}_Type"
        members = build_members(regs)

        w("")
        w(f"/* {'=' * 70} */")
        w(f"/* {short}: {type_key} */")
        w(f"/* {'=' * 70} */")
        w("typedef struct")
        w("{")
        pos, pad = 0, 0
        for m in members:
            if m.offset > pos:
                w(f"    uint8_t RESERVED{pad}[0x{m.offset - pos:X}];")
                pad += 1
            ctype = "uint64_t" if m.size == 64 else "uint32_t"
            arr = f"[{m.count}]" if m.count > 1 else ""
            decl = f"volatile {ctype} {m.name}{arr};"
            w(f"    {decl:<40} /* 0x{m.offset:03X} */")
            pos = m.end
        w(f"}} {tname};")
        w("")
        for m in members:
            w(f"SVD_STATIC_ASSERT(offsetof({tname}, {m.name}) == 0x{m.offset:X}, \"{tname}.{m.name}\");")

        fields_done = False
        for m in members:
            for fname, lsb, width, _ in m.fields:
                mask = ((1 << width) - 1) << lsb
                base = f"{short}_{m.name}_{fname.upper()}"
                if not fields_done:
                    w("")
                    fields_done = True
                w(f"#define {base + '_Pos':<44} {lsb}U")
                w(f"#define {base + '_Msk':<44} 0x{mask:08X}UL")

        w("")
        many = len(instances) > 1
        for index, base in sorted(instances):
            inst = f"{short}{index}" if many else short
            w(f"#define {inst + '_BASE':<24} 0x{base:08X}UL")
            w(f"#define {inst:<24} (({tname}*) {inst}_BASE)")

    w("")
    w(f"#endif /* {guard} */")
    return "\n".join(out) + "\n"


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: svd2header.py <design.svd>")
    sys.stdout.write(emit(sys.argv[1]))
