#!/usr/bin/env python3
# ProsperoEden - The launcher's translation catalogs: template and checks.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
"""strings.py extract          write tools/launcher/launcher.pot from the text marked in the code
strings.py new <tag>          start headless/prosperoeden/ui/lang/<tag>.po from the template
strings.py check              check every catalog in headless/prosperoeden/ui/lang

The code holds the English text: tr("...") where it is drawn, TR("...") in constant tables (and
the setting labels of headless/settings_store.h). A catalog is a gettext .po file named after the
PS5 system language's tag (third_party/ps5_system_language.hpp): pt-BR.po, fr-FR.po...

check fails when a catalog
  - translates text the code no longer has, or leaves text untranslated,
  - changes the {0} {1} placeholders of a text,
  - uses a character the launcher's font does not have.
It warns when a translation is much longer than the English text (it may not fit its place).
"""

import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LAUNCHER = ROOT / "headless/prosperoeden"
CATALOGS = LAUNCHER / "ui/lang"
TEMPLATE = ROOT / "tools/launcher/launcher.pot"
FONT = LAUNCHER / "ui/fonts/montserrat-medium.pefont"
SETTINGS = ROOT / "headless/settings_store.h"
SETTING_LABELS = ("kResolutionLabels", "kUpscalingFilterLabels", "kLanguageLabels")

LITERALS = r'((?:"(?:[^"\\]|\\.)*"\s*)+)'
MARKED = re.compile(r"\b(?:tr|TR)\(\s*" + LITERALS)
ONE = re.compile(r'"((?:[^"\\]|\\.)*)"')

# What a translator cannot tell from the text alone.
NOTES = {
    "Select": "Button hint: choose the highlighted item (not the Select button).",
    "Back": "Button hint: go back one screen.",
    "Change": "Button hint: change the highlighted setting.",
    "Open": "Button hint: open the highlighted folder.",
    "Browse": "Button hint: move through the list.",
    "Page": "Button hint: jump a page of the list (L1 / R1).",
    "Details": "Button hint: show the game's details.",
    "Navigate": "Button hint: move the highlight.",
    "Choose": "Button hint: choose the highlighted language.",
    "UP": "Short tag on the 'Parent folder' row.",
    "OPEN": "Short tag on a folder row.",
    "IN USE": "Tag on the language or folder that is in use now.",
    "Docked": "Console mode: the console as if connected to a TV.",
    "Handheld": "Console mode: the console as if held in the hands.",
    "{0} OF {1}": "Position in a list: 3 OF 12.",
    "Off": "A setting that is switched off.",
    "On": "A setting that is switched on.",
    "None": "No add-ons (updates or DLC).",
    "Default ({0})": "A per-game setting that follows the general setting; {0} is its value.",
    "{0} not available": "{0} is a language name. Shown beside a game that lacks that language.",
    "Add-ons: {0}  /  Language: {1} ({2} in this game)": "{2} is the text '{0} not available'.",
    "STARTING": "Shown over a game's cover while it starts.",
    "Make it yours.": "Headline of the Settings screen.",
    "Your next adventure": "Headline when no game has been played yet.",
    "PS5 EDITION  /  {0}": "{0} is the version, for example v1.000.030.",
    "{0}/ (NSP or XCI)": "{0} is a folder; NSP and XCI are file types.",
    "Docked": "Console mode: the console as if connected to a TV. Keep it short (about 9 letters).",
    "Handheld": "Console mode: the console as if held in the hands. Keep it short (about 9 letters).",
    "Update {0}": "A game update; {0} is its version, for example 1.2.0.",
    "{0} DLC": "{0} is how many DLC (add-on content) a game has installed.",
    "{0} game": "Exactly one game. In a language with more than two plural forms, word it so that "
                "it reads well with any number (Games: {0}).",
    "{0} games": "Any number of games other than one (also 0). See '{0} game'.",
    "{0} game installed": "Exactly one game. See '{0} game'.",
    "{0} games installed": "Any number of games other than one. See '{0} game'.",
    "{0} NCA file": "Exactly one file. See '{0} game'.",
    "{0} NCA files": "Any number of files other than one. See '{0} game'.",
    "Keep keys, firmware and roms folders together. TRIANGLE uses the folder shown.":
        "keys, firmware and roms are folder names (unchanged). TRIANGLE is the controller's "
        "triangle button, in capitals.",
    "Select is the touchpad button on PS5.": "Select is a button's name (unchanged).",
    "Nearest": "An upscaling filter (nearest neighbour). Keep it short.",
    "Bilinear": "An upscaling filter.",
    "Bicubic": "An upscaling filter.",
    "Vulkan (recommended)": "Vulkan is a name (unchanged).",
    "Game could not start: {0} Details: {1}": "{0} is the reason, a sentence in English; {1} is a file.",
    "Sandboxed (code {0}): app folder only": "The app can read only its own folder; {0} is a number.",
    "Full filesystem": "The app can read every folder of the console.",
    "END GAME": "Label of the shortcut that ends the running game.",
    "FPS OVERLAY": "Label: the frames-per-second counter drawn over a game.",
    "SETUP": "Label: whether keys and firmware are in place.",
    "ACCESS": "Label: which folders the app can read.",
    "KEYS": "Label: the encryption keys file (prod.keys).",
    "ADD-ONS": "Label: a game's updates and DLC.",
    "Add-ons: {0}  /  Language: {1}": "{0}: updates and DLC of the game; {1}: the language it will use.",
    "Ryujinx save": "A save file of the Ryujinx emulator. Ryujinx is a name (unchanged).",
    "Last game opened": "Caption under the title of the game played last.",
    "Powered by Eden": "Eden is the emulator's name (unchanged).",
    "THANKS": "Heading of the acknowledgements.",
    "SELECTED": "Heading: the language highlighted in the list.",
    "NEXT START": "Label: the folder used the next time the app starts.",
    "Next launch: {0}": "{0} is the folder used the next time the app starts.",
}


