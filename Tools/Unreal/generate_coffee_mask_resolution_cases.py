"""Offline direct64/128/256 mask rasterization and standalone algorithm timings.

No Unreal imports/Editor calls/project build. Compiles production MCCoffeeWipe.cpp
unchanged with a generated Size macro header and a small stdlib CoreMinimal shim.
The shim is not UE Core/its allocator; times are algorithm-only, not game CPU.
RGBA PNGs are coverage data, generated directly at each N, never upscaled.
"""
import argparse
import datetime
import hashlib
import json
import math
import os
import struct
import subprocess
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CPP = ROOT/'Source/MessControl/Private/MCCoffeeWipe.cpp'
HEADER = ROOT/'Source/MessControl/Public/MCCoffeeWipe.h'
BENCH = ROOT/'Tools/Benchmarks/coffee_mask_resolution_benchmark.cpp'
SHIM = ROOT/'Tools/Benchmarks/coffee_mask_resolution_shim'


def sha(data):
    return hashlib.sha256(data if isinstance(data, bytes) else Path(data).read_bytes()).hexdigest()


def summary(values):
    values = sorted(values)
    middle = len(values)//2
    return dict(median=(values[middle-1]+values[middle])*.5,
                p95=values[math.ceil(.95*len(values))-1], minimum=values[0], maximum=values[-1])


def png(path, mask, n):
    assert len(mask) == n*n
    def chunk(kind, data):
        return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    scan = b''.join(b'\0'+b''.join(bytes((v,v,v,255)) for v in mask[y*n:(y+1)*n]) for y in range(n))
    data = b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',n,n,8,6,0,0,0))
    data += chunk(b'IDAT',zlib.compress(scan,9))+chunk(b'IEND',b'')
    path.write_bytes(data)


def recipe():
    deposits=[]
    for points,radius in (([(.12,.34),(.25,.24),(.40,.30),(.53,.44),(.67,.35),(.82,.48),(.83,.66)],.12),
                          ([(.28,.29),(.25,.59),(.35,.76)],.04),
                          ([(.57,.43),(.65,.67),(.78,.77)],.04)):
        deposits += [dict(**{'from':a,'to':b},radius_uv=radius) for a,b in zip(points,points[1:])]
    line=[(.12,.67),(.45,.45),(.85,.20)]
    lengths=[math.dist(a,b) for a,b in zip(line,line[1:])]
    def point(fraction):
        distance=fraction*sum(lengths)
        for a,b,length in zip(line,line[1:],lengths):
            if distance <= length:
                return [a[i]+(b[i]-a[i])*distance/length for i in (0,1)]
            distance-=length
        return list(line[-1])
    samples=[point(i/48) for i in range(49)]
    return dict(deposits=deposits,
        brush=dict(polyline=line, radius_uv=.045, seconds_per_step=1/60, rate=1.,
                   steps_per_pass=48, passes=3, total_seconds=2.4,
                   segments=[{'from':a,'to':b} for a,b in zip(samples,samples[1:])]),
        deposit_formula='Original FMCCoffeeWipe::Deposit: capsule distance, SmoothStep(.72,1), RoundToInt, max',
        stroke_formula='Original FMCCoffeeWipe::Stroke: SmoothStep(.45,1), exp(-12*Exposure*Weight), fractional float carry, floor',
        rasterization='Each texel sampled directly at ((x+.5)/N,(y+.5)/N); no resizing or upscaling',
        color_contract='RGB=coverage byte, alpha=255; linear data, not sRGB')


def cpp_recipe(data):
    def segment(a,b,r):
        return '{{'+','.join(format(x,'.17g') for x in a)+'},{'+','.join(format(x,'.17g') for x in b)+'},'+format(r,'.17g')+'f}'
    deposit=',\n'.join(segment(s['from'],s['to'],s['radius_uv']) for s in data['deposits'])
    brush=',\n'.join(segment(s['from'],s['to'],data['brush']['radius_uv']) for s in data['brush']['segments'])
    return '#pragma once\n#include <array>\nnamespace CoffeeRecipe {\nstruct Segment {FVector2D From,To;float Radius;};\n'+ \
        'const std::array<Segment,'+str(len(data['deposits']))+'> Deposits={{\n'+deposit+'\n}};\n'+ \
        'const std::array<Segment,'+str(len(data['brush']['segments']))+'> Brush={{\n'+brush+'\n}};\n'+ \
        'constexpr int BrushPasses=3;\nconstexpr float Seconds=1.f/60.f,Rate=1.f;\n}\n'


