"""Compute the serialized size of a Respawn persistence definition.

Layout rules: bool and enum values are 1 byte, int and float 4, string{N} N
bytes, structs are the sum of their fields, arrays multiply by their count
(an enum-sized array uses the enum's member count). Checked against PC's
231 definition, whose size NorthstarLauncher hard-codes as
PERSISTENCE_MAX_SIZE = 56781.

Usage: python scripts/pdef/pdef_size.py <file.pdef> [...]
"""
import sys


def parse(path):
    top, structs, enums = [], {}, {}
    cur_s = cur_e = None
    for raw in open(path, encoding="utf-8", errors="replace"):
        s = raw.split("//")[0].strip()
        if not s:
            continue
        if s.startswith("$STRUCT_START"):
            cur_s = s.split()[1]
            structs[cur_s] = []
            continue
        if s.startswith("$STRUCT_END"):
            cur_s = None
            continue
        if s.startswith("$ENUM_START"):
            cur_e = s.split()[1]
            enums[cur_e] = 0
            continue
        if s.startswith("$ENUM_END"):
            cur_e = None
            continue
        if cur_e:
            enums[cur_e] += 1
            continue
        parts = s.split()
        if len(parts) < 2:
            continue
        # Rejoined so "int x[ someEnum ]" reads the same as "int x[someEnum]".
        (structs[cur_s] if cur_s else top).append((parts[0], "".join(parts[1:])))
    return top, structs, enums


def size_of(path):
    top, structs, enums = parse(path)
    cache = {}

    def type_size(t):
        if t in ("int", "float"):
            return 4
        if t == "bool":
            return 1
        if t.startswith("string{"):
            return int(t[7:-1])
        if t in enums:
            return 1
        if t in structs:
            if t not in cache:
                cache[t] = sum(field_size(ft, fn) for ft, fn in structs[t])
            return cache[t]
        raise KeyError("unknown type " + t)

    def field_size(t, name):
        n = 1
        if "[" in name:
            dim = name.split("[", 1)[1].rstrip("]")
            n = int(dim) if dim.isdigit() else enums[dim]
        return type_size(t) * n

    return sum(field_size(t, n) for t, n in top)


if __name__ == "__main__":
    for p in sys.argv[1:]:
        print("%8d  %s" % (size_of(p), p))
