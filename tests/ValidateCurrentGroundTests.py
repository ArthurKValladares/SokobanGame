"""Tracked-only evidence and destructive current native-ground audit fixtures."""
from __future__ import annotations

from copy import deepcopy
import gzip
import json
from pathlib import Path
import sys
import struct
import tempfile
from types import SimpleNamespace
import unittest

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/"tools"))
import validate_current_ground as audit
from ValidateGroundBottomsTests import boulder_fixture
from ValidateRegularReliefTests import export_fixture


def temporary_directory(prefix):
    base = ROOT/"out/test-output/current-ground"
    base.mkdir(parents=True, exist_ok=True)
    return tempfile.TemporaryDirectory(prefix=prefix, dir=base)


def corner_fixture():
    points = np.asarray([[0, 0, 0], [1, 0, 0], [1, 1, 0], [0, 1, 0]], dtype=float)
    polygons = np.asarray([[0, 1, 2], [0, 2, 3]])
    corners = points[polygons].reshape(-1, 3)
    body = {"points": points.tolist(), "polygons": polygons.tolist(),
            "cornerNormals": [[0, 0, 1]]*6, "uvLayerNames": ["UVMap", "UVMap.001"],
            "uvLayers": {"UVMap": corners[:, :2].tolist(), "UVMap.001": (corners[:, :2]*2+.3).tolist()},
            "faceMaterials": ["stone"]*2}
    uv = {f"TEXCOORD_{index}": np.column_stack((np.asarray(body["uvLayers"][name])[:, 0],
                                              1-np.asarray(body["uvLayers"][name])[:, 1]))
          for index, name in enumerate(body["uvLayerNames"])}
    primitive = audit.native.Primitive({"name": "stone"}, corners, np.asarray(body["cornerNormals"], dtype=float),
                                       uv["TEXCOORD_0"], np.arange(6).reshape(-1, 3), 1)
    return body, [primitive], [uv]


def add_corner_streams(body, primitives, streams):
    records = audit.export_corner_records(primitives, streams)
    # This fixture's source polygons and primitive export triangles share order.
    body["cornerNormals"] = np.concatenate([record["normals"] for record in records]).tolist()
    body["faceMaterials"] = [record["material"] for record in records]
    body["uvLayerNames"] = ["UVMap", "UVMap.001"]
    body["uvLayers"] = {}
    for index, name in enumerate(body["uvLayerNames"]):
        values = np.concatenate([record["uv"][f"TEXCOORD_{index}"] for record in records]).copy()
        values[:, 1] = 1-values[:, 1]
        body["uvLayers"][name] = values.tolist()


class CurrentGroundEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = temporary_directory("current_ground_")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.directory = self.root/"assets/custom/source/ground_modules"
        self.directory.mkdir(parents=True)
        self.source = self.directory.parent/"ground_modules.blend"
        self.source.write_bytes(b"synthetic native source")
        self.contract = {"source": "../ground_modules.blend", "capSurfaces": "cap_surfaces.json",
                         "squareCapSurfaces": "square_cap_surfaces.json"}
        self.contract_path = self.directory/"contract.json"
        self.contract_path.write_text(json.dumps(self.contract))
        for name in ("cap_surfaces.json", "square_cap_surfaces.json"):
            (self.directory/name).write_text("{}")
        self.rows = [{"bottom": bottom, "squareTop": square, "shape": shape, "variant": variant,
                      "exposedSideMask": mask, "body": {}, "reference": {}, "exportSha256": "1"*64}
                     for bottom in (False, True) for square in (False, True)
                     for shape, mask in audit.rock.FAMILIES for variant in range(10)]
        self.bundle = {"format": 1, "source": {"path": "../ground_modules.blend", "sha256": audit.native.digest(self.source)},
                       "models": self.rows, "caps": [{} for _ in range(310)],
                       "capTableSha256": {name: audit.native.digest(self.directory/name)
                                          for name in ("cap_surfaces.json", "square_cap_surfaces.json")}}
        # An external capture path must still resolve source relative to contract.
        self.path = self.root/"fresh-capture.json.gz"

    def load(self):
        with gzip.open(self.path, "wt", encoding="utf-8") as stream:
            json.dump(self.bundle, stream)
        return audit.load_evidence(self.path, self.contract_path, self.contract, self.root)

    def test_complete_evidence_uses_no_ignored_history_and_external_capture_path(self):
        bundle, rows = self.load()
        self.assertEqual(len(rows), 240)
        self.assertEqual(len(bundle["caps"]), 310)
        self.assertFalse((self.root/"out").exists())

    def test_changed_native_source_and_cap_tables_fail_binding(self):
        self.source.write_bytes(b"changed native mesh")
        with self.assertRaisesRegex(AssertionError, "source changed"):
            self.load()
        self.bundle["source"]["sha256"] = audit.native.digest(self.source)
        (self.directory/"cap_surfaces.json").write_text('{"changed":true}')
        with self.assertRaisesRegex(AssertionError, "cap table changed"):
            self.load()

    def test_incomplete_duplicate_or_invalid_identity_cannot_hide_models(self):
        for mutate in (lambda rows: rows.pop(), lambda rows: rows.append(deepcopy(rows[0])),
                       lambda rows: rows[0].update(variant=-1), lambda rows: rows[0].update(bottom=1)):
            with self.subTest(mutate=mutate):
                self.bundle["models"] = deepcopy(self.rows)
                mutate(self.bundle["models"])
                with self.assertRaises(AssertionError):
                    self.load()

    def test_missing_export_binding_and_frozen_reference_are_rejected(self):
        self.bundle["models"][0].pop("reference")
        with self.assertRaisesRegex(AssertionError, "frozen source"):
            self.load()
        self.bundle["models"][0]["reference"] = {}
        self.bundle["models"][0]["exportSha256"] = "not a digest"
        with self.assertRaisesRegex(AssertionError, "export binding"):
            self.load()

    def test_runtime_manifest_requires_each_canonical_path_and_authoring_flags(self):
        prefixes = ("GroundRock", "GroundRockSquare", "GroundRockBottom", "GroundRockBottomSquare")
        manifest = {"models": []}
        identities = []
        for profile, prefix in enumerate(prefixes):
            for shape, _ in audit.rock.FAMILIES:
                for variant in range(10):
                    identity = (profile >= 2, profile % 2 == 1, shape, variant)
                    identities.append(identity)
                    name = prefix+("" if shape == "island" else shape.title())+f"{variant+1:02d}"
                    manifest["models"].append({"name": name, "path": "custom/models/ground_modules/"+audit.model_name(identity),
                                                "preserveSourceScale": True})
        self.assertEqual(audit.audit_manifest(manifest, identities)["runtime_native_models"], 240)
        for fields in ({"path": "custom/pbr/models/GroundRock01.glb"}, {"preserveSourceScale": False},
                       {"geometry": "skinned"}, {"rotateHalfTurn": True}, {"attachments": [{}]},
                       {"material": {"mode": "none"}}):
            with self.subTest(fields=fields):
                changed = deepcopy(manifest)
                changed["models"][0].update(fields)
                with self.assertRaisesRegex(AssertionError, "manifest path/scale"):
                    audit.audit_manifest(changed, identities)
        for mutation in (lambda models: models.pop(), lambda models: models.append(deepcopy(models[0]))):
            changed = deepcopy(manifest)
            mutation(changed["models"])
            with self.assertRaisesRegex(AssertionError, "missing or duplicate"):
                audit.audit_manifest(changed, identities)

    def test_runtime_unsupported_uris_and_asset_escape_are_rejected(self):
        assets = self.root/"assets"
        image = assets/"texture.png"
        image.write_bytes(b"fixture")
        self.assertEqual(audit.asset_path(self.directory, "../../../texture.png", assets), image.resolve())
        for uri in ("../../../texture%2epng", "..\\..\\..\\texture.png", str(image.resolve()),
                    "../../../texture.png?x", "../../../texture.png#x", "../../../texture.png:stream",
                    "../../../../fresh-capture.json.gz"):
            with self.subTest(uri=uri), self.assertRaises(AssertionError):
                audit.asset_path(self.directory, uri, assets)

    def test_native_glb_rejects_external_buffers_before_loading_dependencies(self):
        path = self.root/"fixture.glb"
        for uri in (None, "missing.bin", "outside%2ebin", "..\\outside.bin", "../../outside.bin"):
            metadata = {"asset": {"version": "2.0"}, "buffers": [{"byteLength": 4}]}
            if uri is not None:
                metadata["buffers"][0]["uri"] = uri
            encoded = json.dumps(metadata).encode()
            encoded += b" "*((-len(encoded)) % 4)
            chunks = struct.pack("<II", len(encoded), 0x4E4F534A)+encoded+struct.pack("<II", 4, 0x004E4942)+b"1234"
            path.write_bytes(struct.pack("<III", 0x46546C67, 2, 12+len(chunks))+chunks)
            if uri is None:
                self.assertEqual(audit.read_native_document(path).buffers, [b"1234"])
            else:
                with self.subTest(uri=uri), self.assertRaisesRegex(AssertionError, "embedded geometry buffer"):
                    audit.read_native_document(path)


