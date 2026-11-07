import {test} from 'node:test';
import assert from 'node:assert/strict';
import {webcrypto,createHash} from 'node:crypto';
import {validateManifest,inspectDevice,loadParts,install} from '../flash.mjs';
import {C5Loader} from '../c5-loader.mjs';
const data=new Uint8Array([1,2,3,4]);
const m={boardKey:'ws-c5-35-engine',chip:'ESP32-C5',flashBytes:16777216,parts:['bootloader.bin','partitions.bin','boot_app0.bin','firmware.bin'].map((path,i)=>({path,offset:[8192,32768,57344,65536][i],bytes:4,sha256:createHash('sha256').update(data).digest('hex'),md5:createHash('md5').update(data).digest('hex')}))};
const fetcher=async()=>({ok:true,arrayBuffer:async()=>data.buffer});
function loader(size='16MB',chip='ESP32-C5'){return {chip:{CHIP_NAME:chip,readMac:async()=> '02:00:00:00:00:01'},detectChip:async()=>{},flashSpiAttach:async()=>{},detectFlashSize:async()=>size,flashSetParameters:async()=>{}};}
// Real C5Loader with only the serial-touching chip methods stubbed; flashSetParameters,
// checkCommand and writeFlash are the real code. `cmd` intercepts the command wire.
function realLoader(cmd,flashSize='16MB'){
  const l=new C5Loader({transport:{getInfo:()=>({})},baudrate:115200,romBaudrate:115200,terminal:{clean(){},write(){},writeLine(){}}});
  l.detectChip=async()=>{l.chip={CHIP_NAME:'ESP32-C5',readMac:async()=>'02:00:00:00:00:01'};};
  l.detectFlashSize=async()=>flashSize;l.secureDownloadMode=false;
  if(cmd)l.command=cmd;
  return l;
}
test('reject wrong chip and small flash before write',async()=>{await assert.rejects(inspectDevice(loader('8MB')));await assert.rejects(inspectDevice(loader('16MB','ESP32-S3')));await inspectDevice(loader());});
test('reject wrong offsets and oversized image',()=>{for(const p of [{offset:0},{bytes:0x700000}]){const x=structuredClone(m);Object.assign(x.parts[0],p);assert.throws(()=>validateManifest(x));}});
test('download verifies all files before any write',async()=>{const files=await loadParts(m,'https://example.test/manifest.json',fetcher,webcrypto);assert.equal(files.length,4);const x=structuredClone(m);x.parts[3].sha256='0'.repeat(64);await assert.rejects(loadParts(x,'https://example.test/',fetcher,webcrypto));});
test('ROM plan retains image headers, skips full erase, verifies every file and does not auto-reset',async()=>{const l=loader();let verified=0;l.writeFlash=async o=>{assert.equal(o.eraseAll,false);assert.equal(o.flashSize,'keep');assert.equal(o.flashMode,'keep');assert.equal(o.flashFreq,'keep');assert.deepEqual(o.fileArray.map(f=>f.address),[8192,32768,57344,65536]);};l.flashMd5sum=async()=>{verified++;return Buffer.from(m.parts[0].md5).toString('hex');};l.after=async()=>assert.fail('install must not auto-reset; user presses RESET');await install(l,m,await loadParts(m,'https://example.test/',fetcher,webcrypto),()=>{});assert.equal(verified,4);});
test('verification failure cannot report success or reset',async()=>{const l=loader();l.writeFlash=async()=>{};l.flashMd5sum=async()=> '0'.repeat(32);l.after=async()=>assert.fail('must not reset');await assert.rejects(install(l,m,await loadParts(m,'https://example.test/',fetcher,webcrypto),()=>{}));});

test('accept larger physical flash without expanding write layout',async()=>{for(const size of ['16MB','32MB','64MB','128MB']){assert.equal((await inspectDevice(loader(size))).size,size);}for(const size of ['4MB','8MB',undefined,'unknown','32MBjunk']){await assert.rejects(inspectDevice(loader(size===undefined?'unknown':size)));}});

