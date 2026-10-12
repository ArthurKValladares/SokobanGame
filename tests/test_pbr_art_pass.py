"""Ownership and publication fixtures; never regenerate repository assets."""

from __future__ import annotations

from contextlib import ExitStack, redirect_stderr, redirect_stdout
import base64
import copy
import io
import json
from pathlib import Path
import sys
import struct
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import make_pbr_art_pass as ART
import pbr_art_ownership as OWNERSHIP


def fixture() -> tuple[dict, dict]:
    config = {
        "format": 2,
        "models": [
            {"name": "Generated", "ownership": "generated",
             "source": "vendor/model.gltf", "surface_rule": "stone"},
            {"name": "Retained", "ownership": "retained",
             "source": "custom/models/native.glb", "skip": "Artist-authored geometry"},
        ],
        "attachments": [
            {"name": "GeneratedAttachment", "ownership": "generated",
             "model": "Generated", "slot": 0,
             "source": "vendor/attachment.gltf", "surface_rule": "stone"},
        ],
        "legacy_models": [
            {"name": "LegacyGroundRock01", "ownership": "generated",
             "source": "vendor/legacy.gltf", "surface_rule": "stone"},
        ],
        "profiles": [],
    }
    manifest = {
        "format": 1,
        "textures": [],
        "models": [
            {"name": "Generated", "path": "vendor/model.gltf", "geometry": "skinned",
             "material": {"mode": "none"}, "role": "fixture",
             "attachments": [
                 {"path": "vendor/attachment.gltf", "bone": "Hand", "scale": 0.5},
                 {"path": "vendor/attachment.gltf", "bone": "OtherHand", "scale": 0.75},
             ]},
            {"name": "Retained", "path": "custom/models/native.glb",
             "material": {"mode": "none", "authoredSettings": {"roughness": 0.8}},
             "scale": 1.25, "artistMetadata": {"revision": "native", "reviewed": True},
             "attachments": [
                 {"path": "vendor/attachment.gltf", "bone": "KeepMe", "offset": [1, 2, 3]},
             ]},
        ],
    }
    return config, manifest


