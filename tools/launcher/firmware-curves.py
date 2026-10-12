#!/usr/bin/env python3
"""Reproduce stock_motion.hpp using the selectively extracted keyframe receipt."""
from pathlib import Path
import json
root=Path(__file__).resolve().parents[2]
rows=json.loads((root/'assets/firmware-animation-keys.json').read_text())
path=root/'headless/prosperoeden/pe/ui/stock_motion.hpp'
prefix=path.read_text().split('inline constexpr Keyframe k_')[0]
def num(v):return f'{float(v):.10g}'+('.0f' if float(v).is_integer() else 'f')
for row in rows:
 name=row['function'];prefix+=f'inline constexpr Keyframe k_{name}[] = {{\n'+''.join('    {'+', '.join(num(v) for v in key)+'},\n' for key in row['keys'])+'};\n'
 frame="loop_frame(frame, %s.0f)" % row['duration_frames'] if row.get('loop') else 'frame'
 prefix+=f'inline float {name}(float frame) {{ return sample(k_{name},{frame}); }}\n'
data=(prefix+'} // namespace pe::ui::stock_motion\n').encode('utf-8')
if path.read_bytes()!=data: path.write_bytes(data)