// SPI_SET_PARAMS wiring (the status-1,6 omission): inspectDevice must configure the ROM with the
// DETECTED physical capacity, before any FLASH_(DEFL_)BEGIN, and abort the whole flow on failure.
test('inspectDevice configures SPI_SET_PARAMS before any flash BEGIN',async()=>{
  const ops=[];const l=realLoader(async(op)=>{ops.push(op);return [0,new Uint8Array([0,0])];});
  await inspectDevice(l);
  assert.ok(ops.includes(0x0b),'SPI_SET_PARAMS sent during inspect');
  assert.ok(!ops.includes(0x10)&&!ops.includes(0x02),'inspect sends no BEGIN');
  await l.writeFlash({fileArray:[{data:new Uint8Array(0x400),address:0x10000}],flashSize:'keep',flashMode:'keep',flashFreq:'keep',eraseAll:false,compress:true});
  assert.ok(ops.indexOf(0x0b)>=0&&ops.indexOf(0x0b)<ops.indexOf(0x10),'SPI_SET_PARAMS precedes FLASH_DEFL_BEGIN');
});
test('inspectDevice sends the detected physical capacity for 16MB and 32MB devices',async()=>{
  for(const [sz,cap] of [['16MB',0x1000000],['32MB',0x2000000]]){
    let total;const l=realLoader(async(op,d)=>{if(op===0x0b)total=new DataView(d.buffer,d.byteOffset,d.byteLength).getUint32(4,true);return [0,new Uint8Array([0,0])];},sz);
    await inspectDevice(l);
    assert.equal(total,cap,sz);
  }
});
test('a SPI_SET_PARAMS failure aborts before any write',async()=>{
  const ops=[];const l=realLoader(async(op)=>{ops.push(op);return [0,new Uint8Array(op===0x0b?[1,6]:[0,0])];});
  await assert.rejects(inspectDevice(l),/status 1,6/);
  assert.ok(ops.includes(0x0b),'attempted config');
  assert.ok(!ops.includes(0x10)&&!ops.includes(0x02),'no BEGIN after config failure');
});

const nodeManifest=()=>({...structuredClone(m),boardKey:'xiao-c5-node',flashBytes:8388608});
test('XIAO accepts 8MB, handhelds still reject 8MB, and both reject wrong chips',async()=>{
 const n=nodeManifest();validateManifest(n);await inspectDevice(loader('8MB'),n);
 await assert.rejects(inspectDevice(loader('8MB'),m),/16MB/);
 await assert.rejects(inspectDevice(loader('4MB'),n),/8MB/);
 await assert.rejects(inspectDevice(loader('8MB','ESP32-S3'),n));
});
test('board identity fixes capacity and application partition bounds',()=>{
 for(const patch of [{boardKey:'unknown'},{flashBytes:16777216}])assert.throws(()=>validateManifest({...nodeManifest(),...patch}));
 const n=nodeManifest();n.parts[3].bytes=0x300004;assert.throws(()=>validateManifest(n),/region/);
 const h=structuredClone(m);h.flashBytes=8388608;assert.throws(()=>validateManifest(h));
});
test('XIAO configures detected 8MB geometry and uses the four verified writes',async()=>{
 const n=nodeManifest();let total;
 const l=realLoader(async(op,d)=>{if(op===0x0b)total=new DataView(d.buffer,d.byteOffset,d.byteLength).getUint32(4,true);return [0,new Uint8Array([0,0])];},'8MB');
 await inspectDevice(l,n);assert.equal(total,8388608);
 let verified=0;l.writeFlash=async o=>{assert.equal(o.eraseAll,false);assert.equal(o.flashSize,'keep');assert.equal(o.fileArray.length,4);};
 l.flashMd5sum=async()=>{verified++;return n.parts[0].md5;};
 await install(l,n,await loadParts(n,'https://example.test/',fetcher,webcrypto),()=>{});assert.equal(verified,4);
});
test('the CYD ships its original UI: a 3 MiB application slot, and no engine build',()=>{
 const c={...structuredClone(m),boardKey:'nm-cyd-c5'};validateManifest(c);
 const big=structuredClone(c);big.parts[3].bytes=0x300004;assert.throws(()=>validateManifest(big),/region/);
 const eng=structuredClone(m);eng.parts[3].bytes=0x300004;validateManifest(eng);
 assert.throws(()=>validateManifest({...structuredClone(m),boardKey:'nm-cyd-c5-engine'}),/Unsupported/);
});