def triangle_document() -> dict:
    values = (0, 0, 0, 1, 0, 0, 0, 1, 0,
              0, 0, 1, 0, 0, 1, 0, 0, 1,
              0, 0, 1, 0, 0, 1)
    binary = struct.pack("<24f", *values)
    return {
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": len(binary),
                     "uri": "data:application/octet-stream;base64," +
                            base64.b64encode(binary).decode("ascii")}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": 36},
            {"buffer": 0, "byteOffset": 36, "byteLength": 36},
            {"buffer": 0, "byteOffset": 72, "byteLength": 24},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
             "min": [0, 0, 0], "max": [1, 1, 0]},
            {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
        ],
        "materials": [{"pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1]}}],
        "meshes": [{"primitives": [{"attributes": {
            "POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "material": 0}]}],
        "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0,
    }


class PbrOwnershipTests(unittest.TestCase):
    def test_generated_output_and_allowed_active_paths(self):
        config, manifest = fixture()
        self.assertEqual(OWNERSHIP.generated_output(config["models"][0]),
                         "custom/pbr/models/Generated.glb")
        before = copy.deepcopy((config, manifest))
        definitions = OWNERSHIP.validate_ownership(config, manifest)
        self.assertEqual(set(definitions), {"Generated", "Retained"})
        self.assertEqual((config, manifest), before)
        manifest["models"][0]["path"] = "custom/pbr/models/Generated.glb"
        manifest["models"][0]["attachments"][0]["path"] = \
            "custom/pbr/models/GeneratedAttachment.glb"
        OWNERSHIP.validate_ownership(config, manifest)

    def test_missing_duplicate_and_conflicting_ownership_is_rejected(self):
        def retained_path_collision(config, manifest, path="custom/pbr/models/Generated.glb"):
            config["models"][1]["source"] = path
            manifest["models"][1]["path"] = config["models"][1]["source"]

        cases = [
            ("old format", lambda c, m: c.update(format=1)),
            ("missing model", lambda c, m: c["models"].pop()),
            ("unregistered model", lambda c, m: c["models"].append(
                {"name": "Unknown", "ownership": "retained", "source": "native.glb", "skip": "Keep"})),
            ("duplicate model", lambda c, m: c["models"].append(copy.deepcopy(c["models"][0]))),
            ("duplicate manifest model", lambda c, m: m["models"].append(copy.deepcopy(m["models"][0]))),
            ("missing ownership", lambda c, m: c["models"][0].pop("ownership")),
            ("unknown ownership", lambda c, m: c["models"][0].update(ownership="automatic")),
            ("retained missing reason", lambda c, m: c["models"][1].pop("skip")),
            ("retained blank reason", lambda c, m: c["models"][1].update(skip="")),
            ("generated skipped", lambda c, m: c["models"][0].update(skip="Keep this")),
            ("generated active path conflict", lambda c, m: m["models"][0].update(path="custom/artist.glb")),
            ("retained active path conflict", lambda c, m: m["models"][1].update(path="custom/other.glb")),
            ("retained output overwritten", retained_path_collision),
            ("retained trailing-dot output alias", lambda c, m: retained_path_collision(
                c, m, "custom/pbr/models/Generated.glb.")),
            ("retained trailing-space output alias", lambda c, m: retained_path_collision(
                c, m, "custom/pbr/models/Generated.glb ")),
            ("retained alternate data stream", lambda c, m: retained_path_collision(
                c, m, "custom/pbr/models/Generated.glb:artist")),
            ("retained trailing-dot directory alias", lambda c, m: retained_path_collision(
                c, m, "custom/pbr/models./Generated.glb")),
            ("retained trailing-space directory alias", lambda c, m: retained_path_collision(
                c, m, "custom/pbr/models /Generated.glb")),
            ("generated input overwritten by attachment", lambda c, m: (
                c["models"][0].update(source=OWNERSHIP.generated_output(c["attachments"][0])),
                m["models"][0].update(path=c["models"][0]["source"]))),
            ("legacy input overwritten", lambda c, m: c["legacy_models"][0].update(
                source=OWNERSHIP.generated_output(c["models"][0]))),
            ("missing attachment owner", lambda c, m: c["attachments"][0].pop("model")),
            ("unknown attachment owner", lambda c, m: c["attachments"][0].update(model="Unknown")),
            ("retained attachment owner", lambda c, m: c["attachments"][0].update(model="Retained")),
            ("missing attachment slot", lambda c, m: c["attachments"][0].pop("slot")),
            ("negative attachment slot", lambda c, m: c["attachments"][0].update(slot=-1)),
            ("missing attachment slot target", lambda c, m: c["attachments"][0].update(slot=99)),
            ("duplicate attachment slot", lambda c, m: c["attachments"].append(
                dict(c["attachments"][0], name="OtherAttachment"))),
            ("attachment output collision", lambda c, m: c["attachments"][0].update(name="Generated")),
            ("attachment active path conflict", lambda c, m: m["models"][0]["attachments"][0].update(
                path="custom/artist-attachment.glb")),
            ("legacy active model name", lambda c, m: c["legacy_models"][0].update(name="Generated")),
            ("legacy name without prefix", lambda c, m: c["legacy_models"][0].update(name="OldRock")),
            ("duplicate legacy output", lambda c, m: c["legacy_models"].append(copy.deepcopy(c["legacy_models"][0]))),
        ]
        for name, mutate in cases:
            with self.subTest(name=name):
                config, manifest = fixture()
                mutate(config, manifest)
                with self.assertRaises(ValueError):
                    OWNERSHIP.validate_ownership(config, manifest)

    def test_repository_inventory_protects_native_ground_and_artist_models(self):
        config = json.loads((ROOT / "tools/pbr_art_pass.json").read_text(encoding="utf-8"))
        manifest = json.loads((ROOT / "assets/manifest.json").read_text(encoding="utf-8"))
        definitions = OWNERSHIP.validate_ownership(config, manifest)
        specs = {spec["name"]: spec for spec in config["models"]}
        self.assertEqual(set(specs), set(definitions))
        native_ground = {name: model for name, model in definitions.items()
                         if name.startswith("GroundRock")}
        self.assertGreater(len(native_ground), 10)
        for name, model in native_ground.items():
            with self.subTest(name=name):
                self.assertTrue(model["path"].startswith("custom/models/ground_modules/"))
                self.assertEqual(specs[name]["ownership"], "retained")
                self.assertEqual(specs[name]["source"], model["path"])
                self.assertTrue(specs[name]["skip"])
        self.assertEqual(definitions["GroundRock01"]["path"],
                         "custom/models/ground_modules/ground_island_01.glb")
        for name in ["Stone", *(f"WallStone{index:02}" for index in range(1, 9))]:
            self.assertEqual(specs[name]["ownership"], "retained")
            self.assertEqual(specs[name]["source"], definitions[name]["path"])
        legacy = config.get("legacy_models", [])
        self.assertEqual({spec["name"] for spec in legacy},
                         {f"LegacyGroundRock{index:02}" for index in range(1, 11)})
        self.assertTrue(set(definitions).isdisjoint(spec["name"] for spec in legacy))


class PbrArtPassPublicationTests(unittest.TestCase):
    def setUp(self):
        temporary_parent = ROOT / "out/pbr-art-tests"
        temporary_parent.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(
            prefix="ownership_", dir=temporary_parent)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.assets = self.root / "assets"
        self.output = self.assets / "custom/pbr"
        self.output.mkdir(parents=True)
        self.config_path = self.root / "pbr_art_pass.json"
        self.manifest_path = self.assets / "manifest.json"
        self.config, self.manifest = fixture()
        self.write_plan()
        source = {"asset": {"version": "2.0"}, "materials": [
            {"pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1]}}]}
        for relative in ("vendor/model.gltf", "vendor/attachment.gltf", "vendor/legacy.gltf"):
            path = self.assets / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(source), encoding="utf-8")
        self.retained_path = self.assets / "custom/models/native.glb"
        self.retained_path.parent.mkdir(parents=True)
        self.retained_path.write_bytes(b"Retained artist geometry: do not replace")
        Image.new("L", (4, 4), 128).save(self.output / "surface_height_source.png")

    def write_plan(self):
        self.config_path.write_text(json.dumps(self.config), encoding="utf-8")
        self.manifest_path.write_text(json.dumps(self.manifest), encoding="utf-8")

    def snapshot(self):
        return {path.relative_to(self.root).as_posix():
                None if path.is_dir() else path.read_bytes()
                for path in self.root.rglob("*")}

    def author_fixture(self, config, spec):
        relative = OWNERSHIP.generated_output(spec)
        path = self.assets / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"Generated fixture: " + spec["name"].encode("utf-8"))
        return {"name": spec["name"], "source": spec["source"], "output": relative,
                "triangles": 1, "surface_triangles": {"stone": 1}}

    def run_main(self, arguments, **mocks):
        with ExitStack() as stack:
            for name, value in (("ROOT", self.root), ("ASSETS", self.assets),
                                ("CONFIG", self.config_path), ("OUTPUT", self.output)):
                stack.enter_context(patch.object(ART, name, value))
            for name, mock in mocks.items():
                stack.enter_context(patch.object(ART, name, mock))
            stack.enter_context(redirect_stdout(io.StringIO()))
            stack.enter_context(redirect_stderr(io.StringIO()))
            return ART.main(arguments)

    def test_invalid_ownership_fails_before_source_reads_or_any_writes(self):
        cases = [
            ("missing", lambda: self.config["models"].pop()),
            ("duplicate", lambda: self.config["models"].append(copy.deepcopy(self.config["models"][0]))),
            ("conflicting", lambda: self.manifest["models"][0].update(path="custom/artist.glb")),
        ]
        for name, mutate in cases:
            with self.subTest(name=name):
                self.config, self.manifest = fixture()
                mutate()
                self.write_plan()
                before = self.snapshot()
                with patch.object(ART, "source_document") as source, \
                        patch.object(ART, "make_maps") as maps, \
                        patch.object(ART, "author_model") as author, \
                        patch.object(ART, "save_text") as save:
                    with self.assertRaises((ValueError, SystemExit)):
                        self.run_main(["--update-manifest"])
                    source.assert_not_called()
                    maps.assert_not_called()
                    author.assert_not_called()
                    save.assert_not_called()
                self.assertEqual(self.snapshot(), before)

    def test_check_only_validates_without_maps_models_or_writes(self):
        before = self.snapshot()
        with patch.object(ART, "make_maps") as maps, \
                patch.object(ART, "author_model") as author, \
                patch.object(ART, "save_text") as save:
            self.run_main(["--check-only", "--update-manifest", "--include-legacy"])
            maps.assert_not_called()
            author.assert_not_called()
            save.assert_not_called()
        self.assertEqual(self.snapshot(), before)

    def test_input_preflight_rejects_authored_maps_before_publication(self):
        path = self.assets / self.config["models"][0]["source"]
        path.write_text(json.dumps({"asset": {"version": "2.0"},
                                    "materials": [{"normalTexture": {"index": 0}}]}),
                        encoding="utf-8")
        before = self.snapshot()
        with patch.object(ART, "make_maps") as maps, \
                patch.object(ART, "author_model") as author, \
                patch.object(ART, "save_text") as save:
            with self.assertRaises((ValueError, SystemExit)):
                self.run_main(["--update-manifest"])
            maps.assert_not_called()
            author.assert_not_called()
            save.assert_not_called()
        self.assertEqual(self.snapshot(), before)

    def test_missing_retained_input_fails_before_publication(self):
        self.retained_path.unlink()
        before = self.snapshot()
        with patch.object(ART, "make_maps") as maps, \
                patch.object(ART, "author_model") as author:
            with self.assertRaises((ValueError, FileNotFoundError, SystemExit)):
                self.run_main(["--update-manifest"])
            maps.assert_not_called()
            author.assert_not_called()
        self.assertEqual(self.snapshot(), before)

    def test_missing_height_source_fails_before_publication(self):
        (self.output / "surface_height_source.png").unlink()
        before = self.snapshot()
        with patch.object(ART, "make_maps") as maps, \
                patch.object(ART, "author_model") as author:
            with self.assertRaises(ValueError):
                self.run_main(["--update-manifest"])
            maps.assert_not_called()
            author.assert_not_called()
        self.assertEqual(self.snapshot(), before)

    def test_legacy_sources_are_only_required_when_opted_in(self):
        (self.assets / self.config["legacy_models"][0]["source"]).unlink()
        before = self.snapshot()
        self.run_main(["--check-only"])
        self.assertEqual(self.snapshot(), before)
        with self.assertRaises(FileNotFoundError):
            self.run_main(["--check-only", "--include-legacy"])
        self.assertEqual(self.snapshot(), before)

    def test_generated_publication_preserves_retained_model_and_shared_attachments(self):
        retained = copy.deepcopy(self.manifest["models"][1])
        retained_bytes = self.retained_path.read_bytes()
        generated_before = copy.deepcopy(self.manifest["models"][0])
        with patch.object(ART, "make_maps") as maps, \
                patch.object(ART, "author_model", side_effect=self.author_fixture) as author:
            self.run_main(["--update-manifest"])
            maps.assert_called_once()
            self.assertEqual([call.args[1]["name"] for call in author.call_args_list],
                             ["Generated", "GeneratedAttachment"])
        published = json.loads(self.manifest_path.read_text(encoding="utf-8"))
        self.assertEqual(published["models"][1], retained)
        self.assertEqual(self.retained_path.read_bytes(), retained_bytes)
        expected = generated_before
        expected["path"] = "custom/pbr/models/Generated.glb"
        expected.pop("material")
        expected["attachments"][0]["path"] = "custom/pbr/models/GeneratedAttachment.glb"
        self.assertEqual(published["models"][0], expected)
        self.assertFalse((self.output / "models/LegacyGroundRock01.glb").exists())
        OWNERSHIP.validate_ownership(self.config, published)

    def test_legacy_generation_requires_explicit_opt_in_and_never_registers_models(self):
        retained = copy.deepcopy(self.manifest["models"][1])
        with patch.object(ART, "make_maps"), \
                patch.object(ART, "author_model", side_effect=self.author_fixture) as author:
            self.run_main(["--update-manifest", "--include-legacy"])
            self.assertEqual({call.args[1]["name"] for call in author.call_args_list},
                             {"Generated", "GeneratedAttachment", "LegacyGroundRock01"})
        published = json.loads(self.manifest_path.read_text(encoding="utf-8"))
        self.assertEqual({model["name"] for model in published["models"]},
                         {"Generated", "Retained"})
        self.assertEqual(published["models"][1], retained)
        self.assertTrue((self.output / "models/LegacyGroundRock01.glb").is_file())

    def test_external_manifest_edit_during_authoring_cancels_publication(self):
        external = copy.deepcopy(self.manifest)
        external["models"][1]["artistMetadata"]["revision"] = "Outside edit"
        external_bytes = (json.dumps(external, indent=2) + "\n").encode("utf-8")
        inventory = self.output / "inventory.json"
        inventory.write_bytes(b'{"previous":"inventory"}\n')
        previous_inventory = inventory.read_bytes()

        def concurrent_edit(config, spec):
            row = self.author_fixture(config, spec)
            self.manifest_path.write_bytes(external_bytes)
            return row

        with patch.object(ART, "make_maps"), \
                patch.object(ART, "author_model", side_effect=concurrent_edit), \
                patch.object(ART, "save_text") as save:
            with self.assertRaisesRegex(ValueError, "Manifest changed"):
                self.run_main(["--update-manifest"])
            save.assert_not_called()
        self.assertEqual(self.manifest_path.read_bytes(), external_bytes)
        self.assertEqual(inventory.read_bytes(), previous_inventory)

    def test_real_triangle_pipeline_publishes_without_touching_retained_assets(self):
        self.config["profiles"] = [{"name": "stone", "normal_gain": 1,
                                    "roughness": 0.9, "metallic": 0, "cavity": 0.02}]
        self.config["material_overrides"] = {}
        self.write_plan()
        for relative in ("vendor/model.gltf", "vendor/attachment.gltf", "vendor/legacy.gltf"):
            (self.assets / relative).write_text(json.dumps(triangle_document()), encoding="utf-8")
        retained = copy.deepcopy(self.manifest["models"][1])
        retained_bytes = self.retained_path.read_bytes()
        self.run_main(["--update-manifest"])

        published = json.loads(self.manifest_path.read_text(encoding="utf-8"))
        self.assertEqual(published["models"][1], retained)
        self.assertEqual(self.retained_path.read_bytes(), retained_bytes)
        self.assertEqual(published["models"][0]["path"], "custom/pbr/models/Generated.glb")
        self.assertEqual(published["models"][0]["attachments"][0]["path"],
                         "custom/pbr/models/GeneratedAttachment.glb")
        self.assertEqual(published["models"][0]["attachments"][1],
                         self.manifest["models"][0]["attachments"][1])
        for name in ("Generated", "GeneratedAttachment"):
            path = self.output / f"models/{name}.glb"
            self.assertTrue(path.is_file())
            document = ART.Document(path)
            primitive = document.data["meshes"][0]["primitives"][0]
            attributes = primitive["attributes"]
            self.assertEqual(document.accessor(attributes["POSITION"]).tolist(),
                             [[0, 0, 0], [1, 0, 0], [0, 1, 0]])
            self.assertEqual(document.accessor(attributes["TEXCOORD_0"]).tolist(),
                             [[0, 0], [1, 0], [0, 1]])
            self.assertEqual(document.accessor(attributes["TEXCOORD_1"]).shape, (3, 2))
            self.assertEqual(document.accessor(attributes["TANGENT"]).shape, (3, 4))
            material = document.data["materials"][0]
            self.assertIn("normalTexture", material)
            self.assertIn("occlusionTexture", material)
            self.assertIn("metallicRoughnessTexture", material["pbrMetallicRoughness"])
            self.assertTrue(all((path.parent / image["uri"]).is_file()
                                for image in document.data["images"]))
        self.assertFalse((self.output / "models/LegacyGroundRock01.glb").exists())
        inventory = json.loads((self.output / "inventory.json").read_text(encoding="utf-8"))
        self.assertEqual(inventory["legacy_models"], [])
        OWNERSHIP.validate_ownership(self.config, published)


if __name__ == "__main__":
    unittest.main()
