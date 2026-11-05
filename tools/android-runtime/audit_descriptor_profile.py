"""Audit the complete bounded-descriptor/geometry-clip shader candidate."""
from pathlib import Path
import json
import re
import shutil
import subprocess
import sys

root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tools/native-renderer'))
import mobile_clip
import mobile_descriptors
import spirv_module

out=root/'build/android-descriptors'
baseline=root/'build/native-development/shader_cache/work'
corpus=out/'corpus'
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
report=read(corpus/'report.json')
assert report['counts']=={'ps:ok':2580,'vs:ok':2429} and not report['broken']
for forbidden in ('RuntimeDescriptorArray','ShaderNonUniform','SampledImageArrayNonUniformIndexing','ClipDistance'):
    assert forbidden not in report['summary']['capabilities']
assert 'SPV_EXT_descriptor_indexing' not in report['summary']['extensions']
stub='\nuint g_SpecConstants() { return 0; }\n'
counts={'vs':0,'ps':0}
for entry in report['results']:
    name=entry['name'];original=(baseline/'per-shader/hlsl'/(name+'.hlsl')).read_text(encoding='utf-8')
    original=original[:-len(stub)]+'\n' if original.endswith(stub) else original
    transformed=(corpus/'hlsl'/(name+'.hlsl')).read_text(encoding='utf-8')
    wanted=mobile_clip.rewrite_vertex(original) if entry['stage']=='vs' else original
    assert transformed==mobile_descriptors.rewrite(wanted)
    for heap in mobile_descriptors.ARRAYS:
        original,n=re.subn(r'\b'+heap+r'\[\s*\]\s*:',heap+'[ARRAY_SIZE]:',original)
        transformed,m=re.subn(r'\b'+heap+r'\[64\]\s*:',heap+'[ARRAY_SIZE]:',transformed)
        assert n==m==1
    if entry['stage']=='vs':
        for number,width,location in ((0,4,18),(1,2,19)):
            pattern=rf'\bout float{width} oClipDistance{number}\s*:\s*'
            original,n=re.subn(pattern+rf'SV_ClipDistance{number}\b',f'CLIP_OUTPUT_{number}',original)
            transformed,m=re.subn(pattern+rf'TEXCOORD{location}\b',f'CLIP_OUTPUT_{number}',transformed)
            assert n==m==1
    assert original==transformed,'Shader computation changed: '+name
    assert not any(v.get('interpolation') for v in entry['interface']['inputs'] if entry['stage']=='ps')
    counts[entry['stage']]+=1
header=(root/'src/gpu/backend/vulkan/bounded_descriptors.h').read_text()
assert re.search(r'kBoundedSlots\s*=\s*64\s*;',header)
work=out/'manifest-work'
for name,source in (('per-shader',baseline/'per-shader/report.json'),('variants',baseline/'variants/report.json'),
                    ('spirv',corpus/'report.json'),('passes',root/'build/android-clip/passes-geometry/report.json')):
    dest=work/name;dest.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,dest/'report.json')
manifest=out/'shader_manifest_bounded.json'
subprocess.run([sys.executable,'-I',str(root/'tools/native-renderer/build-shader-manifest.py'),str(work),str(manifest)],check=True)
data=read(manifest)
assert 'SampledImageArrayDynamicIndexing' in data['spirv']['required']['capabilities']
assert data['spirv']['layout']['descriptor_slots']==64
assert data['spirv']['layout']['descriptor_limits']==mobile_descriptors.limits()
probe=spirv_module.parse((root/'build/android-runtime/descriptor_probe.spv').read_bytes())
assert not set(probe['capabilities'])&{'RuntimeDescriptorArray','ShaderNonUniform','SampledImageArrayNonUniformIndexing'}
assert 'SPV_EXT_descriptor_indexing' not in probe['extensions']
bindings={(b['set'],b['binding']):b['type'] for b in probe['bindings']}
assert bindings[(0,0)]=='image2D<float>[64]' and bindings[(1,0)]=='image3D<float>[64]'
assert bindings[(2,0)]=='imageCube<float>[64]' and bindings[(3,0)]=='sampler[64]'
result=dict(vertex_shaders=counts['vs'],pixel_shaders=counts['ps'],computation_sources_preserved=True,
            only_interface_and_array_declarations_changed=True,forbidden_descriptor_capabilities_absent=True,
            slots_per_array=64,probe_fixed_bindings_verified=True,contract_sha256=data['contract_sha256'],
            required_capabilities=data['spirv']['required']['capabilities'])
(out/'corpus-audit.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
