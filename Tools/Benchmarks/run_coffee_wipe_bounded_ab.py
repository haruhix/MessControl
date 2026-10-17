"""Offline full-scan / bounded Stroke A/B; never imports Unreal or changes native code.

prepare: compile unchanged production cpp plus generated experimental candidates,
then run correctness only. measure: explicit timing run, using prepared binaries.
Use measure only during the parent-approved idle measurement window.
"""
import argparse
import datetime
import hashlib
import json
import math
import os
import statistics
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CPP = ROOT/'Source/MessControl/Private/MCCoffeeWipe.cpp'
HEADER = ROOT/'Source/MessControl/Public/MCCoffeeWipe.h'
BENCH = Path(__file__).with_name('coffee_wipe_bounded_ab.cpp')
SHIM = Path(__file__).with_name('coffee_mask_resolution_shim')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def candidates(source):
    start = source.index('bool FMCCoffeeWipe::Stroke(')
    end = source.index('float FMCCoffeeWipe::Remaining(', start)
    original = source[start:end]
    assert original.count('for (int32 Y=0;Y<Size;++Y) for (int32 X=0;X<Size;++X)') == 1
    bounds = '''const int32 MinX=FMath::FloorToInt(FMath::Clamp(FMath::Min(From.X,To.X)-Radius,0.,1.)*Size);
    const int32 MaxX=FMath::Min(Size-1,FMath::CeilToInt(FMath::Clamp(FMath::Max(From.X,To.X)+Radius,0.,1.)*Size));
    const int32 MinY=FMath::FloorToInt(FMath::Clamp(FMath::Min(From.Y,To.Y)-Radius,0.,1.)*Size);
    const int32 MaxY=FMath::Min(Size-1,FMath::CeilToInt(FMath::Clamp(FMath::Max(From.Y,To.Y)+Radius,0.,1.)*Size));
    for (int32 Y=MinY;Y<=MaxY;++Y) for (int32 X=MinX;X<=MaxX;++X)'''
    base = original.replace('bool FMCCoffeeWipe::Stroke(', 'COFFEE_NOINLINE bool StrokeBounded(').replace(
        'for (int32 Y=0;Y<Size;++Y) for (int32 X=0;X<Size;++X)', bounds)
    # Free candidate functions keep exactly the native class constants/Reset.
    base = base.replace('    if (From.ContainsNaN()',
        '    constexpr int32 Size=FMCCoffeeWipe::Size,Count=FMCCoffeeWipe::Count;\n    if (From.ContainsNaN()', 1)
    base = base.replace('Reset(Mask)', 'FMCCoffeeWipe::Reset(Mask)')
    tracked = base.replace('StrokeBounded(', 'StrokeBoundedTracked(', 1)
    tracked = tracked.replace('float Rate)\n', 'float Rate,FByteAggregates* Aggregates)\n', 1)
    assert 'float Rate,FByteAggregates* Aggregates)' in tracked
    tracked = tracked.replace('if (Mask.Num()!=Count) FMCCoffeeWipe::Reset(Mask);',
        'if (Mask.Num()!=Count) { FMCCoffeeWipe::Reset(Mask); if(Aggregates) *Aggregates=ScanAggregates(Mask); }')
    target = '        Changed|=Value!=Next; Value=Next;'
    assert tracked.count(target) == 1
    tracked = tracked.replace(target, '''        if(Aggregates) {
            Aggregates->Sum+=int32(Next)-int32(Value);
            Aggregates->Wet+=int32(Next>63)-int32(Value>63);
            Aggregates->Nonzero+=int32(Next>0)-int32(Value>0);
        }
'''+target)
    return '// Generated from current production Stroke; never a native patch.\n'+base+'\n'+tracked


