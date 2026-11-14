"""Writable creation, hidden frozen entries and responsive host settings."""
import pathlib, subprocess, sys, tempfile
from integration import Server

def run(executable, https=False):
    with tempfile.TemporaryDirectory(prefix='hls-workspace-') as directory:
        root=pathlib.Path(directory)
        (root/'NewFiles').mkdir()
        (root/'NewFiles/keep.txt').write_text('keep existing contents',encoding='utf-8')
        (root/'NewFiles/config.json').write_text('private',encoding='utf-8')
        (root/'NewFiles/.hls-config-lock-config.json').write_text('private',encoding='utf-8')
        (root/'NewFiles/.hls-state-test').mkdir()
        server=Server(executable,root,extra=('--no-magika','--write-mode','shared',*(['--https','--tls-self-signed'] if https else [])))
        try:
            subprocess.run(['node',str(pathlib.Path(__file__).with_suffix('.cjs')),('https://' if https else 'http://')+'127.0.0.1:'+str(server.port)],check=True,timeout=300)
        finally:server.close()

if __name__=='__main__':run(str(pathlib.Path(sys.argv[1]).resolve()),'--https' in sys.argv[2:])
