"""Keep the native ground backing and runtime GLB materials in sync.

Apply inside background Blender with ground_modules.blend open:
  blender --background assets/custom/source/ground_modules.blend --python tools/update_ground_recess_material.py -- --apply
Verify exported materials with ordinary Python:
  python tools/update_ground_recess_material.py --verify

Only GroundModuleRecess base color changes. GLB binary chunks, other materials,
geometry, UVs, texture maps and samplers remain byte-identical. Native meshes
and their frozen construction references are never rebuilt.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import shutil
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'assets/custom/source/ground_modules.blend'
MODELS = ROOT / 'assets/custom/models/ground_modules'
CONTRACT = ROOT / 'assets/custom/source/ground_modules/contract.json'
MATERIAL = 'GroundModuleRecess'
JSON_CHUNK = 0x4E4F534A


def chunks(raw):
    magic, version, length = struct.unpack_from('<4sII', raw)
    if magic != b'glTF' or version != 2 or length != len(raw):
        raise ValueError('Expected a complete GLB 2 file')
    result, cursor = [], 12
    while cursor < length:
        size, kind = struct.unpack_from('<II', raw, cursor)
        payload = raw[cursor + 8:cursor + 8 + size]
        if size % 4 or len(payload) != size:
            raise ValueError('Truncated or unaligned GLB chunk')
        result.append((kind, payload))
        cursor += size + 8
    if cursor != length or sum(kind == JSON_CHUNK for kind, _ in result) != 1:
        raise ValueError('Expected exactly one GLB JSON chunk')
    return result


def update_glb(raw, color):
    original = chunks(raw)
    document = json.loads(next(data for kind, data in original if kind == JSON_CHUNK))
    updated = deepcopy(document)
    matches = [m for m in updated.get('materials', []) if m.get('name') == MATERIAL]
    for material in matches:
        material['pbrMetallicRoughness']['baseColorFactor'] = color
    if updated == document:
        return raw, len(matches)
    encoded = json.dumps(updated, separators=(',', ':'), ensure_ascii=False).encode('utf-8')
    encoded += b' ' * (-len(encoded) % 4)
    body = b''.join(struct.pack('<II', len(data), kind) + data
                    for kind, old in original
                    for data in [encoded if kind == JSON_CHUNK else old])
    result = struct.pack('<4sII', b'glTF', 2, 12 + len(body)) + body
    actual = chunks(result)
    assert [(k, data) for k, data in actual if k != JSON_CHUNK] == [
        (k, data) for k, data in original if k != JSON_CHUNK]
    assert json.loads(next(data for kind, data in actual if kind == JSON_CHUNK)) == updated
    return result, len(matches)


def native_color(bpy, color, apply):
    if Path(bpy.data.filepath).resolve() != SOURCE.resolve():
        raise ValueError('Open the native ground_modules.blend source first')
    material = bpy.data.materials[MATERIAL]
    nodes = [node for node in material.node_tree.nodes if node.type == 'BSDF_PRINCIPLED']
    if len(nodes) != 1 or nodes[0].inputs['Base Color'].is_linked:
        raise ValueError('Expected one unlinked Principled backing base color')
    shader_color = nodes[0].inputs['Base Color']
    before = tuple(shader_color.default_value)
    changed = max(abs(a - b) for a, b in zip(before, color)) > 1e-6 or max(
        abs(a - b) for a, b in zip(material.diffuse_color, color)) > 1e-6
    if changed and not apply:
        raise ValueError('Native backing does not match the material contract')
    if apply and changed:
        shader_color.default_value = color
        material.diffuse_color = color
        previous = bpy.context.preferences.filepaths.save_version
        try:
            bpy.context.preferences.filepaths.save_version = 0
            result = bpy.ops.wm.save_as_mainfile(filepath=str(SOURCE))
            assert 'FINISHED' in result
        finally:
            bpy.context.preferences.filepaths.save_version = previous
    return {'native_material_verified': True, 'native_material_changed': bool(changed),
            'previous_base_color': before, 'material_users': material.users}


def main():
    arguments = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else sys.argv[1:]
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--apply', action='store_true')
    mode.add_argument('--verify', action='store_true')
    parser.add_argument('--backup-dir', type=Path, default=ROOT / 'out/ground-material/pre-recess-fix')
    parser.add_argument('--output', type=Path, default=ROOT / 'out/ground-material/recess-material-update.json')
    args = parser.parse_args(arguments)
    contract = json.loads(CONTRACT.read_text(encoding='utf-8'))
    color = contract['recessMaterialBaseColorFactor']
    assert contract['recessMaterialRevision'] == 1 and len(color) == 4
    assert all(isinstance(v, (float, int)) and 0 <= v <= 1 for v in color) and color[3] == 1
    try:
        import bpy
    except ImportError:
        bpy = None
    if args.apply and bpy is None:
        raise ValueError('Apply inside Blender so the native source is updated together with exports')
    paths = sorted(MODELS.glob('*.glb'))
    assert len(paths) == 240
    planned = [(path, path.read_bytes()) for path in paths]
    updates = [(path, raw, *update_glb(raw, color)) for path, raw in planned]
    assert sum(count for _, _, _, count in updates) == 200
    if args.apply:
        backup = args.backup_dir.resolve()
        if not backup.is_relative_to((ROOT / 'out').resolve()):
            raise ValueError('Backup must stay within workspace out/')
        backup.mkdir(parents=True, exist_ok=True)
        if not (backup / SOURCE.name).exists():
            shutil.copy2(SOURCE, backup / SOURCE.name)
        for path, raw in planned:
            destination = backup / 'models' / path.name
            destination.parent.mkdir(parents=True, exist_ok=True)
            if not destination.exists():
                destination.write_bytes(raw)
    report = native_color(bpy, color, args.apply) if bpy else {'native_material_verified': False}
    rows = []
    for path, original, updated, count in updates:
        changed = original != updated
        if changed and not args.apply:
            raise ValueError(f'{path.name}: backing color differs from contract')
        if changed:
            temporary = path.with_suffix('.material-tmp.glb')
            temporary.write_bytes(updated)
            temporary.replace(path)
        rows.append({'file': path.relative_to(ROOT).as_posix(), 'backing_materials': count,
                     'changed': changed, 'binary_chunks_unchanged': True,
                     'sha256_before': hashlib.sha256(original).hexdigest(),
                     'sha256_after': hashlib.sha256(updated).hexdigest()})
    report.update({'status': 'PASS', 'base_color_factor': color,
                   'models': rows, 'models_checked': len(rows),
                   'models_changed': sum(row['changed'] for row in rows),
                   'backing_materials': sum(row['backing_materials'] for row in rows)})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({key: value for key, value in report.items() if key != 'models'}))


if __name__ == '__main__':
    main()
