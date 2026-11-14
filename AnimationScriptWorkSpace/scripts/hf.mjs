import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { ensureNvencHelpCompatibility } from './hf_compat.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const candidates = [process.env.HF_CLI_PATH,
  path.join(root, 'node_modules/hyperframes/bin/hyperframes.mjs'),
  path.join(process.env.LOCALAPPDATA || '', 'npm-cache/_npx/955481fd4eb96b74/node_modules/hyperframes/bin/hyperframes.mjs')].filter(Boolean);
const cli = candidates.find(p => existsSync(p));
if (!cli) throw new Error('Run npm install in AnimationScriptWorkSpace first.');
ensureNvencHelpCompatibility(cli);
const env = {...process.env, PATH: path.join(root, 'tools/bin') + path.delimiter + process.env.PATH};
for (const name of ['ffmpeg', 'ffprobe']) {
  const binary=path.join(root,'tools/bin',name+'.exe');
  if(existsSync(binary)) env['HYPERFRAMES_'+name.toUpperCase()+'_PATH']=binary;
}
const systemChrome='C:/Program Files/Google/Chrome/Application/chrome.exe';
if(!env.HYPERFRAMES_BROWSER_PATH&&existsSync(systemChrome))env.HYPERFRAMES_BROWSER_PATH=systemChrome;
if(!env.PRODUCER_HEADLESS_SHELL_PATH&&env.HYPERFRAMES_BROWSER_PATH)env.PRODUCER_HEADLESS_SHELL_PATH=env.HYPERFRAMES_BROWSER_PATH;
const child = spawn(process.execPath, [cli, ...process.argv.slice(2)], { cwd: root, env, stdio: 'inherit', windowsHide: true });
child.on('exit', (code, signal) => { process.exitCode = code ?? (signal ? 1 : 0); });
