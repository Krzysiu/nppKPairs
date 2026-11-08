# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile UTF-8 gettext PO catalogs without external dependencies."""
import ast, json, struct, sys
from pathlib import Path

def compile_po(source, target):
    entries, msgid, msgstr, field = {}, None, "", None
    def flush():
        if msgid is not None and (msgstr or msgid == ""): entries[msgid] = msgstr
    for line in Path(source).read_text(encoding="utf-8-sig").splitlines() + [""]:
        line = line.strip()
        if not line:
            flush(); msgid, msgstr, field = None, "", None
        elif line.startswith("#"): continue
        elif line.startswith("msgid "):
            flush(); msgid, msgstr, field = ast.literal_eval(line[6:]), "", "id"
        elif line.startswith("msgstr "): msgstr, field = ast.literal_eval(line[7:]), "str"
        elif line.startswith('"'):
            if field == "id": msgid += ast.literal_eval(line)
            elif field == "str": msgstr += ast.literal_eval(line)
        else: raise ValueError("Unsupported PO entry: " + line)
    keys = sorted(entries)
    originals = [k.encode("utf-8") for k in keys]
    translated = [entries[k].encode("utf-8") for k in keys]
    n = len(keys); offset = 28 + 16*n
    a, b, blob = [], [], b""
    for values, table in [(originals, a), (translated, b)]:
        for value in values:
            table.append((len(value), offset+len(blob))); blob += value+b"\0"
    data = struct.pack("<7I", 0x950412de, 0, n, 28, 28+8*n, 0, 0)
    for length, location in a+b: data += struct.pack("<2I", length, location)
    target = Path(target); target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(data+blob)

if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    if len(sys.argv) == 3: compile_po(sys.argv[1], sys.argv[2])
    else:
        for source in (root/"locale").glob("*.po"):
            compile_po(source, root/"locale"/source.stem/"LC_MESSAGES"/"KPairs.mo")
