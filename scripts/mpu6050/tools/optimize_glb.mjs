/*
 * Compress a browser GLB with Meshopt.
 *
 * Usage:
 *   node optimize_glb.mjs <input.glb> [output.glb]
 *
 * The website uses Three.js' MeshoptDecoder, so the optimized model keeps the
 * full geometry but loads much faster than the raw 12.7 MB export.
 */
import fs from 'node:fs';
import path from 'node:path';
import { NodeIO } from '@gltf-transform/core';
import { KHRMeshQuantization, EXTMeshoptCompression } from '@gltf-transform/extensions';
import { dedup, prune, meshopt } from '@gltf-transform/functions';
import { MeshoptEncoder } from 'meshoptimizer';

const input = process.argv[2];
const output = process.argv[3] || input;
if (!input) {
  console.error('Usage: node optimize_glb.mjs <input.glb> [output.glb]');
  process.exit(1);
}

await MeshoptEncoder.ready;

const io = new NodeIO()
  .registerExtensions([KHRMeshQuantization, EXTMeshoptCompression])
  .registerDependencies({ 'meshopt.encoder': MeshoptEncoder });

const before = fs.statSync(path.resolve(input)).size;
const document = await io.read(path.resolve(input));
const transformation = await meshopt({ encoder: MeshoptEncoder, level: 'high' });

await document.transform(
  dedup(),
  prune(),
  transformation,
);

fs.mkdirSync(path.dirname(path.resolve(output)), { recursive: true });
await io.write(path.resolve(output), document);

const after = fs.statSync(path.resolve(output)).size;
console.log(`Optimized ${path.basename(output)}: ${(before / 1024 / 1024).toFixed(2)} MB -> ${(after / 1024 / 1024).toFixed(2)} MB`);
console.log('If the model changed, bump MODEL_VERSION in src/main.js so browsers fetch the new file.');
