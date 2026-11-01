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
// Build the real loader so IS_STUB, FLASH_WRITE_SIZE, opcodes and packet framing all
// come from the code under test; only the transport/command wire is intercepted.
function romLoader(command){
  const loader=new C5Loader({transport:{getInfo:()=>({})},baudrate:115200,romBaudrate:115200,
    terminal:{clean(){},write(){},writeLine(){}}});
  loader.chip={CHIP_NAME:'ESP32-C5'}; // state after detectChip(); installer never loads a stub
  if(command)loader.command=command;
  return loader;
}
test('real C5Loader SPI_ATTACH sends the 8-byte ROM form',async()=>{
  let pkt;const l=romLoader(async(op,d)=>{pkt={op,bytes:[...d]};return [0,new Uint8Array([0,0])];});
  await l.flashSpiAttach(0);
  assert.equal(pkt.op,0x0d);
  assert.deepEqual(pkt.bytes,[0,0,0,0,0,0,0,0]); // arg word + reserved word
});
test('real writeFlash emits DEFL_BEGIN then DEFL_DATA at the ROM block size',async()=>{
  const ops=[];let begin;
  const l=romLoader(async(op,d)=>{const dv=new DataView(d.buffer,d.byteOffset,d.byteLength);
    if(op===0x10)begin=Array.from({length:d.length/4},(_,i)=>dv.getUint32(i*4,true));
    ops.push({op,len:d.length});return [0,new Uint8Array([0,0])];});
  const data=new Uint8Array(0x400*3+37);crypto.getRandomValues(data); // incompressible payload: multiple blocks + a final partial
  await l.writeFlash({fileArray:[{data,address:0x2000}],flashSize:'keep',flashMode:'keep',flashFreq:'keep',eraseAll:false,compress:true});
  assert.equal(ops[0].op,0x10);                 // BEGIN first
  assert.equal(ops[0].len,20);                  // four params + encryption word
  assert.equal(begin[2],1024);                  // block_size = 0x400, not the stub's 0x4000
  assert.equal(begin[3],0x2000);                // offset
  assert.ok(ops.filter(o=>o.op===0x11).length>=2); // multiple DEFL_DATA blocks incl. a final partial
  assert.ok(!ops.some(o=>o.op===0x12));         // ROM path sends no DEFL_END
});
test('real flashMd5sum ROM reply decodes to the hex digest',async()=>{
  const md5='0123456789abcdef0123456789abcdef';
  const resp=new Uint8Array(34);for(let i=0;i<32;i++)resp[i]=md5.charCodeAt(i); // 32 ASCII hex bytes + [0,0] status
  const l=romLoader(async()=>[0,resp]);
  let actual=await l.flashMd5sum(0x2000,1024);
  assert.equal(actual.length,64); // vendor hex-encodes the 32 ASCII bytes
  if(/^[a-f0-9]{64}$/i.test(actual))actual=String.fromCharCode(...actual.match(/../g).map(x=>parseInt(x,16)));
  assert.equal(actual,md5); // install()'s normalization recovers the digest
});
test('real C5Loader DEFL_BEGIN ROM rejection propagates without retry',async()=>{
  let calls=0;const l=romLoader(async()=>{calls++;return [0,new Uint8Array([1,5])];});
  await assert.rejects(l.flashDeflBegin(20800,13140,0x2000),/status 1,5/);
  assert.equal(calls,1);
});
// SPI_SET_PARAMS (0x0b): the pinned vendor has no such command, so the C5 ROM keeps its
// default flash geometry and rejects the ~3 MB app-region erase (status 1,6). flashSetParameters
// mirrors Python's flash_set_parameters <IIIIII> fl_id,total_size,block,sector,page,status_mask.
test('real C5Loader flashSetParameters emits SPI_SET_PARAMS (0x0b) with the six ROM fields',async()=>{
  for(const cap of [0x1000000,0x2000000]){ // 16 MB and 32 MB devices
    let pkt;const l=romLoader(async(op,d)=>{pkt={op,len:d.length,
      fields:Array.from({length:d.length/4},(_,i)=>new DataView(d.buffer,d.byteOffset,d.byteLength).getUint32(i*4,true))};
      return [0,new Uint8Array([0,0])];});
    await l.flashSetParameters(cap);
    assert.equal(pkt.op,0x0b);
    assert.equal(pkt.len,24); // six uint32, no pad
    assert.deepEqual(pkt.fields,[0,cap,0x10000,0x1000,0x100,0xffff]); // fl_id,total,block,sector,page,mask
  }
});
test('real C5Loader SPI_SET_PARAMS failure propagates without retry',async()=>{
  let calls=0;const l=romLoader(async()=>{calls++;return [0,new Uint8Array([1,6])];});
  await assert.rejects(l.flashSetParameters(0x1000000),/status 1,6/);
  assert.equal(calls,1);
});
