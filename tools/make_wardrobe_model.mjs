#!/usr/bin/env node
// Generate the traversable crossed-arch Wardrobe model.
//
// Coordinates use the engine's tile space: x/y cover one cell and z points
// upward. The GLB conversion below maps that to glTF's X/Y/Z convention. Two
// perpendicular, diagonal archways meet at the centre, leaving all four
// approaches open. A brass crown supports the separately rendered character
// miniature.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const output = path.join(root, "assets", "custom", "models", "wardrobe.glb");

const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const cross = (a, b) => [
  a[1] * b[2] - a[2] * b[1],
  a[2] * b[0] - a[0] * b[2],
  a[0] * b[1] - a[1] * b[0],
];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const normalized = (v) => {
  const length = Math.sqrt(dot(v, v));
  return [v[0] / length, v[1] / length, v[2] / length];
};

class Primitive {
  positions = [];
  normals = [];

  triangle(a, b, c, outward) {
    let normal = cross(sub(b, a), sub(c, a));
    if (dot(normal, outward) < 0) {
      [b, c] = [c, b];
      normal = normal.map((value) => -value);
    }
    if (dot(normal, normal) < 1e-18) return;
    normal = normalized(normal);
    for (const vertex of [a, b, c]) {
      this.positions.push(vertex);
      this.normals.push(normal);
    }
  }

  quad(a, b, c, d, outward) {
    this.triangle(a, b, c, outward);
    this.triangle(a, c, d, outward);
  }

  box(x0, y0, z0, x1, y1, z1) {
    this.quad([x0, y0, z0], [x0, y1, z0], [x0, y1, z1], [x0, y0, z1], [-1, 0, 0]);
    this.quad([x1, y0, z0], [x1, y0, z1], [x1, y1, z1], [x1, y1, z0], [1, 0, 0]);
    this.quad([x0, y0, z0], [x0, y0, z1], [x1, y0, z1], [x1, y0, z0], [0, -1, 0]);
    this.quad([x0, y1, z0], [x1, y1, z0], [x1, y1, z1], [x0, y1, z1], [0, 1, 0]);
    this.quad([x0, y0, z0], [x1, y0, z0], [x1, y1, z0], [x0, y1, z0], [0, 0, -1]);
    this.quad([x0, y0, z1], [x0, y1, z1], [x1, y1, z1], [x1, y0, z1], [0, 0, 1]);
  }
}

const point = (axis, u, v, z) => axis === "x" ? [u, v, z] : [v, u, z];

function rotateAroundTileCenter(mesh, angle) {
  const cosine = Math.cos(angle);
  const sine = Math.sin(angle);
  mesh.positions = mesh.positions.map(([x, y, z]) => {
    const centeredX = x - 0.5;
    const centeredY = y - 0.5;
    return [
      0.5 + centeredX * cosine - centeredY * sine,
      0.5 + centeredX * sine + centeredY * cosine,
      z,
    ];
  });
  mesh.normals = mesh.normals.map(([x, y, z]) => [
    x * cosine - y * sine,
    x * sine + y * cosine,
    z,
  ]);
}

