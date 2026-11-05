"""Pack the audited mobile SPIR-V candidate with the production container writer."""
from pathlib import Path
import importlib.util
import hashlib
import json
import subprocess

root=Path(__file__).resolve().parents[2]
out=root/'build/android-renderer-cache';out.mkdir(parents=True,exist_ok=True)
manifest=root/'build/android-descriptors/shader_manifest_bounded.json'
audit=json.loads((root/'build/android-descriptors/corpus-audit.json').read_text())
full=json.loads(manifest.read_text());assert audit['contract_sha256']==full['contract_sha256']
encoder=root/'build/native-development/broken_bond_smolv_encode.exe'
compressor=root/'build/native-development/broken_bond_shader_compress.exe'
listing=out/'smolv.tsv';rows=[]
for name,source in [('shaders',root/'build/android-descriptors/corpus/spirv'),('passes',root/'build/android-clip/passes-geometry/spirv')]:
    folder=out/name;folder.mkdir(exist_ok=True)
    expected = ({key: entry['spirv'] for key, entry in full['shaders'].items()}
                if name == 'shaders' else full['passes'])
    modules = sorted(source.glob('*.spv'))
    if {p.stem for p in modules} != set(expected):
        raise RuntimeError(f'{name}: module names differ from the audited manifest')
    for spv in modules:
        if hashlib.sha256(spv.read_bytes()).hexdigest() != expected[spv.stem]:
            raise RuntimeError(f'{spv}: module bytes differ from the audited manifest')
        rows.append(f'{spv}\t{folder/(spv.stem+".smolv")}\n')
listing.write_text(''.join(rows),encoding='utf-8')
subprocess.run([str(encoder),str(listing)],check=True)
spec=importlib.util.spec_from_file_location('cache_writer',root/'tools/native-renderer/build-embedded-shader-cache.py')
writer=importlib.util.module_from_spec(spec);spec.loader.exec_module(writer)
baseline=(root/'build/native-development/shader_cache/work/index/index.tsv').read_text()
lines=[]
for line in baseline.splitlines():
    fields=line.split('\t')
    if fields[0] in ('dxil','variants'):continue
    if fields[0]=='spirv':
        lines += ['spirv\tembedded',f'contract\t{full["contract_sha256"]}',*writer.contract_lines(full['spirv'])]
        described=full['spirv']['passes']['passes']
        lines += [f'pass\t{name}\t{described[name]["stage"]}\t{described[name]["layout"]}\t{sha}' for name,sha in sorted(full['passes'].items())]
    elif fields[0] in ('vs','ps'):lines.append(line+'\t'+full['shaders'][fields[1]]['spirv'])
    else:lines.append(line)
embedded=dict(schema=full['schema'],contract_sha256=full['contract_sha256'],spirv=full['spirv'],
              shaders={name:entry['spirv'] for name,entry in full['shaders'].items()},passes=full['passes'])
entries=[(writer.KIND_SPIRV,name,(out/'shaders'/(name+'.smolv')).read_bytes()) for name in full['shaders']]
entries.append((writer.KIND_MANIFEST,'manifest.json',json.dumps(embedded,sort_keys=True,separators=(',',':')).encode()))
entries += [(writer.KIND_PASS,name,(out/'passes'/(name+'.smolv')).read_bytes()) for name in sorted(full['passes'])]
report=writer.package(5,lines,entries,out/'shader_cache_spirv.bin',compressor)
report.update(contract_sha256=full['contract_sha256'],shaders=len(full['shaders']),passes=len(full['passes']))
(out/'package-report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
