from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'headless/graphics.cpp').read_text()
body=s.split('        if (sample_start < 0) {',1)[1].split('\n#endif\n        clock.Present(now);',1)[0]
code='''#include <algorithm>
#include <cstdio>
struct Sample {double sample_start{-1},prior_sample_frame{},sample_worst{};unsigned sample_frames{},presented_frames{};
void present(double now){++presented_frames;if(sample_start<0){'''+body+'''} };
int main(){Sample first;for(int i=0;i<=100;++i)first.present(100+i/20.0);
first.present(111); // Include slow intervals rather than silently exclude them.
Sample second;for(int i=0;i<=150;++i)second.present(200+i/30.0);}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code)
 subprocess.run(['g++','-std=c++20',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 rows=subprocess.check_output([str(p/'test')],text=True).splitlines()
 assert len(rows)==3,rows
 assert 'frames=100 seconds=5.000000 fps=20.000' in rows[0],rows
 assert 'frames=1 seconds=6.000000 fps=0.167 worst_ms=6000.000' in rows[1],rows
 assert 'frames=150 seconds=5.000000 fps=30.000' in rows[2],rows
print('Normal frame summaries: actual intervals, slow gaps, independent game sessions PASS')