class CurrentGroundCornerTests(unittest.TestCase):
    def test_all_source_corner_streams_match_with_uv_origin_conversion(self):
        body, primitives, streams = corner_fixture()
        audit.match_corner_records(audit.export_corner_records(primitives, streams),
                                   audit.source_corner_records(body, "fixture"), "fixture")
        primitives[0].triangles = primitives[0].triangles[::-1, [1, 2, 0]]
        audit.match_corner_records(audit.export_corner_records(primitives, streams),
                                   audit.source_corner_records(body, "fixture"), "fixture")

    def test_geometry_winding_normals_material_and_each_uv_chart_are_independent(self):
        for kind in ("position", "winding", "normal", "material", "uv0", "uv1", "missing_uv", "missing_triangle"):
            body, primitives, streams = corner_fixture()
            mesh = primitives[0]
            if kind == "position": mesh.points[0, 0] += .001
            if kind == "winding": mesh.triangles[0] = mesh.triangles[0, ::-1]
            if kind == "normal": mesh.normals[0] = [.001, 0, np.sqrt(1-.001**2)]
            if kind == "material": mesh.material["name"] = "different stone"
            if kind in ("uv0", "uv1"): streams[0][f"TEXCOORD_{kind[-1]}"][0, 0] += .001
            if kind == "missing_uv": streams[0].pop("TEXCOORD_1")
            if kind == "missing_triangle": mesh.triangles = mesh.triangles[:1]
            with self.subTest(kind=kind), self.assertRaises(AssertionError):
                audit.match_corner_records(audit.export_corner_records(primitives, streams),
                                           audit.source_corner_records(body, "fixture"), "fixture")

    def test_only_measured_normal_precision_receives_the_larger_bound(self):
        body, primitives, streams = corner_fixture()
        primitives[0].normals[0] = [8e-5, 0, np.sqrt(1-(8e-5)**2)]
        audit.match_corner_records(audit.export_corner_records(primitives, streams),
                                   audit.source_corner_records(body, "fixture"), "fixture")
        primitives[0].normals[0] = [2e-4, 0, np.sqrt(1-(2e-4)**2)]
        with self.assertRaises(AssertionError):
            audit.match_corner_records(audit.export_corner_records(primitives, streams),
                                       audit.source_corner_records(body, "fixture"), "fixture")

    def test_native_layer_order_and_nonfinite_corner_data_cannot_be_ignored(self):
        for kind in ("missing_order", "duplicate_order", "nan_uv", "short_normals", "missing_material"):
            body, _, _ = corner_fixture()
            if kind == "missing_order": body.pop("uvLayerNames")
            if kind == "duplicate_order": body["uvLayerNames"] = ["UVMap", "UVMap"]
            if kind == "nan_uv": body["uvLayers"]["UVMap.001"][0][0] = float("nan")
            if kind == "short_normals": body["cornerNormals"].pop()
            if kind == "missing_material": body["faceMaterials"].pop()
            with self.subTest(kind=kind), self.assertRaises(AssertionError):
                audit.source_corner_records(body, "fixture")

    def test_native_relief_preserves_unchanged_corners_and_parent_uv_interpolation(self):
        current, old, streams, old_streams, body, reference = export_fixture()
        add_corner_streams(body, current, streams)
        add_corner_streams(reference, old, old_streams)
        audit.audit_relief_corner_streams(body, reference, "fixture")
        changed = deepcopy(body)
        center = changed["reliefFaces"][0]["center"]
        corner = next(index for index, vertex in enumerate(np.asarray(changed["polygons"]).reshape(-1)) if vertex == center)
        changed["uvLayers"]["UVMap.001"][corner][0] += .01
        with self.assertRaisesRegex(AssertionError, "parent interpolation"):
            audit.audit_relief_corner_streams(changed, reference, "fixture")
        changed = deepcopy(body)
        changed["cornerNormals"][0] = [.01, 0, -np.sqrt(1-.01**2)]
        with self.assertRaisesRegex(AssertionError, "unchanged relief corners"):
            audit.audit_relief_corner_streams(changed, reference, "fixture")

    def test_native_cap_geometry_normal_material_and_coverage_match(self):
        cap = {"object": "cap", "points": np.asarray([[0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1]]),
               "faces": [[0, 1, 2, 3]], "normals": np.asarray([[0, 0, 1]])}
        source = {"object": "cap", "points": cap["points"].tolist(), "polygons": cap["faces"],
                  "properties": {"GroundModuleVariant": 0, "ExposedSideMask": 15, "ConcaveCorners": 0},
                  "cornerNormals": [[0, 0, 1]]*4, "uvLayerNames": [], "uvLayers": {},
                  "faceMaterials": ["GroundModuleCap"]}
        self.assertEqual(audit.audit_caps_source([source], {(0, 0, 0): cap}, {})["native_caps_crosschecked"], 1)
        for kind in ("position", "normal", "material", "identity", "duplicate", "missing"):
            changed = deepcopy(source)
            rows = [changed]
            if kind == "position": changed["points"][0][2] += .001
            if kind == "normal": changed["cornerNormals"][0] = [0, 0, -1]
            if kind == "material": changed["faceMaterials"] = ["unrelated"]
            if kind == "identity": changed["properties"]["GroundModuleVariant"] = 1
            if kind == "duplicate": rows.append(deepcopy(changed))
            if kind == "missing": rows.clear()
            with self.subTest(kind=kind), self.assertRaises(AssertionError):
                audit.audit_caps_source(rows, {(0, 0, 0): cap}, {})

    def test_boulder_rooting_rejects_detached_closed_volumes(self):
        roles = boulder_fixture()
        masses = {name: audit.rock.convex_rock(faces, name) for name, faces in roles.items() if name.startswith("BoulderMass")}
        audit.rooted_masses(masses, roles["IslandCore"], "fixture")
        masses["BoulderMass01"] = audit.rock.convex_rock(roles["BoulderMass01"]+np.asarray([5, 0, 0]), "detached")
        with self.assertRaisesRegex(AssertionError, "not rooted"):
            audit.rooted_masses(masses, roles["IslandCore"], "fixture")


