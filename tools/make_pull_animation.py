#!/usr/bin/env python3
"""Generates the druid's arms-back pulling walk animation.

The clip uses the same Rig_Medium Walking_B cycle as the other heroes, freezes
both arm chains toward the object behind the character, and adds a modest
forward lean. Run from the repository root:

    python tools/make_pull_animation.py
"""

import copy

import make_push_animation as base


OUTPUT_GLB = base.ROOT / "assets/custom/Rig_Medium_Pull.glb"
OUTPUT_ANIMATION = "Pull_Walk"
LEAN_DEGREES = 20.0


def main():
    skeleton_gltf, _ = base.load_glb(base.SKELETON_GLB)
    skeleton = base.Skeleton(skeleton_gltf)

    foot = skeleton.world_position(skeleton.by_name["foot.r"])
    toes = skeleton.world_position(skeleton.by_name["toes.r"])
    forward = base.vnormalize(
        (toes[0] - foot[0], 0.0, toes[2] - foot[2]))
    backward = tuple(-component for component in forward)
    up = (0.0, 1.0, 0.0)

    lean_local = base.localized_pitch(
        skeleton, base.LEAN_BONE, forward, up, LEAN_DEGREES)
    lean_index = skeleton.by_name[base.LEAN_BONE]
    skeleton.local_rotation[lean_index] = base.qnormalize(base.qmul(
        lean_local, skeleton.local_rotation[lean_index]))
    head_counter = base.localized_pitch(
        skeleton, base.HEAD_BONE, forward, up, -LEAN_DEGREES)
    head_index = skeleton.by_name[base.HEAD_BONE]
    skeleton.local_rotation[head_index] = base.qnormalize(base.qmul(
        head_counter, skeleton.local_rotation[head_index]))

    original_targets = base.ARM_TARGETS
    base.ARM_TARGETS = [
        (name, -abs(forward_weight), up_weight)
        for name, forward_weight, up_weight in original_targets
    ]
    try:
        pose = base.solve_arm_pose(skeleton, forward, up)
    finally:
        base.ARM_TARGETS = original_targets
    base.verify_pose(skeleton, backward)

    gltf, binary = base.load_glb(base.BASE_GLB)
    names = [node.get("name", "") for node in gltf["nodes"]]
    walk = next(
        (copy.deepcopy(animation)
         for animation in gltf.get("animations", [])
         if animation.get("name") == base.BASE_ANIMATION),
        None)
    if walk is None:
        raise SystemExit(f"animation {base.BASE_ANIMATION!r} not found")
    walk["name"] = OUTPUT_ANIMATION

    duration = max(
        base.read_accessor(gltf, binary, sampler["input"])[-1][0]
        for sampler in walk["samplers"])
    time_accessor = base.append_accessor(
        gltf, binary, [(0.0,), (duration,)], "SCALAR")

    base_skeleton = base.Skeleton(gltf)
    baked_pitches = {
        base.LEAN_BONE: base.localized_pitch(
            base_skeleton, base.LEAN_BONE, forward, up, LEAN_DEGREES),
        base.HEAD_BONE: base.localized_pitch(
            base_skeleton, base.HEAD_BONE, forward, up, -LEAN_DEGREES),
    }
    frozen = 0
    for channel in walk["channels"]:
        target = channel["target"]
        if target.get("path") != "rotation":
            continue
        bone = names[target["node"]]
        sampler = walk["samplers"][channel["sampler"]]
        if bone in baked_pitches:
            values = base.read_accessor(gltf, binary, sampler["output"])
            leaned = [base.qnormalize(base.qmul(
                baked_pitches[bone], tuple(value))) for value in values]
            output = base.append_accessor(gltf, binary, leaned, "VEC4")
            walk["samplers"].append({
                "input": sampler["input"],
                "output": output,
                "interpolation": sampler.get("interpolation", "LINEAR"),
            })
            channel["sampler"] = len(walk["samplers"]) - 1
        elif bone in pose:
            rotation = tuple(pose[bone])
            output = base.append_accessor(
                gltf, binary, [rotation, rotation], "VEC4")
            walk["samplers"].append({
                "input": time_accessor,
                "output": output,
                "interpolation": "LINEAR",
            })
            channel["sampler"] = len(walk["samplers"]) - 1
            frozen += 1

    gltf["animations"] = [walk]
    gltf["buffers"][0]["byteLength"] = len(binary)
    OUTPUT_GLB.parent.mkdir(parents=True, exist_ok=True)
    base.write_glb(OUTPUT_GLB, gltf, binary)
    print(
        f"wrote {OUTPUT_GLB} ({OUTPUT_GLB.stat().st_size} bytes), "
        f"animation '{OUTPUT_ANIMATION}', duration {duration:.3f}s, "
        f"{frozen} arm channels frozen")


if __name__ == "__main__":
    main()
