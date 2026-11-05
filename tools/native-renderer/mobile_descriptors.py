"""Explicit fixed descriptor-array profile; runtime indices are remapped per draw."""
import re

SLOTS = 64
ARRAYS = ('g_Texture2DDescriptorHeap', 'g_Texture3DDescriptorHeap',
          'g_TextureCubeDescriptorHeap', 'g_SamplerDescriptorHeap')


def rewrite(source):
    for name in ARRAYS:
        source, count = re.subn(r'\b' + name + r'\[\s*\]\s*:', f'{name}[{SLOTS}] :', source)
        if count != 1:
            raise ValueError(f'Expected one original declaration for {name}, got {count}')
    return source


def limits():
    return dict(maxBoundDescriptorSets=4, maxPerStageDescriptorSampledImages=3*SLOTS,
                maxPerStageDescriptorSamplers=SLOTS, maxPerStageResources=4*SLOTS,
                maxDescriptorSetSampledImages=3*SLOTS, maxDescriptorSetSamplers=SLOTS)