def summary(values):
    ordered = sorted(values)
    assert len(ordered) == 240 and all(math.isfinite(v) and v >= 0 for v in ordered)
    return dict(median=statistics.median(values), p95=ordered[math.ceil(.95*len(ordered))-1],
                minimum=ordered[0], maximum=ordered[-1], samples=len(values),
                zero_duration_samples=sum(v == 0 for v in values),
                median_at_clock_floor=statistics.median(values) == 0,
                batch_medians=[statistics.median(values[i:i+30]) for i in range(0,240,30)])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('prepare','measure'), default='prepare')
    parser.add_argument('--output', type=Path, default=ROOT/'Saved/CoffeeWipeBoundedAB')
    args = parser.parse_args()
    output = args.output.resolve(); output.mkdir(parents=True,exist_ok=True)
    build = output/'Build'; generated = build/'Generated'; generated.mkdir(parents=True,exist_ok=True)
    reports = output/'Reports'; reports.mkdir(exist_ok=True)
    paths = (CPP, HEADER, BENCH, Path(__file__).resolve(), SHIM/'CoreMinimal.h')
    hashes = {str(p.relative_to(ROOT)):sha(p) for p in paths}
    manifest_path = output/'PreparedManifest.json'
    if args.mode == 'prepare':
        header = HEADER.read_text(encoding='utf-8')
        assert header.count('static constexpr int32 Size=64;') == 1
        (generated/'MCCoffeeWipe.h').write_text(header.replace('static constexpr int32 Size=64;',
            'static constexpr int32 Size=MC_TEST_SIZE;'),encoding='utf-8')
        (generated/'coffee_wipe_bounded_candidates.inc').write_text(candidates(CPP.read_text()),encoding='utf-8')
        vswhere = Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Microsoft Visual Studio/Installer/vswhere.exe'
        install = subprocess.check_output([str(vswhere),'-latest','-products','*','-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
        vcvars = Path(install)/'VC/Auxiliary/Build/vcvars64.bat'
        assert vcvars.is_file()
        manifest = dict(prepared_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(), source_sha256=hashes,
            compiler='MSVC x64 /O2 /fp:precise /std:c++20; no LTO; native cpp separate translation unit; candidates/wrappers noinline',
            generated_sha256={p.name:sha(p) for p in generated.iterdir()}, binaries=[])
        for n in (64,128,256):
            folder = build/str(n);folder.mkdir(exist_ok=True)
            exe = folder/'coffee_wipe_bounded_ab.exe';cmd = folder/'build.cmd'
            cmd.write_text('@echo off\ncall "'+str(vcvars)+'" >nul\nif errorlevel 1 exit /b 1\n'+
                'cl.exe /nologo /O2 /EHsc /std:c++20 /fp:precise /DMC_TEST_SIZE='+str(n)+' '+
                '/I"'+str(generated)+'" /I"'+str(SHIM)+'" "'+str(CPP)+'" "'+str(BENCH)+'" '+
                '/Fe:"'+str(exe)+'" /Fo:"'+folder.as_posix()+'/"\nexit /b %errorlevel%\n',encoding='utf-8')
            done = subprocess.run(['cmd.exe','/d','/c',str(cmd)],cwd=folder,capture_output=True,text=True)
            (folder/'Build.log').write_text(done.stdout+done.stderr,encoding='utf-8')
            if done.returncode: raise RuntimeError('Compile failed: '+str(folder/'Build.log'))
            verify = reports/f'Correctness_{n}.json'
            done = subprocess.run([str(exe),'verify',str(verify)],capture_output=True,text=True)
            (folder/'Verify.log').write_text(done.stdout+done.stderr,encoding='utf-8')
            if done.returncode: raise RuntimeError('Parity failed: '+str(folder/'Verify.log'))
            manifest['binaries'].append(dict(resolution=n,exe=str(exe),exe_sha256=sha(exe),
                correctness=str(verify),correctness_sha256=sha(verify),result=json.loads(verify.read_text())))
        assert hashes == {str(p.relative_to(ROOT)):sha(p) for p in paths}, 'Source changed during preparation'
        manifest_path.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(dict(prepared=str(manifest_path),correctness_passed=True,timed_benchmark_run=False)))
        return
    manifest = json.loads(manifest_path.read_text())
    assert hashes == manifest['source_sha256'], 'Prepared binaries are stale; rerun prepare'
    assert all(sha(generated/name)==value for name,value in manifest['generated_sha256'].items())
    report = dict(scope='Standalone scalar Stroke algorithm A/B; not Unreal game thread, tooth integration, network, GPU or upload measurement',
        generated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),prepared_manifest=str(manifest_path),
        prepared_manifest_sha256=sha(manifest_path),source_sha256=hashes,compiler=manifest['compiler'],
        method='24 warmups each; 8x30 paired samples alternating AB/BA; reset/copy/allocation/checksum outside timing; steady_clock and compiler fences',
        aggregate_scope='Exact byte sum, cells>63 and cells>0 only; not the deposited-mask-weighted AMCMouthSurface::RemainingLiquid numerator',
        caveats=['stdlib shim is not UE Core; engine bit parity not asserted.',
            'Warm-cache one-host microbenchmark; very short no-op timings are clock/dispatch floor.',
            'Full-scan A is unchanged native cpp. B changes loop AABB only. C additionally updates generic aggregates.',
            'Higher resolutions are offline experiments; production/native gameplay remains64.',
            'No benchmark for applying masks to tooth UVs, native contacts, lifecycle, allocations, upload, replication or geometry.'],results=[])
    for item in manifest['binaries']:
        exe = Path(item['exe']);assert sha(exe)==item['exe_sha256']
        n = item['resolution'];rawpath=reports/f'Timings_{n}.json'
        done = subprocess.run([str(exe),'benchmark',str(rawpath)],capture_output=True,text=True)
        (build/str(n)/'Benchmark.log').write_text(done.stdout+done.stderr,encoding='utf-8')
        if done.returncode: raise RuntimeError('Run failed: '+str(build/str(n)/'Benchmark.log'))
        raw=json.loads(rawpath.read_text());metrics=[]
        for fixture in raw['fixtures']:
            names=('full_scan_A','bounded_B','bounded_plus_aggregate_scan','bounded_incremental_aggregates')
            data={name:summary(fixture[name]) for name in names}
            data.update(name=fixture['name'],full_to_bounded_median_ratio=(data['full_scan_A']['median']/data['bounded_B']['median']
                if data['bounded_B']['median'] else None),
                aggregate_scan_to_incremental_median_ratio=(data['bounded_plus_aggregate_scan']['median']/data['bounded_incremental_aggregates']['median']
                if data['bounded_incremental_aggregates']['median'] else None))
            metrics.append(data)
        report['results'].append(dict(resolution=n,raw_samples=str(rawpath),raw_samples_sha256=sha(rawpath),
            correctness=raw['correctness'],metrics=metrics))
    assert hashes == {str(p.relative_to(ROOT)):sha(p) for p in paths}, 'Source changed during measurement'
    result=reports/'Report.json';result.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(report=str(result),timed_benchmark_run=True)))


if __name__ == '__main__':
    main()
