#!/usr/bin/env python3
"""Generates assets/custom/Rig_Medium_Push.glb: a block-pushing walk cycle.

Takes the "Walking_B" animation from Rig_Medium_MovementBasic.glb and replaces
the arm-chain rotation channels with a constant arms-forward pose, so the
character walks with both arms extended straight out in front as if pushing a
block. The pose is not sampled from an existing clip; it is solved with
forward kinematics against the Rogue skeleton: each arm bone's rotation is
adjusted so the bone points along the character's forward axis (see
ARM_TARGETS to tweak). The output GLB contains the full original node
hierarchy and exactly one animation, "Push_Walk", at index 0.

Run from the repository root:  python tools/make_push_animation.py
"""

import copy
import json
import math
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASE_GLB = ROOT / "assets/KayKit Adventurers 2.0/Animations/gltf/Rig_Medium/Rig_Medium_MovementBasic.glb"
SKELETON_GLB = ROOT / "assets/KayKit Adventurers 2.0/Characters/gltf/Rogue.glb"
OUTPUT_GLB = ROOT / "assets/custom/Rig_Medium_Push.glb"

BASE_ANIMATION = "Walking_B"
OUTPUT_ANIMATION = "Push_Walk"

# Bones aligned per side, in chain order, with the target direction expressed
# as (forward, up) weights. (1.0, 0.0) is straight horizontal forward; raise
# or lower the hands by giving `up` a small positive/negative weight.
ARM_TARGETS = [
    ("upperarm", 1.0, 0.0),
    ("lowerarm", 1.0, 0.0),
    ("wrist", 1.0, 0.0),
    ("hand", 1.0, 0.0),
]
SIDES = ["l", "r"]

# How far the arms angle outward, away from the body midline. 0.0 keeps both
# arms exactly parallel (shoulder-width apart); each +0.1 adds roughly 6
# degrees of outward angle per arm. The chain stays straight either way, so
# this only spreads the hands apart. Negative values pinch them inward.
ARM_SPREAD = 0.12

COMPONENT_FLOAT = 5126
TYPE_SIZES = {"SCALAR": 1, "VEC3": 3, "VEC4": 4}

# ---------------------------------------------------------------- quaternions

def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    )


def qconj(q):
    return (-q[0], -q[1], -q[2], q[3])


def qnormalize(q):
    length = math.sqrt(sum(c * c for c in q))
    return tuple(c / length for c in q)


def qrotate(q, v):
    p = (v[0], v[1], v[2], 0.0)
    x, y, z, _ = qmul(qmul(q, p), qconj(q))
    return (x, y, z)


def vnormalize(v):
    length = math.sqrt(sum(c * c for c in v))
    return tuple(c / length for c in v)


def vcross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def vdot(a, b):
    return sum(x * y for x, y in zip(a, b))


def q_from_to(a, b):
    a, b = vnormalize(a), vnormalize(b)
    d = vdot(a, b)
    if d > 0.999999:
        return (0.0, 0.0, 0.0, 1.0)
    if d < -0.999999:
        axis = vcross(a, (1.0, 0.0, 0.0))
        if vdot(axis, axis) < 1e-8:
            axis = vcross(a, (0.0, 1.0, 0.0))
        axis = vnormalize(axis)
        return (axis[0], axis[1], axis[2], 0.0)
    axis = vcross(a, b)
    return qnormalize((axis[0], axis[1], axis[2], 1.0 + d))

# ------------------------------------------------------------------------ glb

def load_glb(path):
    data = path.read_bytes()
    if data[:4] != b"glTF":
        raise SystemExit(f"{path} is not a GLB file")
    json_len, json_type = struct.unpack_from("<I4s", data, 12)
    if json_type != b"JSON":
        raise SystemExit(f"{path}: first chunk is not JSON")
    gltf = json.loads(data[20:20 + json_len])
    bin_off = 20 + json_len + (-json_len) % 4
    bin_len, bin_type = struct.unpack_from("<I4s", data, bin_off)
    if bin_type != b"BIN\x00":
        raise SystemExit(f"{path}: second chunk is not BIN")
    return gltf, bytearray(data[bin_off + 8:bin_off + 8 + bin_len])