function addArch(mesh, axis) {
  const v0 = 0.455;
  const v1 = 0.545;
  const spring = 0.57;
  const outer = 0.56;
  const inner = 0.465;
  const segments = 20;
  const leftOuter = 0.5 - outer;
  const leftInner = 0.5 - inner;
  const rightInner = 0.5 + inner;
  const rightOuter = 0.5 + outer;

  if (axis === "x") {
    mesh.box(leftOuter, v0, 0.03, leftInner, v1, spring);
    mesh.box(rightInner, v0, 0.03, rightOuter, v1, spring);
  } else {
    mesh.box(v0, leftOuter, 0.03, v1, leftInner, spring);
    mesh.box(v0, rightInner, 0.03, v1, rightOuter, spring);
  }

  const frontNormal = axis === "x" ? [0, -1, 0] : [-1, 0, 0];
  const backNormal = frontNormal.map((value) => -value);
  for (let i = 0; i < segments; ++i) {
    const a = Math.PI * i / segments;
    const b = Math.PI * (i + 1) / segments;
    const ringPoint = (radius, angle, v) => point(
      axis,
      0.5 + radius * Math.cos(angle),
      v,
      spring + radius * Math.sin(angle),
    );
    const outerA0 = ringPoint(outer, a, v0);
    const outerB0 = ringPoint(outer, b, v0);
    const innerA0 = ringPoint(inner, a, v0);
    const innerB0 = ringPoint(inner, b, v0);
    const outerA1 = ringPoint(outer, a, v1);
    const outerB1 = ringPoint(outer, b, v1);
    const innerA1 = ringPoint(inner, a, v1);
    const innerB1 = ringPoint(inner, b, v1);
    mesh.quad(outerA0, innerA0, innerB0, outerB0, frontNormal);
    mesh.quad(outerA1, outerB1, innerB1, innerA1, backNormal);
    const middle = (a + b) * 0.5;
    const radial = axis === "x"
      ? [Math.cos(middle), 0, Math.sin(middle)]
      : [0, Math.cos(middle), Math.sin(middle)];
    mesh.quad(outerA0, outerB0, outerB1, outerA1, radial);
    mesh.quad(innerA0, innerA1, innerB1, innerB0, radial.map((value) => -value));
  }
}

function addCylinder(mesh, centerX, centerY, radius, z0, z1, segments = 16) {
  for (let i = 0; i < segments; ++i) {
    const a = Math.PI * 2 * i / segments;
    const b = Math.PI * 2 * (i + 1) / segments;
    const pa = [centerX + radius * Math.cos(a), centerY + radius * Math.sin(a)];
    const pb = [centerX + radius * Math.cos(b), centerY + radius * Math.sin(b)];
    mesh.triangle([centerX, centerY, z0], [pb[0], pb[1], z0], [pa[0], pa[1], z0], [0, 0, -1]);
    mesh.triangle([centerX, centerY, z1], [pa[0], pa[1], z1], [pb[0], pb[1], z1], [0, 0, 1]);
    mesh.quad(
      [pa[0], pa[1], z0], [pb[0], pb[1], z0],
      [pb[0], pb[1], z1], [pa[0], pa[1], z1],
      [Math.cos((a + b) * 0.5), Math.sin((a + b) * 0.5), 0],
    );
  }
}

const wood = new Primitive();
addArch(wood, "x");
addArch(wood, "y");
rotateAroundTileCenter(wood, Math.PI / 4);

const brass = new Primitive();
for (const [x, y] of [[-0.0125, 0.5], [1.0125, 0.5], [0.5, -0.0125], [0.5, 1.0125]]) {
  brass.box(x - 0.06, y - 0.06, 0.02, x + 0.06, y + 0.06, 0.09);
}
addCylinder(brass, 0.5, 0.5, 0.135, 1.07, 1.17, 16);
rotateAroundTileCenter(brass, Math.PI / 4);

const materials = [
  {
    name: "Warm Wood",
    pbrMetallicRoughness: {
      baseColorFactor: [0.32, 0.105, 0.035, 1],
      metallicFactor: 0,
      roughnessFactor: 0.72,
    },
  },
  {
    name: "Brass",
    pbrMetallicRoughness: {
      baseColorFactor: [0.78, 0.44, 0.09, 1],
      metallicFactor: 0.92,
      roughnessFactor: 0.27,
    },
  },
];

const toGltf = ([x, y, z]) => [x, z, -y];
const binaryParts = [];
let binaryLength = 0;
const bufferViews = [];
const accessors = [];
const gltfPrimitives = [];

function appendBuffer(buffer, target) {
  const padding = (4 - binaryLength % 4) % 4;
  if (padding) {
    binaryParts.push(Buffer.alloc(padding));
    binaryLength += padding;
  }
  const offset = binaryLength;
  binaryParts.push(buffer);
  binaryLength += buffer.length;
  bufferViews.push({ buffer: 0, byteOffset: offset, byteLength: buffer.length, target });
  return bufferViews.length - 1;
}