def unescape(text):
    return re.sub(r"\\(.)", lambda m: {"n": "\n", "t": "\t"}.get(m.group(1), m.group(1)), text)


def escape(text):
    return text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


def joined(literals):
    return "".join(unescape(part) for part in ONE.findall(literals))


def marked_text():
    """English text -> the files that use it."""
    found = {}
    sources = sorted(p for pattern in ("*.cpp", "*.hpp", "*.h") for p in LAUNCHER.rglob(pattern))
    for path in sources:
        source = path.read_text(encoding="utf-8")
        # Comments may quote tr("...") too; drop them.
        source = re.sub(r"//[^\n]*", "", source)
        for match in MARKED.finditer(source):
            text = joined(match.group(1))
            if text:
                found.setdefault(text, []).append(path.relative_to(LAUNCHER).as_posix())
    settings = SETTINGS.read_text(encoding="utf-8")
    for name in SETTING_LABELS:
        table = re.search(name + r"\[\] = \{([^}]*)\}", settings)
        assert table, name
        for text in ONE.findall(table.group(1)):
            found.setdefault(unescape(text), []).append("settings_store.h")
    return found


def parse_po(path):
    """msgid -> msgstr of a catalog (the header entry is skipped)."""
    entries, key, value, part = {}, None, None, None
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line.startswith("msgid "):
            if key:
                entries[key] = value or ""
            key, value, part = joined(line[6:]), "", "id"
        elif line.startswith("msgstr "):
            value, part = joined(line[7:]), "str"
        elif line.startswith('"'):
            if part == "id":
                key += joined(line)
            elif part == "str":
                value += joined(line)
    if key:
        entries[key] = value or ""
    return entries


def write_catalog(path, texts, translations, language):
    lines = [f"# ProsperoEden launcher - {language}",
             "# English text is the key (msgid); msgstr is the translation. Keep {0} {1} as they are,",
             "# keep UPPERCASE labels uppercase, and keep names (ProsperoEden, Eden, PS5, Vulkan, OpenGL,",
             "# AMD FSR, DLC, NSP, XCI, prod.keys, Ryujinx) unchanged.",
             ""]
    for text in sorted(texts, key=str.lower):
        if text in NOTES:
            lines.append(f"#. {NOTES[text]}")
        lines.append("#: " + ", ".join(sorted(set(texts[text]))))
        lines.append(f'msgid "{escape(text)}"')
        lines.append(f'msgstr "{escape(translations.get(text, ""))}"')
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def font_characters():
    data = FONT.read_bytes()
    magic, _version, _w, _h, _size, _range, _asc, _desc, _gap, glyphs, _kerns = struct.unpack_from("<IIHHfffffII", data)
    assert magic == 0x46505A50, "not a .pefont file"
    return {struct.unpack_from("<I", data, 40 + 24 * index)[0] for index in range(glyphs)}


def check():
    texts = marked_text()
    characters = font_characters()
    failed = False
    catalogs = sorted(CATALOGS.glob("*.po"))
    if not catalogs:
        print("no catalogs in", CATALOGS)
    for path in catalogs:
        entries = parse_po(path)
        problems, warnings = [], []
        for text in texts:
            if not entries.get(text):
                problems.append(f"untranslated: {text!r}")
        for text, translation in entries.items():
            if text not in texts:
                problems.append(f"not in the code any more: {text!r}")
                continue
            if sorted(re.findall(r"\{\d\}", text)) != sorted(re.findall(r"\{\d\}", translation)) and translation:
                problems.append(f"placeholders differ: {text!r} -> {translation!r}")
            missing = sorted({c for c in translation if ord(c) not in characters and c not in "\n"})
            if missing:
                problems.append(f"characters the font lacks {''.join(missing)!r} in {translation!r}")
            if text.isupper() and translation and translation != translation.upper():
                warnings.append(f"label not uppercase: {text!r} -> {translation!r}")
            if translation and len(translation) > max(len(text) * 1.7, len(text) + 12):
                warnings.append(f"long ({len(text)} -> {len(translation)}): {translation!r}")
        print(f"{path.name}: {len(entries)} texts, {len(problems)} problems, {len(warnings)} warnings")
        for line in problems[:40]:
            print("   ", line)
        for line in warnings[:12]:
            print("    warning:", line)
        failed |= bool(problems)
    print(f"{len(texts)} texts in the code, {len(catalogs)} catalogs" + (" FAIL" if failed else " PASS"))
    return 1 if failed else 0


def main():
    command = sys.argv[1] if len(sys.argv) > 1 else ""
    if command == "extract":
        texts = marked_text()
        write_catalog(TEMPLATE, texts, {}, "template")
        print(f"{TEMPLATE.relative_to(ROOT)}: {len(texts)} texts, {sum(len(t.split()) for t in texts)} words")
    elif command == "new" and len(sys.argv) == 3:
        path = CATALOGS / f"{sys.argv[2]}.po"
        existing = parse_po(path) if path.exists() else {}
        write_catalog(path, marked_text(), existing, sys.argv[2])
        print(f"{path.relative_to(ROOT)}: {len(existing)} translations kept")
    elif command == "check":
        sys.exit(check())
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
