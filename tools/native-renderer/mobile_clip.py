"""Explicit geometry-clipping interface for translated Vulkan vertex shaders."""
import re

INTERPOLATORS = 18
CLIP_DISTANCES = 6
GEOMETRY_VERTICES = 9


def rewrite_vertex(source):
    """Change only clip-distance output declarations; preserve all computations."""
    for number, width, location in ((0, 4, 18), (1, 2, 19)):
        pattern = rf'\bout float{width} oClipDistance{number}\s*:\s*SV_ClipDistance{number}\b'
        # The compiler uses declaration-order I/O locations. The original 18
        # varyings precede these two, so they become locations 18 and 19. Keep
        # every output implicit: this DXC rejects partial explicit assignments.
        replacement = f'out float{width} oClipDistance{number} : TEXCOORD{location}'
        source, count = re.subn(pattern, replacement, source)
        if count != 1:
            raise ValueError(f'Expected one original clip output {number}, found {count}')
    if re.search(r'\bSV_ClipDistance\w*\b', source):
        raise ValueError('Unmapped native clip-distance declaration remains')
    return source


def required_limits():
    # Per vertex: Position + 18 float4 varyings + six user distances on input.
    # The geometry stage consumes distances and outputs only Position/varyings.
    return dict(maxVertexOutputComponents=82, maxGeometryInputComponents=82,
                maxGeometryOutputComponents=76, maxGeometryOutputVertices=9,
                maxGeometryTotalOutputComponents=684)


def check_geometry(interface):
    located = lambda values: {v['location']: v['type'] for v in values if 'location' in v}
    builtins = lambda values: {v['builtin']: v['type'] for v in values if 'builtin' in v}
    inputs = {i: 'float4[3]' for i in range(19)}
    inputs[19] = 'float2[3]'
    outputs = {i: 'float4' for i in range(INTERPOLATORS)}
    modes = {m['mode']: m['arguments'] for m in interface['execution_modes']}
    return (interface['entry_points'] == [dict(model='geometry', name='main')] and
            located(interface['inputs']) == inputs and located(interface['outputs']) == outputs and
            builtins(interface['inputs']) == {'Position': 'float4[3]'} and
            builtins(interface['outputs']) == {'Position': 'float4'} and
            modes.get(26) == [GEOMETRY_VERTICES] and  # SPIR-V OutputVertices.
            'Geometry' in interface['capabilities'] and
            set(interface['capabilities']) <= {'Geometry', 'Shader'} and
            not interface['bindings'] and not interface['push_constant_bytes'] and not interface['spec_constants'])