def metrics(mask,n):
    wet=[((i%n+.5)/n,(i//n+.5)/n) for i,v in enumerate(mask) if v>63]
    return dict(sum=sum(mask), mean_coverage=sum(mask)/(255*n*n),
        nonzero_cells=sum(v>0 for v in mask), wet_cells_gt63=sum(v>63 for v in mask),
        wet_area_fraction_gt63=len(wet)/(n*n), core_cells_gt180=sum(v>180 for v in mask),
        wet_uv_bounds=dict(min=[min(p[i] for p in wet) for i in (0,1)],
                           max=[max(p[i] for p in wet) for i in (0,1)],
                           threshold=63, bounds='Inclusive cell centres') if wet else None)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'Saved/CoffeeMaskResolution/Data')
    args=parser.parse_args(); output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    build=output.parent/'Build';build.mkdir(parents=True,exist_ok=True)
    generated=build/'Generated';generated.mkdir(parents=True,exist_ok=True)
    original={str(p.relative_to(ROOT)):sha(p) for p in (CPP,HEADER)}
    source=HEADER.read_text(encoding='utf-8')
    assert source.count('static constexpr int32 Size=64;') == 1
    (generated/'MCCoffeeWipe.h').write_text(source.replace('static constexpr int32 Size=64;',
        'static constexpr int32 Size=MC_TEST_SIZE;'),encoding='utf-8')
    data=recipe(); canonical=json.dumps(data,sort_keys=True,separators=(',',':')).encode()
    (generated/'coffee_mask_resolution_recipe.h').write_text(cpp_recipe(data),encoding='utf-8')
    vswhere=Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Microsoft Visual Studio/Installer/vswhere.exe'
    install=subprocess.check_output([str(vswhere),'-latest','-products','*','-requires',
        'Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
    vcvars=Path(install)/'VC/Auxiliary/Build/vcvars64.bat'
    assert vcvars.is_file(), 'MSVC x64 environment unavailable'
    cpu=dict(scope='Standalone original algorithm code with stdlib shim; not Unreal gameplay CPU, allocator, RHI upload or replication timings',
        compiler='MSVC x64 /O2 /fp:precise /std:c++20; no LTO; production cpp separate translation unit',
        source_sha256=original, shim_sha256=sha(SHIM/'CoreMinimal.h'), benchmark_sha256=sha(BENCH),
        generated_header_sha256=sha(generated/'MCCoffeeWipe.h'), recipe_sha256=sha(canonical),
        resets='Allocation/reset/copies outside timed interval; warm cache; 24 warmups then8 batches ×30 calls per operation',
        stroke_fixture=data['brush']['segments'][24], deposit_fixture=data['deposits'][2],
        caveats=['Math/vector/container shim uses standard C++ library, not linked UE Core; bit parity with a running UE build is not asserted.',
                 'Single-host scalar algorithm microbenchmark; background load, CPU clocks and scheduling can affect timing.',
                 'Remaining scans the full array; HasWetCells is measured on all-zero mask to exercise its worst full scan.',
                 'GPU and network behavior are not measured by this helper.'], results=[])
    cases=[]
    for n in (64,128,256):
        case=output/str(n);case.mkdir(parents=True,exist_ok=True)
        binary_dir=build/str(n);binary_dir.mkdir(parents=True,exist_ok=True)
        exe=binary_dir/'coffee_mask_resolution_benchmark.exe'
        command=binary_dir/'build.cmd'
        command.write_text('@echo off\ncall "'+str(vcvars)+'" >nul\nif errorlevel 1 exit /b 1\n'+
            'cl.exe /nologo /O2 /EHsc /std:c++20 /fp:precise /DMC_TEST_SIZE='+str(n)+' '+
            '/I"'+str(generated)+'" /I"'+str(SHIM)+'" "'+str(CPP)+'" "'+str(BENCH)+'" '+
            '/Fe:"'+str(exe)+'" /Fo:"'+binary_dir.as_posix()+'/"\nexit /b %errorlevel%\n',encoding='utf-8')
        result=subprocess.run(['cmd.exe','/d','/c',str(command)],cwd=binary_dir,capture_output=True,text=True)
        (binary_dir/'Build.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        if result.returncode:
            raise RuntimeError('Standalone compile failed: '+str(binary_dir/'Build.log'))
        result=subprocess.run([str(exe),str(case)],capture_output=True,text=True,check=True)
        (case/'Run.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        timing=json.loads((case/'timings.json').read_text())
        timed={name:dict(**summary(v), batch_medians=[summary(v[i:i+30])['median'] for i in range(0,240,30)],
                        samples=len(v)) for name,v in timing['samples'].items()}
        cpu['results'].append(dict(resolution=n,unit=timing['unit'],metrics=timed,
            raw_samples=str(case/'timings.json'), raw_samples_sha256=sha(case/'timings.json')))
        entry=dict(resolution=n,metrics={})
        for state in ('before','after'):
            mask=(case/(state+'.mask')).read_bytes(); assert len(mask)==n*n
            file=case/(state+'.png');png(file,mask,n)
            entry[state+'_path']=str(file);entry[state+'_sha256']=sha(file)
            entry[state+'_mask_sha256']=sha(mask);entry['metrics'][state]=metrics(mask,n)
        assert entry['metrics']['after']['sum'] < entry['metrics']['before']['sum']
        entry['budget_raw_bytes']=dict(one_byte_mask=n*n,two_replicated_byte_arrays=2*n*n,
            fractional_float_array=4*n*n,gpu_bgra_texture=4*n*n,full_dirty_upload=4*n*n,
            max_upload_bytes_per_second_at30hz=4*n*n*30,
            note='Raw payload/texel sizes only; no allocator/RHI alignment, protocol/delta/compression or bandwidth measurement')
        cases.append(entry)
    assert original=={str(p.relative_to(ROOT)):sha(p) for p in (CPP,HEADER)}, 'Production source changed during generation'
    cpu_path=output/'CpuAlgorithmReport.json';cpu_path.write_text(json.dumps(cpu,indent=2)+'\n',encoding='utf-8')
    result=dict(schema_version=1,generated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        world_size_cm=1200.,half_size_cm=600.,uv_orientation='PNG row0 = V0; col0 = U0; no flip',
        recipe=data,recipe_sha256=sha(canonical),native_source_sha256=original,
        cpu_report=str(cpu_path),cpu_report_sha256=sha(cpu_path),cases=cases,
        scope='Independent direct-N offline coverage data and scalar algorithm timings. Native gameplay remains64; higher-resolution cleaning/network integration is not delivered.',
        source_unchanged=True)
    path=output/'cases.json';path.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(cases=str(path),cpu_report=str(cpu_path),resolutions=[c['resolution'] for c in cases])))


if __name__=='__main__':
    main()
