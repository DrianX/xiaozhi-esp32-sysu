const fs = require('fs');
const path = require('path');
const occtimportjs = require('occt-import-js')();

const stepPath = process.argv[2];
const outPath = process.argv[3];
if (!stepPath || !outPath) {
  console.error('Usage: node step-to-glb.js <input.step> <output.glb>');
  process.exit(1);
}

function srgbToLinear(c) {
  return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
}

// Stable hash used for subtle variation between otherwise uncolored part types.
function hashString(s) {
  let h = 2166136261 >>> 0;
  for (let i = 0; i < s.length; i++) {
    h ^= s.charCodeAt(i);
    h = Math.imul(h, 16777619);
  }
  return h >>> 0;
}

const METAL_RE = /(pin|pad|screw|header|焊盘|螺丝|testpoint|天线|antenna)/i;

function defaultSrgbColor(mesh) {
  const name = mesh.name || '';
  if (name === 'Board~drEI' || /^Board/i.test(name)) return [0.02, 0.09, 0.24];
  if (METAL_RE.test(name)) return [0.68, 0.70, 0.73];
  // Most uncolored parts are small SMD components. Keep them in a restrained
  // graphite family; a per-part-type hash means repeated parts stay consistent.
  const category = name.split('~')[0] || 'component';
  const h = hashString(category);
  const grays = [
    [0.14, 0.15, 0.17],
    [0.20, 0.21, 0.23],
    [0.26, 0.27, 0.29],
    [0.31, 0.32, 0.34],
    [0.37, 0.38, 0.40],
    [0.44, 0.45, 0.47],
  ];
  return grays[h % grays.length];
}

function computeNormals(localPos, vc, localIdx) {
  const n = new Float32Array(vc * 3);
  const triCount = localIdx.length / 3;
  for (let t = 0; t < triCount; t++) {
    const a = localIdx[t * 3] * 3;
    const b = localIdx[t * 3 + 1] * 3;
    const c = localIdx[t * 3 + 2] * 3;
    const abx = localPos[b] - localPos[a];
    const aby = localPos[b + 1] - localPos[a + 1];
    const abz = localPos[b + 2] - localPos[a + 2];
    const acx = localPos[c] - localPos[a];
    const acy = localPos[c + 1] - localPos[a + 1];
    const acz = localPos[c + 2] - localPos[a + 2];
    const nx = aby * acz - abz * acy;
    const ny = abz * acx - abx * acz;
    const nz = abx * acy - aby * acx;
    for (const v of [a, b, c]) {
      n[v] += nx;
      n[v + 1] += ny;
      n[v + 2] += nz;
    }
  }
  for (let i = 0; i < n.length; i += 3) {
    const len = Math.hypot(n[i], n[i + 1], n[i + 2]);
    if (len > 1e-12) {
      n[i] /= len;
      n[i + 1] /= len;
      n[i + 2] /= len;
    } else {
      n[i] = 0; n[i + 1] = 1; n[i + 2] = 0;
    }
  }
  return n;
}

