# IDAPython helper: apply names from re/seed_manifest.json.
# Safe: modifies only the IDA database, never the input binary on disk.

import json
import os

import ida_bytes
import ida_kernwin
import ida_name
import idaapi


def _manifest_path():
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(here, "seed_manifest.json")


def _ea_from_entry(entry, imagebase):
    if "va" in entry:
        return int(entry["va"], 16)
    if "rva" in entry:
        return imagebase + int(entry["rva"], 16)
    return None


def _safe_name(ea, name):
    if ea is None:
        return False
    if not ida_bytes.is_loaded(ea):
        ida_kernwin.msg("[AutoWalk seed] SKIP not loaded: 0x%X %s\n" % (ea, name))
        return False
    ok = ida_name.set_name(ea, name, ida_name.SN_FORCE)
    ida_kernwin.msg("[AutoWalk seed] %s 0x%X -> %s\n" % ("OK" if ok else "FAIL", ea, name))
    return bool(ok)


def main():
    path = _manifest_path()
    with open(path, "r", encoding="utf-8") as f:
        manifest = json.load(f)

    imagebase = idaapi.get_imagebase()
    expected = int(manifest["target"]["expected_image_base"], 16)
    if imagebase != expected:
        ida_kernwin.msg(
            "[AutoWalk seed] WARNING image base is 0x%X, expected 0x%X. "
            "Absolute VAs may not match this database.\n" % (imagebase, expected)
        )

    applied = 0
    for category in ("functions", "vtables", "rtti"):
        for entry in manifest.get(category, []):
            ea = _ea_from_entry(entry, imagebase)
            if _safe_name(ea, entry["name"]):
                applied += 1

    ida_kernwin.msg("[AutoWalk seed] Applied %d names.\n" % applied)


if __name__ == "__main__":
    main()