function addPrimitive(primitive, material) {
  const positions = primitive.positions.map(toGltf);
  const normals = primitive.normals.map(toGltf);
  const uvs = primitive.positions.map(([x, y]) => [x, y]);
  const positionData = Buffer.alloc(positions.length * 12);
  const normalData = Buffer.alloc(normals.length * 12);
  const uvData = Buffer.alloc(uvs.length * 8);
  const indexData = Buffer.alloc(positions.length * 4);
  positions.forEach((value, index) => value.forEach((n, axis) => positionData.writeFloatLE(n, index * 12 + axis * 4)));
  normals.forEach((value, index) => value.forEach((n, axis) => normalData.writeFloatLE(n, index * 12 + axis * 4)));
  uvs.forEach((value, index) => value.forEach((n, axis) => uvData.writeFloatLE(n, index * 8 + axis * 4)));
  positions.forEach((_, index) => indexData.writeUInt32LE(index, index * 4));
  const min = [0, 1, 2].map((axis) => Math.min(...positions.map((p) => p[axis])));
  const max = [0, 1, 2].map((axis) => Math.max(...positions.map((p) => p[axis])));
  const positionAccessor = accessors.push({
    bufferView: appendBuffer(positionData, 34962), componentType: 5126,
    count: positions.length, type: "VEC3", min, max,
  }) - 1;
  const normalAccessor = accessors.push({
    bufferView: appendBuffer(normalData, 34962), componentType: 5126,
    count: normals.length, type: "VEC3",
  }) - 1;
  const uvAccessor = accessors.push({
    bufferView: appendBuffer(uvData, 34962), componentType: 5126,
    count: uvs.length, type: "VEC2",
  }) - 1;
  const indexAccessor = accessors.push({
    bufferView: appendBuffer(indexData, 34963), componentType: 5125,
    count: positions.length, type: "SCALAR",
  }) - 1;
  gltfPrimitives.push({
    attributes: { POSITION: positionAccessor, NORMAL: normalAccessor, TEXCOORD_0: uvAccessor },
    indices: indexAccessor,
    material,
    mode: 4,
  });
}

addPrimitive(wood, 0);
addPrimitive(brass, 1);
const binary = Buffer.concat(binaryParts);
const document = {
  asset: { version: "2.0", generator: "tools/make_wardrobe_model.mjs" },
  scene: 0,
  scenes: [{ nodes: [0] }],
  nodes: [{ mesh: 0, name: "Wardrobe" }],
  meshes: [{ name: "Wardrobe", primitives: gltfPrimitives }],
  materials,
  buffers: [{ byteLength: binary.length }],
  bufferViews,
  accessors,
};
let json = Buffer.from(JSON.stringify(document));
json = Buffer.concat([json, Buffer.alloc((4 - json.length % 4) % 4, 0x20)]);
const paddedBinary = Buffer.concat([binary, Buffer.alloc((4 - binary.length % 4) % 4)]);
const total = 12 + 8 + json.length + 8 + paddedBinary.length;
const glb = Buffer.alloc(total);
glb.writeUInt32LE(0x46546c67, 0);
glb.writeUInt32LE(2, 4);
glb.writeUInt32LE(total, 8);
glb.writeUInt32LE(json.length, 12);
glb.writeUInt32LE(0x4e4f534a, 16);
json.copy(glb, 20);
const binaryHeader = 20 + json.length;
glb.writeUInt32LE(paddedBinary.length, binaryHeader);
glb.writeUInt32LE(0x004e4942, binaryHeader + 4);
paddedBinary.copy(glb, binaryHeader + 8);
fs.mkdirSync(path.dirname(output), { recursive: true });
if (!fs.existsSync(output) || !fs.readFileSync(output).equals(glb)) {
  fs.writeFileSync(output, glb);
}
console.log(`Wrote ${output} (${wood.positions.length + brass.positions.length} vertices)`);