(async () => {
  const occt = await occtimportjs;
  console.log('Reading STEP…');
  const t0 = Date.now();
  const content = fs.readFileSync(stepPath);
  const result = occt.ReadStepFile(new Uint8Array(content), {
    linearUnit: 'millimeter',
    linearDeflectionType: 'absolute_value',
    linearDeflection: 0.1,
    angularDeflection: 0.4,
  });
  console.log(`STEP parsed in ${((Date.now() - t0) / 1000).toFixed(1)} s, success=${result.success}`);
  if (!result.success) process.exit(2);

  const meshes = result.meshes || [];
  const positions = [];
  const normals = [];
  const colors = [];
  const indices = [];
  let vertexCount = 0;
  let missingNormals = 0;
  let nonIndexed = 0;
  const min = [Infinity, Infinity, Infinity];
  const max = [-Infinity, -Infinity, -Infinity];

  for (const mesh of meshes) {
    const pos = mesh.attributes?.position?.array;
    if (!pos || pos.length < 9) continue;
    const srcNrm = mesh.attributes?.normal?.array;
    const srcIdx = mesh.index?.array;
    const vc = pos.length / 3;

    // occt is Z-up. Bake a Z-up -> Y-up rotation (x, y, z) -> (x, z, -y).
    const localPos = new Float32Array(vc * 3);
    for (let i = 0; i < pos.length; i += 3) {
      const x = pos[i], y = pos[i + 1], z = pos[i + 2];
      localPos[i] = x;
      localPos[i + 1] = z;
      localPos[i + 2] = -y;
      if (x < min[0]) min[0] = x; if (x > max[0]) max[0] = x;
      if (z < min[1]) min[1] = z; if (z > max[1]) max[1] = z;
      if (-y < min[2]) min[2] = -y; if (-y > max[2]) max[2] = -y;
    }

    const localIdx = srcIdx ? Uint32Array.from(srcIdx) : Uint32Array.from({ length: vc }, (_, i) => i);
    let localNrm;
    if (srcNrm) {
      localNrm = new Float32Array(vc * 3);
      for (let i = 0; i < vc; i++) {
        localNrm[i * 3] = srcNrm[i * 3];
        localNrm[i * 3 + 1] = srcNrm[i * 3 + 2];
        localNrm[i * 3 + 2] = -srcNrm[i * 3 + 1];
      }
    } else {
      missingNormals++;
      localNrm = computeNormals(localPos, vc, localIdx);
    }
    if (!srcIdx) nonIndexed++;

    for (let i = 0; i < localPos.length; i++) positions.push(localPos[i]);
    for (let i = 0; i < localNrm.length; i++) normals.push(localNrm[i]);

    const baseColor = mesh.color || defaultSrgbColor(mesh);
    const lin = baseColor.map(srgbToLinear);
    const cr = Math.round(lin[0] * 65535);
    const cg = Math.round(lin[1] * 65535);
    const cb = Math.round(lin[2] * 65535);
    for (let i = 0; i < vc; i++) colors.push(cr, cg, cb, 65535);

    for (let i = 0; i < localIdx.length; i++) indices.push(vertexCount + localIdx[i]);
    vertexCount += vc;
  }

  if (normals.length !== vertexCount * 3) {
    throw new Error(`Normal count mismatch: ${normals.length / 3} vs ${vertexCount}`);
  }

  const center = [0, 1, 2].map(i => (min[i] + max[i]) / 2);
  for (let i = 0; i < positions.length; i += 3) {
    positions[i] -= center[0];
    positions[i + 1] -= center[1];
    positions[i + 2] -= center[2];
  }
  const size = [max[0] - min[0], max[1] - min[1], max[2] - min[2]];
  console.log(`Merged: ${meshes.length} meshes, ${vertexCount} vertices, ${indices.length / 3} triangles`);
  console.log(`Missing normals: ${missingNormals}, non-indexed meshes: ${nonIndexed}`);
  console.log(`Bounds (mm): ${size.map(v => v.toFixed(2)).join(' x ')}`);

  const posArray = new Float32Array(positions);
  const posBytes = Buffer.from(posArray.buffer);
  const nrmBytes = Buffer.from(new Float32Array(normals).buffer);
  const colorBytes = Buffer.from(new Uint16Array(colors).buffer);
  const idxBytes = Buffer.from(new Uint32Array(indices).buffer);

  const chunks = [];
  let binLength = 0;
  for (const [bytes, target] of [[posBytes, 34962], [nrmBytes, 34962], [colorBytes, 34962], [idxBytes, 34963]]) {
    const offset = binLength;
    chunks.push({ offset, bytes, target });
    binLength += bytes.length;
    binLength = (binLength + 3) & ~3;
  }
  let binBuffer = Buffer.alloc(binLength);
  for (const chunk of chunks) chunk.bytes.copy(binBuffer, chunk.offset);

  // Report the exact Float32 bounds rather than the theoretical ±size/2; the
  // glTF validator requires accessor min/max to match the stored data exactly.
  const boundsMin = [Infinity, Infinity, Infinity];
  const boundsMax = [-Infinity, -Infinity, -Infinity];
  for (let i = 0; i < posArray.length; i += 3) {
    for (let axis = 0; axis < 3; axis++) {
      const value = posArray[i + axis];
      if (value < boundsMin[axis]) boundsMin[axis] = value;
      if (value > boundsMax[axis]) boundsMax[axis] = value;
    }
  }

  const gltf = {
    asset: { version: '2.0', generator: 'mpu6050-dashboard STEP converter (occt-import-js)' },
    scene: 0,
    scenes: [{ nodes: [0] }],
    nodes: [{ name: 'ESP32-3D-Model', mesh: 0 }],
    meshes: [{ name: 'ESP32-3D-Model', primitives: [{ attributes: { POSITION: 0, NORMAL: 1, COLOR_0: 2 }, indices: 3, material: 0, mode: 4 }] }],
    materials: [{
      name: 'PCB and components',
      doubleSided: true,
      pbrMetallicRoughness: {
        baseColorFactor: [1, 1, 1, 1],
        metallicFactor: 0.16,
        roughnessFactor: 0.52,
      },
    }],
    accessors: [
      { bufferView: 0, componentType: 5126, count: vertexCount, type: 'VEC3', min: boundsMin, max: boundsMax },
      { bufferView: 1, componentType: 5126, count: vertexCount, type: 'VEC3' },
      { bufferView: 2, componentType: 5123, normalized: true, count: vertexCount, type: 'VEC4' },
      { bufferView: 3, componentType: 5125, count: indices.length, type: 'SCALAR' },
    ],
    bufferViews: chunks.map(chunk => ({
      buffer: 0,
      byteOffset: chunk.offset,
      byteLength: chunk.bytes.length,
      target: chunk.target,
    })),
    buffers: [{ byteLength: binBuffer.length }],
  };

  let jsonText = JSON.stringify(gltf);
  while (jsonText.length % 4 !== 0) jsonText += ' ';
  const jsonBuffer = Buffer.from(jsonText, 'utf8');
  const totalLength = 12 + 8 + jsonBuffer.length + 8 + binBuffer.length;
  const header = Buffer.alloc(12);
  header.writeUInt32LE(0x46546c67, 0);
  header.writeUInt32LE(2, 4);
  header.writeUInt32LE(totalLength, 8);
  const jsonHeader = Buffer.alloc(8);
  jsonHeader.writeUInt32LE(jsonBuffer.length, 0);
  jsonHeader.writeUInt32LE(0x4e4f534a, 4);
  const binHeader = Buffer.alloc(8);
  binHeader.writeUInt32LE(binBuffer.length, 0);
  binHeader.writeUInt32LE(0x004e4942, 4);
  fs.mkdirSync(path.dirname(outPath), { recursive: true });
  fs.writeFileSync(outPath, Buffer.concat([header, jsonHeader, jsonBuffer, binHeader, binBuffer]));
  console.log(`Wrote ${outPath} (${(fs.statSync(outPath).size / 1024 / 1024).toFixed(2)} MB) in ${((Date.now() - t0) / 1000).toFixed(1)} s`);
})();
