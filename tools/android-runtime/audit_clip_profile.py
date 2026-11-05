"""Audit the completed geometry-clipping corpus and build its candidate manifest.

Uses baseline DXIL metadata, the new SPIR-V corpus and the new renderer passes.
No baseline cache is modified and no partial corpus can pass this audit.
"""
from pathlib import Path
import hashlib
import json
import re
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tools/native-renderer'))
import mobile_clip
import spirv_module

out = root / 'build/android-clip'
baseline = root / 'build/native-development/shader_cache/work'
corpus = out / 'corpus-geometry'
passes = out / 'passes-geometry'
read = lambda path: json.loads(path.read_text(encoding='utf-8-sig'))
report = read(corpus / 'report.json')
assert report['counts'] == {'ps:ok': 2580, 'vs:ok': 2429} and not report['broken']
assert 'ClipDistance' not in report['summary']['capabilities']
old = {r['name']: r for r in read(baseline / 'spirv/report.json')['results']}
stub = '\nuint g_SpecConstants() { return 0; }\n'
vertices = pixels = 0
for entry in report['results']:
    name = entry['name']
    if entry['stage'] == 'ps':
        assert entry['spirv_sha256'] == old[name]['spirv_sha256'], 'Pixel binary changed: ' + name
        assert not any(v.get('interpolation') for v in entry['interface']['inputs']), 'Unaudited pixel interpolation'
        pixels += 1
        continue
    original = (baseline / 'per-shader/hlsl' / (name + '.hlsl')).read_text(encoding='utf-8')
    original = original[:-len(stub)] + '\n' if original.endswith(stub) else original
    transformed = (corpus / 'hlsl' / (name + '.hlsl')).read_text(encoding='utf-8')
    assert transformed == mobile_clip.rewrite_vertex(original)
    # Independently compare every other byte after normalizing just the two
    # declarations. Original instructions and clip-plane evaluation order stay.
    for number, width, location in ((0, 4, 18), (1, 2, 19)):
        pattern = rf'\bout float{width} oClipDistance{number}\s*:\s*'
        original, count = re.subn(pattern + rf'SV_ClipDistance{number}\b', f'CLIP_OUTPUT_{number}', original)
        transformed, changed = re.subn(pattern + rf'TEXCOORD{location}\b', f'CLIP_OUTPUT_{number}', transformed)
        assert count == changed == 1
    assert original == transformed, 'Non-interface computation changed: ' + name
    vertices += 1
pass_report = read(passes / 'report.json')
assert not pass_report['broken'] and len(pass_report['results']) == 19
assert all(r['result'] == 'ok' for r in pass_report['results'])
geometry = (passes / 'spirv/clip_planes_gs.spv').read_bytes()
assert mobile_clip.check_geometry(spirv_module.parse(geometry))
tested_geometry = (root / 'build/android-runtime/clip_planes_gs.spv').read_bytes()
assert geometry == tested_geometry, 'Packaged geometry module differs from the tested module'
work = out / 'manifest-work'
for name, source in (('per-shader', baseline / 'per-shader/report.json'),
                     ('variants', baseline / 'variants/report.json'),
                     ('spirv', corpus / 'report.json'), ('passes', passes / 'report.json')):
    directory = work / name
    directory.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, directory / 'report.json')
manifest = out / 'shader_manifest_geometry.json'
subprocess.run([sys.executable, '-I', str(root / 'tools/native-renderer/build-shader-manifest.py'),
                str(work), str(manifest)], check=True)
data = read(manifest)
assert 'Geometry' in data['spirv']['required']['capabilities']
assert 'ClipDistance' not in data['spirv']['required']['capabilities']
assert data['spirv']['stage_interface']['geometry_limits'] == mobile_clip.required_limits()
audit = dict(vertex_shaders=vertices, pixel_shaders=pixels, pixel_modules_unchanged=True,
             vertex_computations_unchanged=True, renderer_passes=19,
             geometry_module_matches_phone_test=True,
             geometry_sha256=hashlib.sha256(geometry).hexdigest(), contract_sha256=data['contract_sha256'])
(out / 'corpus-audit.json').write_text(json.dumps(audit, indent=2) + '\n')
print(json.dumps(audit, indent=2))