class CurrentGroundMaterialTests(unittest.TestCase):
    def setUp(self):
        self.temp = temporary_directory("current_ground_material_")
        self.addCleanup(self.temp.cleanup)
        self.assets = Path(self.temp.name)/"assets"
        (self.assets/"models").mkdir(parents=True)
        (self.assets/"textures").mkdir()
        for name in ("albedo", "normal", "orm"):
            Image.new("RGB", (2, 2), (128, 128, 255)).save(self.assets/f"textures/{name}.png")
        self.material = {"name": "stone", "pbrMetallicRoughness": {"baseColorFactor": [.93, .92, .9, 1],
                         "baseColorTexture": {"index": 0}, "metallicRoughnessTexture": {"index": 2}},
                         "normalTexture": {"index": 1}, "occlusionTexture": {"index": 2, "strength": .6}}
        self.original = SimpleNamespace(path=self.assets/"models/original.glb", data={
            "materials": [deepcopy(self.material)], "textures": [{"source": i, "sampler": 0} for i in range(3)],
            "images": [{"uri": f"../textures/{name}.png"} for name in ("albedo", "normal", "orm")],
            "samplers": [{"magFilter": 9729, "minFilter": 9987}]})
        self.current = deepcopy(self.original)
        self.current.path = self.assets/"models/current.glb"
        self.current.data["materials"].append({"name": "GroundModuleRecess", "doubleSided": True,
            "pbrMetallicRoughness": {"baseColorFactor": [.61, .48, .28, 1], "metallicFactor": 0, "roughnessFactor": .95}})
        self.contract = {"recessMaterialBaseColorFactor": [.61, .48, .28, 1]}

    def test_current_pbr_accepts_float32_and_omitted_defaults(self):
        self.original.data["materials"][0]["normalTexture"].update(texCoord=0, scale=1)
        self.original.data["materials"][0]["pbrMetallicRoughness"].update(metallicFactor=1, roughnessFactor=1)
        self.current.data["materials"][0]["name"] = "stone.001"
        self.current.data["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"][0] = .9300000071525574
        report = audit.audit_materials(self.current, self.original, self.contract, self.assets)
        self.assertEqual(report["decoded_shared_pngs"], 3)

    def test_map_sampler_shader_and_recess_contract_cannot_change(self):
        original = deepcopy(self.current)
        for kind in ("sampler", "uv_binding", "plate_color", "unknown_material", "recess_color", "recess_glow", "recess_roughness"):
            self.current = deepcopy(original)
            materials = self.current.data["materials"]
            if kind == "sampler": self.current.data["samplers"][0]["wrapS"] = 33071
            if kind == "uv_binding": materials[0]["normalTexture"]["texCoord"] = 1
            if kind == "plate_color": materials[0]["pbrMetallicRoughness"]["baseColorFactor"][0] += .1
            if kind == "unknown_material": materials[0]["name"] = "foreign"
            if kind == "recess_color": materials[1]["pbrMetallicRoughness"]["baseColorFactor"][0] += .1
            if kind == "recess_glow": materials[1]["emissiveFactor"] = [1, 0, 0]
            if kind == "recess_roughness": materials[1]["pbrMetallicRoughness"]["roughnessFactor"] = .2
            with self.subTest(kind=kind), self.assertRaises(AssertionError):
                audit.audit_materials(self.current, self.original, self.contract, self.assets)

    def test_missing_unfinalized_or_corrupt_png_is_rejected(self):
        self.current.data["images"][0]["bufferView"] = 0
        with self.assertRaisesRegex(AssertionError, "finalized"):
            audit.audit_materials(self.current, self.original, self.contract, self.assets)
        self.current.data["images"][0].pop("bufferView")
        (self.assets/"textures/albedo.png").write_bytes(b"not PNG")
        with self.assertRaises(OSError):
            audit.audit_materials(self.current, self.original, self.contract, self.assets)
        (self.assets/"textures/albedo.png").unlink()
        with self.assertRaisesRegex(AssertionError, "Missing or external"):
            audit.audit_materials(self.current, self.original, self.contract, self.assets)


if __name__ == "__main__":
    unittest.main()
