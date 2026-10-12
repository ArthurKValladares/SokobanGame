"""Validate which assets the PBR art pass may generate and publish.

This module performs no filesystem writes and has no image-tool dependencies.
Both authoring and auditing use the same ownership contract.
"""
from __future__ import annotations

import re
from pathlib import PurePosixPath, PureWindowsPath


def asset_path(value, label):
    if (not isinstance(value, str) or not value or "\\" in value or
            PurePosixPath(value).is_absolute() or PureWindowsPath(value).drive or
            any(part in {"", ".", ".."} or part.endswith((".", " ")) or ":" in part
                for part in value.split("/"))):
        raise ValueError(f"{label}: expected an assets-relative path, got {value!r}")
    return value


def generated_output(spec):
    name = spec.get("name")
    if (not isinstance(name, str) or
            re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_-]*", name) is None or
            name.upper() in {"CON", "PRN", "AUX", "NUL"} or
            re.fullmatch(r"(?:COM|LPT)[1-9]", name, re.IGNORECASE) is not None):
        raise ValueError(f"Invalid generated model name: {name!r}")
    return f"custom/pbr/models/{name}.glb"


def named_records(records, label):
    if not isinstance(records, list):
        raise ValueError(f"{label}: expected a list")
    result = {}
    for record in records:
        name = record.get("name") if isinstance(record, dict) else None
        if not isinstance(name, str) or not name:
            raise ValueError(f"{label}: missing model name")
        if name in result:
            raise ValueError(f"{label}: duplicate model name {name}")
        result[name] = record
    return result


def validate_ownership(config, manifest):
    """Reject an incomplete or conflicting plan before any assets are written."""
    if config.get("format") != 2:
        raise ValueError("PBR ownership requires config format 2; review the old inventory first")
    definitions = named_records(manifest.get("models"), "Manifest models")
    models = named_records(config.get("models"), "PBR models")
    missing, extra = definitions.keys() - models.keys(), models.keys() - definitions.keys()
    if missing or extra:
        raise ValueError(f"PBR ownership inventory mismatch: missing={sorted(missing)}, extra={sorted(extra)}")

    # Output paths are case-insensitive on Windows. A retained model or an
    # undeclared attachment must never be overwritten through another identity.
    outputs = {}

    def claim_output(spec, label):
        output = generated_output(spec)
        key = output.casefold()
        if key in outputs:
            raise ValueError(f"Duplicate generated output: {output} ({outputs[key]} and {label})")
        outputs[key] = label
        source = asset_path(spec.get("source"), label)
        if source.casefold() == key:
            raise ValueError(f"{label}: generated output would overwrite its source")
        return output

    for name, spec in models.items():
        source = asset_path(spec.get("source"), name)
        current = asset_path(definitions[name].get("path"), name)
        ownership = spec.get("ownership")
        if ownership == "retained":
            if not isinstance(spec.get("skip"), str) or not spec["skip"].strip():
                raise ValueError(f"{name}: retained ownership requires a skip reason")
            if current != source:
                raise ValueError(f"{name}: retained path changed: expected {source}, got {current}")
        elif ownership == "generated":
            if spec.get("skip") is not None:
                raise ValueError(f"{name}: generated ownership cannot also skip generation")
            output = claim_output(spec, name)
            if current not in {source, output}:
                raise ValueError(f"{name}: conflicting publication path {current}; expected {source} or {output}")
        else:
            raise ValueError(f"{name}: ownership must be generated or retained")

    attachment_slots = set()
    for name, spec in named_records(config.get("attachments", []), "PBR attachments").items():
        if spec.get("ownership") != "generated":
            raise ValueError(f"{name}: attachment ownership must be generated")
        owner, slot = spec.get("model"), spec.get("slot")
        if owner not in definitions or not isinstance(slot, int) or isinstance(slot, bool) or slot < 0:
            raise ValueError(f"{name}: invalid attachment model/slot {owner!r}/{slot!r}")
        if models[owner]["ownership"] != "generated":
            raise ValueError(f"{name}: cannot publish an attachment on retained model {owner}")
        attachments = definitions[owner].get("attachments", [])
        if slot >= len(attachments):
            raise ValueError(f"{name}: missing attachment slot {owner}[{slot}]")
        if (owner, slot) in attachment_slots:
            raise ValueError(f"Duplicate attachment slot: {owner}[{slot}]")
        attachment_slots.add((owner, slot))
        output = claim_output(spec, name)
        current = asset_path(attachments[slot].get("path"), name)
        if current not in {spec["source"], output}:
            raise ValueError(f"{name}: conflicting attachment publication path {current}")

    for name, spec in named_records(config.get("legacy_models", []), "Legacy PBR models").items():
        if name in definitions or not name.startswith("Legacy"):
            raise ValueError(f"{name}: legacy models need a separate unregistered Legacy identity")
        if spec.get("ownership") != "generated":
            raise ValueError(f"{name}: legacy ownership must be generated")
        claim_output(spec, name)

    for spec in [*models.values(), *config.get("attachments", []), *config.get("legacy_models", [])]:
        if spec["source"].casefold() in outputs:
            raise ValueError(f"{spec['name']}: generated output would overwrite an input source")

    for name, definition in definitions.items():
        for slot, attachment in enumerate(definition.get("attachments", [])):
            path = asset_path(attachment.get("path"), f"{name}[{slot}]")
            if (name, slot) not in attachment_slots and path.casefold() in outputs:
                raise ValueError(f"{name}[{slot}]: generated output would overwrite a retained attachment")
    return definitions
