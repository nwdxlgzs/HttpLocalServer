"""Run shipped ZIP codecs and task orchestration against both actual modes."""
import pathlib
import subprocess
import sys
import tempfile
import zipfile
from integration import Server, check


def run(executable):
    for mode in ('filetrans', 'server'):
        with tempfile.TemporaryDirectory(prefix='hls-archive-http-') as folder:
            root = pathlib.Path(folder)
            fixture = root / 'archive-fixture'
            (fixture / 'nested').mkdir(parents=True)
            (fixture / 'empty').mkdir()
            expected = {'large.bin': bytes(range(251)) * 5000,
                        'nested/中文.txt': '有界 ZIP 流'.encode('utf-8') * 100,
                        'zero.txt': b''}
            for name, data in expected.items():
                (fixture / name).write_bytes(data)
            (fixture / 'config.json').write_text('{"version":1,"host":{"announcement":"never archive"}}', encoding='utf-8')
            server = Server(executable, root, mode, ('--no-magika','--config',str(fixture/'config.json')),config=True)
            try:
                for managed in (False, True):
                    output = root / ('managed.zip' if managed else 'codec.zip')
                    command = ['node', str(pathlib.Path(__file__).with_name('archive_test.cjs')),
                               'http://127.0.0.1:' + str(server.port), str(output)]
                    if managed:
                        command.append('managed')
                    subprocess.run(command, check=True, timeout=40)
                    with zipfile.ZipFile(output) as archive:
                        for name, data in expected.items():
                            check(archive.read('archive-fixture/' + name) == data, 'Exact archived bytes')
                        check('archive-fixture/empty/' in archive.namelist(), 'Empty directory preserved')
                        check('archive-fixture/config.json' not in archive.namelist(), 'Frozen file skipped')
                output = root / 'cache-recovered.zip'
                subprocess.run(['node', str(pathlib.Path(__file__).with_name('cache_jobs_test.cjs')),
                                'http://127.0.0.1:' + str(server.port), str(output)], check=True, timeout=45)
                with zipfile.ZipFile(output) as archive:
                    for name, data in expected.items():
                        check(archive.read('archive-fixture/' + name) == data, 'Exact ZIP bytes after mid-file server-cache recovery')
            finally:
                server.close()
    print('archive HTTP integration passed: Server/FileTrans, legacy codec and managed queue, ZIP64, Unicode, empty files/directories, frozen exclusions')


if __name__ == '__main__':
    run(str(pathlib.Path(sys.argv[1]).resolve()))
