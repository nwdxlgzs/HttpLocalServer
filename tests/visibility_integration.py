"""Filesystem visibility, filtered pagination and hidden-ancestor search."""
import concurrent.futures
import ctypes
import os
import pathlib
import sys
import tempfile
import time
from integration import Server, check
from v2_integration import route


def hide(path, system=False):
    if os.name == 'nt':
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.SetFileAttributesW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32]
        check(kernel.SetFileAttributesW(str(path), 4 if system else 2), 'Set native hidden/system attribute')


def run(exe):
    with tempfile.TemporaryDirectory(prefix='hls-visibility-') as directory:
        root = pathlib.Path(directory)
        for i in range(125):
            (root / ('needle-visible-%03d.txt' % i)).write_text('visible')
        hidden_name = 'private-folder' if os.name == 'nt' else '.private-folder'
        private = root / hidden_name
        (private / 'nested').mkdir(parents=True)
        (private / 'needle-child.txt').write_text('inside hidden folder')
        (private / 'nested/needle-deep.txt').write_text('under hidden ancestor')
        hide(private)
        hidden_files = []
        for i in range(23):
            name = ('needle-hidden-%03d.txt' if os.name == 'nt' else '.needle-hidden-%03d.txt') % i
            p = root / name
            p.write_text('hidden')
            hide(p, system=i == 0)
            hidden_files.append('/' + name)
        (root / '.needle-dot.txt').write_text('dot prefix follows host convention')
        visible_total = 126 if os.name == 'nt' else 125
        server = Server(exe, root, extra=('--no-magika',))
        try:
            default = server.api(route('list', path='/', limit=100))
            tail = server.api(route('list', path='/', offset=100, limit=100))
            check(default['total'] == visible_total and len(default['entries']) == 100, 'Hidden items excluded before first-page count')
            check(len(tail['entries']) == visible_total - 100, 'Filtered second page has no gaps or hidden placeholders')
            check(not any(e['hidden'] for e in default['entries'] + tail['entries']), 'Default view only returns visible items')
            all_items = server.api(route('list', path='/', showHidden=1, limit=500))
            check(all_items['total'] == 150, 'Opt-in listing includes native hidden folders and files')
            flags = {e['path']: e['hidden'] for e in all_items['entries']}
            check(all(flags[name] for name in hidden_files) and flags['/' + hidden_name], 'API exposes actual hidden metadata')
            check(flags['/.needle-dot.txt'] == (os.name != 'nt'), 'Dot files are ordinary on Windows unless flagged hidden')

            def listing(show):
                return server.api(route('list', path='/', showHidden=int(show), limit=1))['total']

            with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
                totals = list(pool.map(listing, [False, True] * 12))
            check(totals == [visible_total, 150] * 12, 'Different visitors and listing caches do not mix visibility')
            check(server.request(route('list', path='/', showHidden=2))[0] == 400, 'Invalid visibility flag is rejected')
            for _ in range(100):
                found = server.api(route('search', path='/', q='needle', showHidden=1, limit=500))
                if not found['indexing'] and found['total'] == 151:
                    break
                time.sleep(.05)
            check(found['total'] == 151, 'Search index includes all searchable generations')
            visible = server.api(route('search', path='/', q='needle', limit=500))
            check(visible['total'] == visible_total, 'Default search excludes hidden ancestors and their visible descendants')
            check(not any(e['path'].startswith('/' + hidden_name + '/') for e in visible['entries']), 'Hidden-ancestor contents cannot appear through search')
            scoped = server.api(route('search', path='/' + hidden_name, q='needle', limit=100))
            check(scoped['total'] == 2, 'Explicitly opening a hidden folder still permits searching visible children')
            check(server.api(route('list', path='/', limit=1))['total'] == visible_total, 'Another visitor retains default after opt-in requests')
        finally:
            server.close()
    print('Visibility HTTP tests passed: native attributes, pagination, visitor isolation, hidden-ancestor search')


if __name__ == '__main__':
    run(pathlib.Path(sys.argv[1]).resolve())
