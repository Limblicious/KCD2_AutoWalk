import re
import sys
import zipfile
import os

# Patches the vanilla defaultProfile.xml / defaultActionHelp.xml with the
# foot-magnetism actions, mirroring the vanilla mounted follow definitions
# (same bindings/hold semantics) plus an AutoWalk-specific disable-reason key.

PROFILE_ACTIONS = (
    '		<action name="foot_magnetism_activate" onPress="1" onRelease="1" onHold="1" '
    'holdTriggerDelay="0.25" holdRepeatDelay="-1" keyboard="e" xboxpad="xi_a" pspad="pad_cross" />\n'
    '		<action name="foot_magnetism_deactivate" onPress="1" onRelease="1" onHold="1" '
    'holdTriggerDelay="0.25" holdRepeatDelay="-1" keyboard="e" xboxpad="xi_a" pspad="pad_cross" />\n'
)

AUTOWALK_NOT_ON_PATH_KEY = "ui_autowalk_henry_not_on_path"

HELP_ROWS = (
    '		<hint text="ui_hud_magnetism_activate" priority="1" action="foot_magnetism_activate" '
    f'visible="false" type="Hold" order="500" disable_reason="{AUTOWALK_NOT_ON_PATH_KEY}"/>\n'
    '		<hint text="ui_hud_magnetism_deactivate" priority="1" action="foot_magnetism_deactivate" '
    'visible="false" type="Hold" order="501"/>\n'
)


def patch_profile(text):
    m = re.search(r'<actionmap name="player" priority="default" exclusivity="0">', text)
    if not m:
        raise RuntimeError("player actionmap not found in defaultProfile.xml")
    insert_at = text.find('>', m.start()) + 1
    if 'foot_magnetism_activate' in text:
        return text
    return text[:insert_at] + '\n' + PROFILE_ACTIONS + text[insert_at:]


def patch_help(text):
    if 'foot_magnetism_activate' in text:
        return text
    m = re.search(r'<action_help_set actionmap="player">', text)
    if m:
        insert_at = text.find('>', m.start()) + 1
        return text[:insert_at] + '\n' + HELP_ROWS + text[insert_at:]
    # no player set yet: append a new one before </action_help>
    anchor = text.rfind('</action_help>')
    if anchor == -1:
        raise RuntimeError("</action_help> not found in defaultActionHelp.xml")
    block = ('\t<action_help_set actionmap="player">\n' + HELP_ROWS +
             '\t</action_help_set>\n')
    return text[:anchor] + block + text[anchor:]


def write_english_localization(loc_out):
    xml = (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<Table>\n'
        f'  <Row><Cell>{AUTOWALK_NOT_ON_PATH_KEY}</Cell>'
        '<Cell>Horse is not on suitable road</Cell>'
        '<Cell>Henry is not on suitable road</Cell></Row>\n'
        '</Table>\n'
    )
    os.makedirs(os.path.dirname(loc_out), exist_ok=True)
    with zipfile.ZipFile(loc_out, 'w', zipfile.ZIP_STORED) as z:
        # KCD2 generic localization resources are loaded by the
        # <anything>_<modid>.xml suffix contract.
        z.writestr('text_ui_kcd_autowalk.xml', xml)
    print(f"Wrote {loc_out} ({os.path.getsize(loc_out)} bytes)")


def main(profile_in, help_in, pak_out, loc_out):
    with open(profile_in, encoding='utf-8', errors='replace') as f:
        prof = f.read()
    with open(help_in, encoding='utf-8', errors='replace') as f:
        help_ = f.read()

    prof = patch_profile(prof)
    help_ = patch_help(help_)

    os.makedirs(os.path.dirname(pak_out), exist_ok=True)
    with zipfile.ZipFile(pak_out, 'w', zipfile.ZIP_STORED) as z:
        z.writestr('Libs/Config/defaultProfile.xml', prof)
        z.writestr('Libs/Config/defaultActionHelp.xml', help_)
    print(f"Wrote {pak_out} ({os.path.getsize(pak_out)} bytes)")
    write_english_localization(loc_out)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4])