def write_glb(path, gltf, binary):
    json_bytes = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    json_bytes += b" " * ((-len(json_bytes)) % 4)
    binary = bytes(binary) + b"\x00" * ((-len(binary)) % 4)
    total = 12 + 8 + len(json_bytes) + 8 + len(binary)
    out = bytearray()
    out += struct.pack("<4sII", b"glTF", 2, total)
    out += struct.pack("<I4s", len(json_bytes), b"JSON") + json_bytes
    out += struct.pack("<I4s", len(binary), b"BIN\x00") + binary
    path.write_bytes(out)


def read_accessor(gltf, binary, index):
    accessor = gltf["accessors"][index]
    if accessor.get("componentType") != COMPONENT_FLOAT:
        raise SystemExit(f"accessor {index}: only float accessors supported")
    view = gltf["bufferViews"][accessor["bufferView"]]
    components = TYPE_SIZES[accessor["type"]]
    stride = view.get("byteStride", components * 4)
    base = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    return [
        struct.unpack_from(f"<{components}f", binary, base + i * stride)
        for i in range(accessor["count"])
    ]


def append_accessor(gltf, binary, values, accessor_type):
    flat = [component for value in values for component in value]
    while len(binary) % 4:
        binary.append(0)
    view_index = len(gltf["bufferViews"])
    gltf["bufferViews"].append({
        "buffer": 0,
        "byteOffset": len(binary),
        "byteLength": len(flat) * 4,
    })
    binary += struct.pack(f"<{len(flat)}f", *flat)
    accessor = {
        "bufferView": view_index,
        "componentType": COMPONENT_FLOAT,
        "count": len(values),
        "type": accessor_type,
    }
    if accessor_type == "SCALAR":
        accessor["min"] = [min(flat)]
        accessor["max"] = [max(flat)]
    gltf["accessors"].append(accessor)
    return len(gltf["accessors"]) - 1

# ------------------------------------------------------------------- skeleton

class Skeleton:
    def __init__(self, gltf):
        self.nodes = gltf["nodes"]
        self.parent = {}
        for index, node in enumerate(self.nodes):
            for child in node.get("children", []):
                self.parent[child] = index
        self.by_name = {node.get("name", ""): index for index, node in enumerate(self.nodes)}
        self.local_rotation = {
            index: tuple(node.get("rotation", (0.0, 0.0, 0.0, 1.0)))
            for index, node in enumerate(self.nodes)
        }

    def world_rotation(self, index):
        rotation = (0.0, 0.0, 0.0, 1.0)
        chain = []
        current = index
        while current is not None:
            chain.append(current)
            current = self.parent.get(current)
        for node in reversed(chain):
            rotation = qmul(rotation, self.local_rotation[node])
        return rotation

    def world_position(self, index):
        position = (0.0, 0.0, 0.0)
        rotation = (0.0, 0.0, 0.0, 1.0)
        chain = []
        current = index
        while current is not None:
            chain.append(current)
            current = self.parent.get(current)
        for node in reversed(chain):
            translation = tuple(self.nodes[node].get("translation", (0.0, 0.0, 0.0)))
            position = tuple(p + o for p, o in zip(position, qrotate(rotation, translation)))
            rotation = qmul(rotation, self.local_rotation[node])
        return position


def solve_arm_pose(skeleton, forward, up):
    """Returns {bone name: local rotation} making both arm chains point at the
    per-bone target directions."""
    pose = {}
    right = vnormalize(vcross(up, forward))
    for side in SIDES:
        # Outward direction for this side, determined from the shoulder's
        # actual position relative to the body midline.
        shoulder = skeleton.world_position(skeleton.by_name[f"upperarm.{side}"])
        outward_sign = 1.0 if vdot(shoulder, right) >= 0.0 else -1.0
        outward = tuple(outward_sign * ARM_SPREAD * r for r in right)
        chain = [f"{name}.{side}" for name, _, _ in ARM_TARGETS]
        children = chain[1:] + [None]
        for (name, forward_weight, up_weight), child in zip(ARM_TARGETS, children):
            bone = f"{name}.{side}"
            index = skeleton.by_name[bone]
            if child is None:
                # Terminal bone: keep whatever direction it has after its
                # parents were aligned (ball hands have no meaningful aim).
                pose[bone] = skeleton.local_rotation[index]
                continue
            child_index = skeleton.by_name[child]
            target = vnormalize(tuple(
                f * forward_weight + u * up_weight + o
                for f, u, o in zip(forward, up, outward)))
            bone_position = skeleton.world_position(index)
            child_position = skeleton.world_position(child_index)
            direction = vnormalize(tuple(c - b for c, b in zip(child_position, bone_position)))
            correction = q_from_to(direction, target)
            parent_rotation = skeleton.world_rotation(skeleton.parent[index])
            local = qmul(
                qmul(qmul(qconj(parent_rotation), correction), parent_rotation),
                skeleton.local_rotation[index])
            skeleton.local_rotation[index] = qnormalize(local)
            pose[bone] = skeleton.local_rotation[index]
    return pose


