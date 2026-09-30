#!/usr/bin/env python3
"""Validate every matrix cell before reporting medians; rep0 is warmup."""
import json,re,statistics,sys
from pathlib import Path
MODES=('group','flush_each','finish_each','query_wait','query_poll','present')
PATTERN=re.compile(r'EDEN_GPU_PROBE mode=(\w+) width=(\d+) height=(\d+) draws=(\d+) rep=(\d+) issue_ns=(\d+) issue_cpu_ns=(\d+) finish_ns=(\d+) finish_cpu_ns=(\d+) wall_ns=(\d+) cpu_ns=(\d+) available=(\d+) pixel=64,128,191,255')
def analyze_native(text):
    for memory_type in (12,0):
        assert f'EDEN_NATIVE_QUEUE_PASS cases=32 memory_type={memory_type} matrix_submit_mode=0' in text
    pattern=r'EDEN_NATIVE_QUEUE memory_type=(\d+) data=(\d+) sleep=(\d+) rep=(\d+) submit_ns=(\d+) first_ns=(\d+) wall_ns=(\d+) cpu_ns=(\d+) gpu_ticks=(\d+) polls=(\d+) marker=101'
    rows={}
    for m in re.finditer(pattern,text):
        memory_type,data,sleep,rep,submit,first,wall,cpu,ticks,polls=map(int,m.groups())
        assert memory_type in (12,0) and data in (0,1,2,3) and sleep in (0,1) and rep in range(4)
        assert 0<submit<=first<=wall<=2000000000
        key=(memory_type,data,sleep,rep); assert key not in rows
        rows[key]=(submit,first,wall,cpu,ticks)
    assert len(rows)==64
    return [dict(memory_type=memory_type,data=data,sleep=sleep,**{field:statistics.median(rows[(memory_type,data,sleep,rep)][i] for rep in (1,2,3))
            for i,field in enumerate(('submit_ns','first_ns','wall_ns','cpu_ns','gpu_ticks'))})
            for memory_type in (12,0) for data in (0,1,2,3) for sleep in (0,1)]

def analyze(text):
    if 'EDEN_NATIVE_QUEUE' in text: analyze_native(text)
    assert 'EDEN_GPU_PROBE_PASS cases=36 repeats=4 warmup_rep=0' in text
    assert 'EDEN_PERF_OWNER_CLOCK_CHECK valid=1 ' in text
    assert 'Graphics failure:' not in text
    modes=tuple('finish_spin' if m=='finish_each' and 'mode=finish_spin ' in text else m for m in MODES)
    cells={}
    for match in PATTERN.finditer(text):
        mode,*values=match.groups()
        w,h,d,rep,issue,ic,finish,fc,wall,cpu,ready=map(int,values)
        assert mode in modes and (w,h) in ((16,16),(1920,1080)) and d in (1,16,128) and rep in range(4)
        assert wall==issue+finish and cpu==ic+fc and wall>0 and ready<=d
        key=(mode,w,d,rep); assert key not in cells
        cells[key]=(wall,cpu,issue,finish)
    assert len(cells)==144
    summary=[]
    for mode in modes:
        for w in (16,1920):
            for d in (1,16,128):
                samples=[cells[(mode,w,d,rep)] for rep in (1,2,3)]
                summary.append(dict(mode=mode,width=w,draws=d,**{
                    field:statistics.median(row[i] for row in samples)/1e6
                    for i,field in enumerate(('wall_ms','cpu_ms','issue_ms','finish_ms'))}))
    return summary
if __name__=='__main__':
    if sys.argv[1:]==['--self-test']:
        source='EDEN_PERF_OWNER_CLOCK_CHECK valid=1 busy_ns=20000000\nEDEN_GPU_PROBE_PASS cases=36 repeats=4 warmup_rep=0\n'
        for mode in MODES:
            for w,h in ((16,16),(1920,1080)):
                for d in (1,16,128):
                    for rep in range(4):
                        source+=f'EDEN_GPU_PROBE mode={mode} width={w} height={h} draws={d} rep={rep} issue_ns=2 issue_cpu_ns=1 finish_ns=8 finish_cpu_ns=1 wall_ns=10 cpu_ns=2 available=0 pixel=64,128,191,255\n'
        assert len(analyze(source))==36
        assert len(analyze(source.replace("mode=finish_each ","mode=finish_spin ")))==36
        for bad in (source.replace('rep=3','rep=2'),source.replace('wall_ns=10','wall_ns=11'),source.replace('valid=1','valid=0'),source.replace('pixel=64','pixel=63')):
            try: analyze(bad)
            except AssertionError: pass
            else: raise AssertionError('invalid receipt accepted')
        native=''
        for memory_type in (12,0):
            native+=f'EDEN_NATIVE_QUEUE_PASS cases=32 memory_type={memory_type} matrix_submit_mode=0\n'
            for data in (0,1,2,3):
                for sleep in (0,1):
                    for rep in range(4):
                        native+=f'EDEN_NATIVE_QUEUE memory_type={memory_type} data={data} sleep={sleep} rep={rep} submit_ns=1 first_ns=2 wall_ns=3 cpu_ns=2 gpu_ticks=5 polls=1 marker=101\n'
        assert len(analyze_native(native))==16
        for bad in (native.replace('rep=3','rep=2'),native.replace('marker=101','marker=0'),native.replace('first_ns=2','first_ns=4')):
            try: analyze_native(bad)
            except AssertionError: pass
            else: raise AssertionError('invalid native receipt accepted')
        print('GPU matrix validator PASS: completeness, duplicates, pixels, CPU qualification, accounting')
    elif sys.argv[1:2]==['--native']:
        print(json.dumps(analyze_native(Path(sys.argv[2]).read_text(errors='replace')),indent=2))
    else:
        print(json.dumps(analyze(Path(sys.argv[1]).read_text(errors='replace')),indent=2))
