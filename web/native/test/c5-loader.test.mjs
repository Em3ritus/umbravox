import {test} from 'node:test';
import assert from 'node:assert/strict';
import {C5Loader} from '../c5-loader.mjs';
import {ESPLoader} from '../vendor/esptool-js-0.7.0.js';
function fixture(Type=C5Loader,chip='ESP32-C5',stub=false) {
  const l=Object.create(Type.prototype);
  Object.assign(l,{chip:{CHIP_NAME:chip},IS_STUB:stub,FLASH_WRITE_SIZE:1024,ESP_FLASH_DEFL_BEGIN:0x10,DEFAULT_TIMEOUT:3000,ERASE_REGION_TIMEOUT_PER_MB:30000,info:()=>{},debug:()=>{}});
  return l;
}
test('real vendor C5 ROM builder needs fifth word; adapter preserves all four fields',async()=>{
  for(const Type of [ESPLoader,C5Loader]) {
    const l=fixture(Type);let packet;
    l.command=async(op,data)=>{assert.equal(op,0x10);packet=data;return [0,new Uint8Array(data.length===20?[0,0]:[1,5])];};
    if(Type===ESPLoader) await assert.rejects(l.flashDeflBegin(20800,13140,0x2000),/status 1,5/);
    else {assert.equal(await l.flashDeflBegin(20800,13140,0x2000),13);assert.equal(packet.length,20);assert.deepEqual(Array.from({length:5},(_,i)=>new DataView(packet.buffer).getUint32(i*4,true)),[21504,13,1024,8192,0]);}
  }
});
test('stub and other ROM packet formats unchanged',async()=>{
  for(const [chip,stub,len] of [['ESP32-C5',true,16],['ESP32-S3',false,20],['ESP32',false,16]]) {
    const l=fixture(C5Loader,chip,stub);l.command=async(op,data)=>{assert.equal(data.length,len);return [0,new Uint8Array([0,0])];};await l.flashDeflBegin(20800,13140,8192);
  }
});
test('C5 ROM error propagates without retry or reset',async()=>{
  const l=fixture();let calls=0;l.command=async()=>{calls++;return [0,new Uint8Array([1,5])];};await assert.rejects(l.flashDeflBegin(20800,13140,8192),/status 1,5/);assert.equal(calls,1);
});
// Real initialization: build via the actual constructor so FLASH_WRITE_SIZE, IS_STUB
// and ESP_FLASH_DEFL_BEGIN come from the code, not from a fabricated fixture. This is
// the ROM packet the hardware receives; it fails on the stub-sized 0x4000 block size.
test('real C5Loader constructor emits the Python ROM FLASH_DEFL_BEGIN packet',async()=>{
  const loader=new C5Loader({transport:{getInfo:()=>({})},baudrate:115200,romBaudrate:115200,
    terminal:{clean(){},write(){},writeLine(){}}});
  loader.chip={CHIP_NAME:'ESP32-C5'}; // state after detectChip(); installer never loads a stub
  assert.equal(loader.IS_STUB,false);
  let pkt;
  loader.command=async(op,d)=>{pkt={op,len:d.length,
    fields:Array.from({length:d.length/4},(_,i)=>new DataView(d.buffer,d.byteOffset,d.byteLength).getUint32(i*4,true))};
    return [0,new Uint8Array([0,0])];};
  await loader.flashDeflBegin(20800,13140,0x2000); // bootloader.bin @0x2000
  assert.equal(pkt.op,0x10);
  assert.equal(pkt.len,20); // four ROM params + encryption word
  assert.deepEqual(pkt.fields,[21504,13,1024,8192,0]); // erase_size,num_blocks,block_size(0x400),offset,enc
});