def verify_pose(skeleton, forward):
    print("pose verification (world space):")
    ok = True
    hands = {}
    for side in SIDES:
        shoulder = skeleton.world_position(skeleton.by_name[f"upperarm.{side}"])
        hand = skeleton.world_position(skeleton.by_name[f"hand.{side}"])
        offset = tuple(h - s for h, s in zip(hand, shoulder))
        length = math.sqrt(vdot(offset, offset))
        forwardness = vdot(offset, forward) / length
        hands[side] = hand
        print(f"  {side}: shoulder {tuple(round(v, 3) for v in shoulder)}"
              f" hand {tuple(round(v, 3) for v in hand)}"
              f" forwardness {forwardness:.3f} rise {offset[1]:+.3f}")
        if forwardness < 0.95:
            ok = False
    separation = math.sqrt(sum((a - b) ** 2 for a, b in zip(hands['l'], hands['r'])))
    symmetry = abs(hands['l'][1] - hands['r'][1]) + abs(
        vdot(hands['l'], forward) - vdot(hands['r'], forward))
    print(f"  hand separation {separation:.3f} (shoulder width 0.424), symmetry error {symmetry:.4f}")
    if symmetry > 0.02 or not ok:
        raise SystemExit("pose verification failed; arms are not both straight forward")

# ----------------------------------------------------------------------- main

def main():
    skeleton_gltf, _ = load_glb(SKELETON_GLB)
    skeleton = Skeleton(skeleton_gltf)

    # Character forward = horizontal direction of the toes relative to the
    # foot (glTF is Y-up).
    foot = skeleton.world_position(skeleton.by_name["foot.r"])
    toes = skeleton.world_position(skeleton.by_name["toes.r"])
    forward = vnormalize((toes[0] - foot[0], 0.0, toes[2] - foot[2]))
    up = (0.0, 1.0, 0.0)
    print(f"character forward axis: {tuple(round(v, 3) for v in forward)}")

    pose = solve_arm_pose(skeleton, forward, up)
    verify_pose(skeleton, forward)

    base_gltf, base_bin = load_glb(BASE_GLB)
    names = [node.get("name", "") for node in base_gltf["nodes"]]
    walk = None
    for animation in base_gltf.get("animations", []):
        if animation.get("name") == BASE_ANIMATION:
            walk = copy.deepcopy(animation)
            break
    if walk is None:
        raise SystemExit(f"animation {BASE_ANIMATION!r} not found")
    walk["name"] = OUTPUT_ANIMATION

    duration = 0.0
    for sampler in walk["samplers"]:
        times = read_accessor(base_gltf, base_bin, sampler["input"])
        duration = max(duration, times[-1][0])

    time_accessor = append_accessor(base_gltf, base_bin, [(0.0,), (duration,)], "SCALAR")

    frozen = 0
    for channel in walk["channels"]:
        target = channel["target"]
        if target.get("path") != "rotation":
            continue
        bone = names[target["node"]]
        if bone not in pose:
            continue
        rotation = tuple(pose[bone])
        output_accessor = append_accessor(base_gltf, base_bin, [rotation, rotation], "VEC4")
        walk["samplers"].append({
            "input": time_accessor,
            "output": output_accessor,
            "interpolation": "LINEAR",
        })
        channel["sampler"] = len(walk["samplers"]) - 1
        frozen += 1

    expected = len(ARM_TARGETS) * len(SIDES)
    if frozen != expected:
        print(f"warning: froze {frozen} channels, expected {expected}", file=sys.stderr)

    base_gltf["animations"] = [walk]
    base_gltf["buffers"][0]["byteLength"] = len(base_bin)

    OUTPUT_GLB.parent.mkdir(parents=True, exist_ok=True)
    write_glb(OUTPUT_GLB, base_gltf, base_bin)
    print(f"wrote {OUTPUT_GLB} ({OUTPUT_GLB.stat().st_size} bytes), "
          f"animation '{OUTPUT_ANIMATION}', duration {duration:.3f}s, {frozen} arm channels frozen")


if __name__ == "__main__":
    main()
