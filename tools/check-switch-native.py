#!/usr/bin/env python3
"""SwitchPS5's fixed native launcher contract; runtime checks are in the preview harness."""
from pathlib import Path
import json
root=Path(__file__).resolve().parents[1]
ui=root/'headless/prosperoeden/pe/ui'
cmake=(root/'headless/CMakeLists.txt').read_text()
for token in ('launcher_lua_sources','pe/ui/runtime_launcher.cpp','pe/ui/launcher_updates.cpp','pe/ui/theme_kit.cpp'):
    assert token not in cmake, token
front=(root/'headless/prosperoeden/frontend.cpp').read_text()
assert 'RuntimeLauncher' not in front and 'UpdateNotice::Start()' not in front
settings=(ui/'settings.cpp').read_text()
assert '"Themes"' not in settings and '"Launcher"' not in settings
for field in ('frame_gen_method','post_processing','compact_memory','speed_limit','memory_layout'):
    assert '&Preferences::'+field in settings,field
assert 'kHomeGames = 12;' in (ui/'launcher.hpp').read_text()
assert (root/'headless/prosperoeden/ui/art/home-blank.tga').is_file()
param=json.loads((root/'sce_sys/param.json').read_text())
assert param['titleId']=='PPSA99009' and param['contentVersion']=='01.005.000'
assert param['localizedParameters']['en-US']['titleName']=='Switch'
print('SwitchPS5: native launcher, current settings, assets, identity and manual updates PASS')
