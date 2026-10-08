import os
import re
import sys
import zipfile


PROFILE_PATH = "Libs/Config/defaultProfile.xml"
QUICKSAVE_ACTIONS = (
    '        <action consoleCmd="1" name="lw_quicksave" onRelease="1" keyboard="_keybinds_ref_" />\n'
    '        <action consoleCmd="1" name="lw_quicksave_ctrl" holdTriggerDelay="0.5" onHold="1" '
    'holdRepeatDelay="-1" retriggerable="0" xboxpad="xi_back" pspad="pad_touch"/>'
)


def patch_profile(text):
    if "foot_magnetism_activate" not in text:
        raise RuntimeError("AutoWalk actions not found in source data pak")

    has_quicksave = 'name="lw_quicksave"' in text
    has_controller_quicksave = 'name="lw_quicksave_ctrl"' in text
    if has_quicksave != has_controller_quicksave:
        raise RuntimeError("partial Unlimited Saving II actions found in source profile")

    if not has_quicksave:
        open_menu = re.search(
            r'(<actionmap name="open_menu"[^>]*>.*?<action name="open_menu"[^>]*/>)',
            text,
            re.DOTALL,
        )
        if not open_menu:
            raise RuntimeError("open_menu actionmap not found in defaultProfile.xml")
        text = text[: open_menu.end()] + "\n" + QUICKSAVE_ACTIONS + text[open_menu.end() :]

    skip_time_map = re.search(
        r'<actionmap name="open_skiptime"[^>]*>.*?</actionmap>', text, re.DOTALL
    )
    if not skip_time_map:
        raise RuntimeError("open_skiptime actionmap not found")
    skip_time_match = re.search(
        r'<action name="open_skiptime"[^>]*/>', skip_time_map.group(0)
    )
    if not skip_time_match:
        raise RuntimeError("open_skiptime action not found in its actionmap")

    skip_time = skip_time_match.group(0)
    if 'onHold="1"' not in skip_time:
        if 'onRelease="1"' not in skip_time:
            raise RuntimeError("unexpected open_skiptime action format")
        patched_skip_time = skip_time.replace(
            'onRelease="1"', 'onRelease="1" onHold="1" holdTriggerDelay="0.25"', 1
        )
        start = skip_time_map.start() + skip_time_match.start()
        end = skip_time_map.start() + skip_time_match.end()
        text = text[:start] + patched_skip_time + text[end:]
    elif 'holdTriggerDelay="0.25"' not in skip_time:
        raise RuntimeError("unexpected open_skiptime hold behavior")

    return text


def main(source_pak, output_pak):
    with zipfile.ZipFile(source_pak, "r") as source:
        entries = {entry.filename: source.read(entry) for entry in source.infolist()}

    if PROFILE_PATH not in entries:
        raise RuntimeError(f"{PROFILE_PATH} not found in AutoWalk data pak")

    profile = entries[PROFILE_PATH].decode("utf-8-sig")
    entries[PROFILE_PATH] = patch_profile(profile).encode("utf-8")

    os.makedirs(os.path.dirname(output_pak), exist_ok=True)
    with zipfile.ZipFile(output_pak, "w", zipfile.ZIP_STORED) as output:
        for name, data in entries.items():
            output.writestr(name, data)

    print(f"Wrote {output_pak} ({os.path.getsize(output_pak)} bytes)")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
