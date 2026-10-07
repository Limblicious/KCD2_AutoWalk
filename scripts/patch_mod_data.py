import re
import sys
import zipfile
import os

# Patches the vanilla defaultProfile.xml / defaultActionHelp.xml with the
# foot-magnetism actions, mirroring the vanilla mounted follow definitions
# (same bindings, hold semantics, and localization keys).

PROFILE_ACTIONS = (
    '		<action name="foot_magnetism_activate" onPress="1" onRelease="1" onHold="1" '
    'holdTriggerDelay="0.25" holdRepeatDelay="-1" keyboard="e" xboxpad="xi_a" pspad="pad_cross" />\n'
    '		<action name="foot_magnetism_deactivate" onPress="1" onRelease="1" onHold="1" '
    'holdTriggerDelay="0.25" holdRepeatDelay="-1" keyboard="e" xboxpad="xi_a" pspad="pad_cross" />\n'
)

HELP_ROWS = (
    '		<hint text="ui_hud_magnetism_activate" priority="1" action="foot_magnetism_activate" '
    'visible="false" type="Hold" order="500" disable_reason="ui_magnetism_not_on_path"/>\n'
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


def main(profile_in, help_in, pak_out):
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


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3])
