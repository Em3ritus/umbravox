"""Offline generator checks; optional full CSV-to-runtime lookup audit."""
import csv
import importlib.util
import subprocess
import tempfile
from pathlib import Path
import sys
spec=importlib.util.spec_from_file_location('generator',Path(__file__).with_name('generate_oui.py'))
g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)
 for filename,registry,bits,_ in g.SOURCES:
  with (p/filename).open('w') as f:
   w=csv.writer(f);w.writerow(['Registry','Assignment','Organization Name']);w.writerow([registry,'001122334'[:bits//4],registry+' vendor'])
 out=p/'out.inc';g.generate(p,out,'2026-10-31','');before=out.read_bytes();g.generate(p,out,'2026-10-31','');assert before==out.read_bytes()
 with (p/'oui.csv').open('a') as f:f.write('MA-L,001122,Other vendor\n')
 g.generate(p,out,'2026-10-31','');assert 'MULTIPLE IEEE REGISTRANTS' in out.read_text()
 good=out.read_bytes()
 (p/'mam.csv').write_text('Registry,Assignment,Organization Name\nMA-M,BAD,Broken\n')
 try:g.generate(p,out,'2026-10-31','');raise AssertionError('malformed source accepted')
 except ValueError:pass
 assert out.read_bytes()==good
 (p/'mam.csv').write_text('Registry,Assignment,Organization Name\n')
 try:g.generate(p,out,'2026-10-31','');raise AssertionError('empty source accepted')
 except ValueError:pass
 print('generator: deterministic, ambiguity explicit, malformed/empty fail without replacing output PASS')
if len(sys.argv)==3:
 source=Path(sys.argv[1]);queries={};groups={24:{},28:{},36:{}}
 for filename,registry,bits,_ in g.SOURCES:
  for row in csv.DictReader((source/filename).open()):
   key=int(row['Assignment'],16);name=g.unicodedata.normalize('NFKD',row['Organization Name']).encode('ascii','ignore').decode().strip();name=' '.join(name.split()) or 'REGISTRANT NAME NOT ASCII'
   item=groups[bits].setdefault(key,[set(),set()]);item[0].add(name);item[1].add(registry)
   for suffix in [0,(1<<(48-bits))-1]:queries[(key<<(48-bits))|suffix]=None
 for mac in queries:
  if (mac>>40)&1:expected=(0,'NONE','GROUP ADDRESS')
  elif (mac>>40)&2:expected=(0,'NONE','LOCALLY ADMINISTERED')
  else:
   expected=(0,'NONE','UNKNOWN')
   for bits in [36,28,24]:
    if (item:=groups[bits].get(mac>>(48-bits))) is not None:
     names,regs=item;expected=(bits,'IAB' if 'IAB' in regs else next(iter(regs)),next(iter(names)) if len(names)==1 else 'MULTIPLE IEEE REGISTRANTS');break
  queries[mac]=expected
 r=subprocess.run([sys.argv[2],'--stdin'],input=''.join(f'{m:012X}\n' for m in queries),text=True,capture_output=True,check=True)
 lines=r.stdout.splitlines();assert len(lines)==len(queries)
 for (mac,expected),line in zip(queries.items(),lines):
  bits,registry,name=line.split('\t',2);assert (int(bits),registry,name)==expected,(hex(mac),expected,line)
 print(f'full CSV oracle: {len(queries)} prefix boundary addresses PASS')
