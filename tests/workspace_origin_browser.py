"""Browser same-origin boundary between the ZIP application and host console."""
import pathlib,socket,subprocess,sys,tempfile
from integration import Server
from wasm_app_integration import package

def run(exe,https=False):
    with tempfile.TemporaryDirectory(prefix='hls-app-origin-') as directory:
        root=pathlib.Path(directory);app=root/'app.zip'
        package(app,"export function onRequest(){return {};}")
        with socket.socket() as probe:probe.bind(('127.0.0.1',0));port=probe.getsockname()[1]
        server=Server(exe,root,'server',('--workspace-app',str(app),'--workspace-control-port',str(port),'--no-magika','--mcp',*(['--https','--tls-self-signed'] if https else [])))
        try:
            scheme='https' if https else 'http'
            subprocess.run(['node',str(pathlib.Path(__file__).with_suffix('.cjs')),f'{scheme}://127.0.0.1:{server.port}',f'{scheme}://localhost:{port}'],check=True,timeout=90)
        finally:server.close()

if __name__=='__main__':run(str(pathlib.Path(sys.argv[1]).resolve()),'--https' in sys.argv[2:])
