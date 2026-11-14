// HyperFrames 0.9.44 expects numeric enum values in NVENC help. FFmpeg 4.2
// omits those numbers, so its supported legacy presets were missed.
// Preserve discovery of both old ("slow E..V.....") and new ("p5 17 E..V...") formats.
import fs from 'node:fs';
import path from 'node:path';

export function ensureNvencHelpCompatibility(cli) {
 const dist=path.resolve(path.dirname(cli),'../dist');
 if(!fs.existsSync(dist))return;
 const before=String.raw`/^\s{4,}([a-z][a-z0-9_]*)\s+-?\d+\s+[E.]\S{3}/`;
 const after=String.raw`/^\s{4,}([a-z][a-z0-9_]*)\s+(?:-?\d+\s+)?[E.]\S{3}/`;
 for(const name of fs.readdirSync(dist)) {
  if(!name.endsWith('.js'))continue;
  const file=path.join(dist,name);const source=fs.readFileSync(file,'utf8');
  if(source.includes('function parseEncoderPresetNames(')&&source.includes(before)) {
   fs.writeFileSync(file,source.replace(before,after));
   console.log('[production] Applied legacy FFmpeg NVENC help compatibility.');
  }
 }
}
