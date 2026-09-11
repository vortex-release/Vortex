"""Pack a Source 2 Viewer glTF export for the native DX11 preview.

Usage: python pack-preview.py model.glb ../assets
Only body/glove meshes, skin transforms and base-color textures are retained.
The source asset stays in the user's installed game; Source 2 Viewer exports it.
"""
import json
import pathlib
import struct
import sys

source, destination = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
raw = source.read_bytes()
length = struct.unpack_from('<I', raw, 12)[0]
doc = json.loads(raw[20:20 + length])
binary = raw[28 + length:]

def accessor(index):
    a = doc['accessors'][index]
    view = doc['bufferViews'][a['bufferView']]
    code = {5121: 'B', 5123: 'H', 5125: 'I', 5126: 'f'}[a['componentType']]
    count = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}[a['type']]
    fmt = '<' + code * count
    offset = view.get('byteOffset', 0) + a.get('byteOffset', 0)
    stride = view.get('byteStride', struct.calcsize(fmt))
    values = [struct.unpack_from(fmt, binary, offset + i * stride) for i in range(a['count'])]
    if a.get('normalized'):
        maximum = 255 if code == 'B' else 65535
        values = [tuple(x / maximum for x in v) for v in values]
    return values

nodes = doc['nodes']
joints = doc['skins'][0]['joints']
joint_index = {node: i for i, node in enumerate(joints)}
parents = {child: p for p, node in enumerate(nodes) for child in node.get('children', [])}
inverse = accessor(doc['skins'][0]['inverseBindMatrices'])
pose = {node: dict(nodes[node]) for node in joints}
for animation in doc.get('animations', []):
    if animation.get('name') == 'tools_preview':
        for channel in animation['channels']:
            target = channel['target']
            sampler = animation['samplers'][channel['sampler']]
            pose[target['node']][target['path']] = accessor(sampler['output'])[0]

meshes = [m for m in doc['meshes'] if 'thirdperson_' in m.get('name', '')]
parts = []
vertices = []
indices = []
materials = []
for mesh in meshes:
    for primitive in mesh['primitives']:
        attrs = {k: accessor(v) for k, v in primitive['attributes'].items()}
        base = len(vertices)
        for p, n, uv, bones, weights in zip(attrs['POSITION'], attrs['NORMAL'], attrs['TEXCOORD_0'], attrs['JOINTS_0'], attrs['WEIGHTS_0']):
            assert all(0 <= b < len(joints) for b in bones)
            vertices.append(struct.pack('<8f4I4f', *p, *n, *uv, *bones, *weights))
        start = len(indices)
        indices.extend(base + value[0] for value in accessor(primitive['indices']))
        material = primitive['material']
        if material not in materials:
            materials.append(material)
        parts.append((start, len(indices) - start, materials.index(material)))

destination.mkdir(parents=True, exist_ok=True)
out = bytearray(struct.pack('<6I', 0x36565250, 1, len(vertices), len(indices), len(joints), len(parts)))
for i, node in enumerate(joints):
    p = pose[node]
    out.extend(struct.pack('<i26f', joint_index.get(parents.get(node), -1),
                           *p.get('translation', [0, 0, 0]), *p.get('rotation', [0, 0, 0, 1]),
                           *p.get('scale', [1, 1, 1]), *inverse[i]))
out.extend(b''.join(vertices))
out.extend(struct.pack('<' + 'I' * len(indices), *indices))
for part in parts:
    out.extend(struct.pack('<3I', *part))
(destination / 'preview-model.bin').write_bytes(out)

from PIL import Image
for i, material in enumerate(materials):
    data = doc['materials'][material]['pbrMetallicRoughness']
    texture = doc['textures'][data['baseColorTexture']['index']]
    image = doc['images'][texture['source']]
    with Image.open(source.parent / image['uri']) as picture:
        picture = picture.convert('RGBA')
        picture.thumbnail((1024, 1024))
        picture.save(destination / f'preview-{i}.png', optimize=True)
print(f'{len(vertices)} vertices, {len(indices)//3} triangles, {len(joints)} joints, {len(parts)} materials')
(destination / 'preview-model.json').write_text(json.dumps({
    'source': 'agents/models/ctm_sas/ctm_sas.vmdl_c',
    'exporter': 'Source 2 Viewer 20.0 (https://s2v.app)',
    'source_build': 14181,
    'character': 'SAS',
    'joints': [nodes[i]['name'] for i in joints],
    'live_pose': 'Retargets the shared 0..24 world skeleton; extra joints follow the exported local pose.',
    'source_to_preview': '(y, z, x) * 0.0254',
    'fallback': 'tools_preview pose with procedural breathing and turntable rotation',
}, indent=2) + '\n')
