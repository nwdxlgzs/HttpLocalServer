"""Optional offline unpacking. CMake can also unpack vendored sources directly."""
import hashlib
import pathlib
import tarfile
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
PACKAGES = ROOT / 'third_party'
DEPS = ROOT / '.deps'


def verify(name, expected):
    path = PACKAGES / name
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        raise RuntimeError('Source archive checksum mismatch: ' + name)
    return path


def main():
    boost = verify('boost-headers-1.83.0.tar.xz', '9e073d635431f223add11bcf52fadf245654c4a8650c3a5e9b74cf941d41db70')
    destination = DEPS / 'boost-1.83.0'
    if not (destination / 'boost/beast.hpp').exists():
        destination.mkdir(parents=True, exist_ok=True)
        with tarfile.open(boost) as archive:
            for member in archive:
                path = pathlib.PurePosixPath(member.name)
                if member.isfile() and not path.is_absolute() and '..' not in path.parts:
                    target = destination / member.name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with archive.extractfile(member) as source, target.open('wb') as output:
                        output.write(source.read())
    sqlite = verify('sqlite-amalgamation-3510200.zip', '6e2a845a493026bdbad0618b2b5a0cf48584faab47384480ed9f592d912f23ec')
    destination = DEPS / 'sqlite-amalgamation-3510200'
    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(sqlite) as archive:
        for name in ('sqlite3.c', 'sqlite3.h'):
            (destination / name).write_bytes(archive.read('sqlite-amalgamation-3510200/' + name))
    quickjs=verify('quickjs-ng-0.18.0.tar.gz','28bcd89aa3683fd2a946537e5ce3f11cc46b5cbf3383c7561aaef3df5ff9f1dd')
    with tarfile.open(quickjs) as archive:
        for member in archive:
            name=pathlib.PurePosixPath(member.name)
            if member.isfile() and not name.is_absolute() and '..' not in name.parts:
                target=DEPS/member.name;target.parent.mkdir(parents=True,exist_ok=True)
                with archive.extractfile(member) as source,target.open('wb') as output:output.write(source.read())
    print('Offline dependencies ready')


if __name__ == '__main__':
    main()
