from pathlib import Path
import re
from xml.etree import ElementTree

root = Path(__file__).resolve().parents[2]
app = Path(__file__).parent
source = (app / 'eden_app.cpp').read_text()
entry = (root / 'headless/main.cpp').read_text()
package = (root / 'tools/package-headless-native.sh').read_text()
rml = (app / 'ui/main.rml').read_text()
ui = app / 'ui'
sizes = {int(value) for value in re.findall(r'font-size:\s*(\d+)px',
                                          (ui / 'styles/app.rcss').read_text())}
assert sizes <= {20, 24, 28, 32, 36, 40, 48}, sizes
for image in ElementTree.fromstring(rml).iter('img'):
    path = ui / image.attrib['src']
    header = path.read_bytes()[:18]
    assert header[:3] == b'\0\0\2' and header[16] == 32 and header[17] == 0x28, path
    if image.attrib['src'].startswith('chrome/') or image.attrib['src'] in {
        'icons/continue-playing.tga', 'icons/load-rom.tga',
        'icons/settings.tga', 'icons/help.tga',
    }:
        assert int.from_bytes(header[12:14], 'little') == int(image.attrib['width']), path
        assert int.from_bytes(header[14:16], 'little') == int(image.attrib['height']), path

assert 'RADIO_INPUT_CROSS' in source and 'selected_game_ = "/app0/assets/roms/"' in source
assert 'Eden::ReadNativeDirectory("/app0/assets/roms"' in source
renderer = (app / 'frontend.cpp').read_text()
assert 'source_width != destination_width' not in renderer
assert 'SDL_RenderCopy(renderer_, texture->texture' in renderer
assert '"/download0/" + source.substr(position)' in renderer
assert 'SelectProsperoEdenGame(launch_error)' in entry and 'Selected ROM is no longer available' in entry
assert 'PPSA99008' in package and 'PROSPEROEDEN0001' in package
assert 'PROSPEROEDEN' in rml and 'rom-dialog' in rml
print('ProsperoEden ROM selection integration PASS')
