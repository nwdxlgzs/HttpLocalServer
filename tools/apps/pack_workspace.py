#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Package a trusted Server application; runtime builds remain offline."""
import argparse,pathlib,zipfile

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('source',type=pathlib.Path);parser.add_argument('output',type=pathlib.Path);args=parser.parse_args()
    root=args.source.resolve(strict=True);output=args.output.resolve()
    if not (root/'server.qjs').is_file():raise SystemExit('Source requires server.qjs')
    if output==root or root in output.parents:raise SystemExit('Output ZIP must be outside the application source')
    entries=[];total=0
    for path in sorted(root.rglob('*')):
        if path.is_symlink():raise SystemExit('Symlinks are not allowed: '+str(path))
        if not path.is_file():continue
        relative=path.relative_to(root).as_posix();size=path.stat().st_size
        if len(relative)>4096 or len(path.relative_to(root).parts)>32 or any(c in relative for c in '\\:%?#\x00'):raise SystemExit('Unsupported ZIP path: '+relative)
        if size>32*1024*1024:raise SystemExit('Entry exceeds 32 MiB: '+relative)
        total+=size;entries.append((path,relative))
    if len(entries)>8192 or total>256*1024*1024:raise SystemExit('Application exceeds ZIP safety budgets')
    output.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for path,name in entries:
            data=path.read_bytes();info=zipfile.ZipInfo(name,(2026,1,1,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED
            # Stored entries avoid rejecting extremely repetitive app assets under
            # the runtime's deliberate 200:1 expansion ceiling.
            import zlib
            if len(data)>max(1,len(zlib.compress(data)))*190:info.compress_type=zipfile.ZIP_STORED
            archive.writestr(info,data)
    if output.stat().st_size>32*1024*1024:output.unlink();raise SystemExit('Workspace ZIP exceeds 32 MiB')
    print(str(output))

if __name__=='__main__':main()
