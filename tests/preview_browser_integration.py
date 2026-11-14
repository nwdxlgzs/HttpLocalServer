"""Real browser acceptance for embedded preview engines and both ZIP modes."""
import pathlib
import subprocess
import sys
import tempfile
from integration import Server
from preview_fixtures import create


def run(executable,https=False):
    with tempfile.TemporaryDirectory(prefix='hls-preview-browser-') as directory:
        root=pathlib.Path(directory)
        create(root)
        server=Server(executable,root,extra=('--zip-preview','host','--no-magika',*(['--https','--tls-self-signed'] if https else [])))
        try:
            subprocess.run(['node',str(pathlib.Path(__file__).with_name('preview_browser.cjs')),
                            ('https://' if https else 'http://')+'127.0.0.1:'+str(server.port)],check=True,timeout=360)
        except Exception:
            print('Server exit at failure:',server.process.poll(),flush=True)
            server.log.seek(0);print(server.log.read().decode('utf-8',errors='replace')[-6000:])
            raise
        finally:
            server.close()


if __name__=='__main__':
    run(str(pathlib.Path(sys.argv[1]).resolve()),'--https' in sys.argv[2:])
