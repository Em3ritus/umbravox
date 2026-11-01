import {test} from 'node:test';
import assert from 'node:assert/strict';
import {webcrypto,createHash} from 'node:crypto';
import {validateManifest,inspectDevice,loadParts,install} from '../flash.mjs';
const data=new Uint8Array([1,2,3,4]);
const m={chip:'ESP32-C5',flashBytes:16777216,parts:['bootloader.bin','partitions.bin','boot_app0.bin','firmware.bin'].map((path,i)=>({path,offset:[8192,32768,57344,65536][i],bytes:4,sha256:createHash('sha256').update(data).digest('hex'),md5:createHash('md5').update(data).digest('hex')}))};
const fetcher=async()=>({ok:true,arrayBuffer:async()=>data.buffer});
function loader(size='16MB',chip='ESP32-C5'){return {chip:{CHIP_NAME:chip,readMac:async()=> '02:00:00:00:00:01'},detectChip:async()=>{},flashSpiAttach:async()=>{},detectFlashSize:async()=>size};}
test('reject wrong chip and small flash before write',async()=>{await assert.rejects(inspectDevice(loader('8MB')));await assert.rejects(inspectDevice(loader('16MB','ESP32-S3')));await inspectDevice(loader());});
test('reject wrong offsets and oversized image',()=>{for(const p of [{offset:0},{bytes:0x700000}]){const x=structuredClone(m);Object.assign(x.parts[0],p);assert.throws(()=>validateManifest(x));}});
test('download verifies all files before any write',async()=>{const files=await loadParts(m,'https://example.test/manifest.json',fetcher,webcrypto);assert.equal(files.length,4);const x=structuredClone(m);x.parts[3].sha256='0'.repeat(64);await assert.rejects(loadParts(x,'https://example.test/',fetcher,webcrypto));});
test('ROM plan retains image headers, skips full erase, verifies every file',async()=>{const l=loader();let verified=0,reset=false;l.writeFlash=async o=>{assert.equal(o.eraseAll,false);assert.equal(o.flashSize,'keep');assert.equal(o.flashMode,'keep');assert.equal(o.flashFreq,'keep');assert.deepEqual(o.fileArray.map(f=>f.address),[8192,32768,57344,65536]);};l.flashMd5sum=async()=>{verified++;return Buffer.from(m.parts[0].md5).toString('hex');};l.after=async()=>{reset=true;};await install(l,m,await loadParts(m,'https://example.test/',fetcher,webcrypto),()=>{});assert.equal(verified,4);assert.ok(reset);});
test('verification failure cannot report success or reset',async()=>{const l=loader();l.writeFlash=async()=>{};l.flashMd5sum=async()=> '0'.repeat(32);l.after=async()=>assert.fail('must not reset');await assert.rejects(install(l,m,await loadParts(m,'https://example.test/',fetcher,webcrypto),()=>{}));});
