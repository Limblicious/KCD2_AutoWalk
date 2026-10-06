# IDAPython helper: dump known vtable slot targets from re/seed_manifest.json.
# Safe/read-only with respect to the input binary and IDB names.

import json
import os

import ida_bytes
import ida_funcs
import ida_name
import ida_kernwin


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    manifest_path = os.path.join(here, "seed_manifest.json")
    output_path = os.path.join(os.path.dirname(here), ".re", "vtable_targets.json")

    with open(manifest_path, "r", encoding="utf-8") as f:
        manifest = json.load(f)

    result = {}
    for vt in manifest.get("vtables", []):
        base = int(vt["va"], 16)
        slots = int(vt.get("slots", 0))
        contract = vt.get("slot_contract", [])
        rows = []
        for i in range(slots):
            slot_ea = base + i * 8
            target = ida_bytes.get_qword(slot_ea)
            fn = ida_funcs.get_func(target)
            rows.append({
                "slot": i,
                "contract": contract[i] if i < len(contract) else None,
                "slot_ea": "0x%X" % slot_ea,
                "target": "0x%X" % target,
                "target_name": ida_name.get_name(target),
                "is_function": fn is not None
            })
        result[vt["name"]] = rows

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)

    ida_kernwin.msg("[AutoWalk] wrote %s\n" % output_path)


if __name__ == "__main__":
    main()
