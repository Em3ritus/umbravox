"""Generate flash-resident IEEE MA-L lookup from an explicitly supplied oui.csv."""
import csv,hashlib,json,sys,unicodedata
from pathlib import Path
source=Path(sys.argv[1]);target=Path(sys.argv[2])
rows=list(csv.DictReader(source.read_text(encoding='utf-8-sig').splitlines()))
entries={};names={};offset=0
for row in rows:
 if row['Registry']!='MA-L':continue
 key=int(row['Assignment'],16)
 name=unicodedata.normalize('NFKD',row['Organization Name']).encode('ascii','ignore').decode()
 if name not in names:names[name]=offset;offset+=len(name)+1
 if key in entries:
  # IEEE retains a few historical shared assignments; do not choose a vendor.
  name='MULTIPLE IEEE REGISTRANTS'
  if name not in names:names[name]=offset;offset+=len(name)+1
 entries[key]=names[name]
text='// Generated from https://standards-oui.ieee.org/oui/oui.csv\n'
text+='// Source SHA256: '+hashlib.sha256(source.read_bytes()).hexdigest()+'\n'
text+='static const char names[] =\n'+''.join(json.dumps(n+'\0')+'\n' for n in names).replace('\\u0000','\\000')+';\n'
text+='struct Entry {uint32_t prefix,offset;};\nstatic constexpr Entry entries[] = {\n'
text+=''.join('{0x%06x,%d},\n'%(k,v) for k,v in sorted(entries.items()))+'};\n'
target.write_text(text)
print(len(entries),'MA-L prefixes;',offset,'name bytes')
