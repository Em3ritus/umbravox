#!/usr/bin/env python3
"""Assemble the native browser installer from checksum-verified release ZIPs.
Usage: assemble-native-installer.py OUTPUT NM_ZIP WS35_ZIP WS28_ZIP
Never compiles or changes firmware. All artifacts stay same-origin on Pages.
"""
import hashlib, io, json, pathlib, shutil, sys, zipfile

def assemble(out, packages):
    root = pathlib.Path(__file__).resolve().parents[1]
    out = pathlib.Path(out)
    out.mkdir(parents=True, exist_ok=True)
    shutil.copytree(root/'web/native', out, dirs_exist_ok=True, ignore=shutil.ignore_patterns('test'))
    for key, path in zip(('nm-cyd-c5-engine','ws-c5-35-engine','ws-c5-28-engine'), packages):
        with zipfile.ZipFile(path) as archive:
            names=archive.namelist()
            manifest_name=next(n for n in names if n.endswith('/manifest.json'))
            prefix=manifest_name.rsplit('/',1)[0]+'/'
            m=json.loads(archive.read(manifest_name))
            if m['environment'] != key or m['minimum_flash_bytes'] != 0x1000000 or m['app_slot_bytes'] != 0x600000:
                raise ValueError('Wrong board or partition layout in package')
            for line in archive.read(prefix+'SHA256SUMS.txt').decode().splitlines():
                digest, name=line.split('  ',1)
                if hashlib.sha256(archive.read(prefix+name)).hexdigest()!=digest: raise ValueError('Package checksum mismatch')
            dest=out/'firmware'/key;dest.mkdir(parents=True,exist_ok=True)
            parts=[]
            for f in m['files']:
                data=archive.read(prefix+f['path'])
                if hashlib.sha256(data).hexdigest()!=f['sha256']: raise ValueError('Firmware checksum mismatch')
                (dest/f['path']).write_bytes(data)
                parts.append({'path':f['path'],'offset':int(f['offset'],16),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'md5':hashlib.md5(data).hexdigest()})
            package_name=pathlib.Path(path).name
            shutil.copy2(path,out/package_name)
            manifest={'boardKey':key,'chip':'ESP32-C5','flashBytes':0x1000000,'commit':m['commit'],'parts':parts,'packageUrl':package_name}
            (dest/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (out/'.nojekyll').touch()
    files=sorted(p for p in out.rglob('*') if p.is_file() and p.name!='SITE-SHA256SUMS.txt')
    (out/'SITE-SHA256SUMS.txt').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(out))+'\n' for p in files))

if __name__=='__main__':
    if len(sys.argv)!=5: raise SystemExit(__doc__)
    assemble(sys.argv[1],sys.argv[2:])
