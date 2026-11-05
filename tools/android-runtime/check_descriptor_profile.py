"""Check real and negative device captures against the completed candidate."""
from pathlib import Path
import copy
import importlib.util
import json

root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('android_preflight',root/'scripts/check-android-vulkan.py')
preflight=importlib.util.module_from_spec(spec);spec.loader.exec_module(preflight)
out=root/'build/android-descriptors';capture=root/'build/phone-vulkan.json'
manifest=out/'shader_manifest_bounded.json'
actual=preflight.assess(capture,root,0,manifest)
assert not actual['blockers'] and not actual['texture_upload_blockers']
assert len(actual['cpu_texture_fallbacks'])==4
original=json.loads(capture.read_text(encoding='utf-8-sig'))
cases=[]
def reject(name,mutate,blocker):
    data=copy.deepcopy(original);mutate(data['devices'][0])
    fixture=out/('fixture-'+name+'.json');fixture.write_text(json.dumps(data))
    report=preflight.assess(fixture,root,0,manifest)
    assert blocker in report['blockers'],(name,report['blockers'])
    cases.append(dict(case=name,rejected_for=blocker))
reject('no-dynamic-indexing',lambda d:d['features'].__setitem__('shaderSampledImageArrayDynamicIndexing',0),
       'capability:SampledImageArrayDynamicIndexing')
reject('too-few-images',lambda d:d['properties']['limits'].__setitem__('maxPerStageDescriptorSampledImages',191),
       'descriptor-limit:maxPerStageDescriptorSampledImages')
reject('too-few-samplers',lambda d:d['properties']['limits'].__setitem__('maxPerStageDescriptorSamplers',63),
       'descriptor-limit:maxPerStageDescriptorSamplers')
reject('too-few-resources',lambda d:d['properties']['limits'].__setitem__('maxPerStageResources',255),
       'descriptor-limit:maxPerStageResources')
reject('too-few-sets',lambda d:d['properties']['limits'].__setitem__('maxBoundDescriptorSets',3),
       'descriptor-limit:maxBoundDescriptorSets')
reject('no-geometry',lambda d:d['features'].__setitem__('geometryShader',0),'capability:Geometry')
legacy=preflight.assess(capture,root,0,root/'build/native-development/shader_cache/shader_manifest.json')
assert 'capability:RuntimeDescriptorArray' in legacy['blockers'] and 'capability:ClipDistance' in legacy['blockers']
summary=dict(candidate_checked_subset='no shader/texture blocker found',negative_cases=cases,
             legacy_profile_still_rejected=True,scope='Read-only device capture and source/manifest policy, not full renderer certification')
(out/'negative-checks.json').write_text(json.dumps(summary,indent=2)+'\n')
(out/'phone-renderer-feasibility.json').write_text(json.dumps(actual,indent=2)+'\n')
print(json.dumps(summary,indent=2))
