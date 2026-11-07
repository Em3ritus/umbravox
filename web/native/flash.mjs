// Four-file ROM installation. No merged-image padding, full erase or RAM stub.
export function validateManifest(m) {
  const expected = [['bootloader.bin',0x2000],['partitions.bin',0x8000],['boot_app0.bin',0xe000],['firmware.bin',0x10000]];
  const node = m.boardKey === 'xiao-c5-node';
  const handheld = ['nm-cyd-c5-engine','ws-c5-35-engine','ws-c5-28-engine'].includes(m.boardKey);
  if ((!node && !handheld) || m.chip !== 'ESP32-C5' || m.flashBytes !== (node ? 8388608 : 16777216) || m.parts?.length !== 4) throw Error('Unsupported manifest');
  m.parts.forEach((p,i)=> {
    if(p.path !== expected[i][0] || p.offset !== expected[i][1] || !Number.isInteger(p.bytes) || p.bytes <= 0 || p.bytes % 4 || !/^[a-f0-9]{64}$/.test(p.sha256) || !/^[a-f0-9]{32}$/.test(p.md5)) throw Error('Invalid firmware part');
    if(p.bytes > [0x6000,0x1000,0x2000,node ? 0x300000 : 0x600000][i]) throw Error('Firmware part exceeds its region');
  });
  return m;
}
export async function inspectDevice(loader, manifest) {
  const minimum = manifest ? validateManifest(manifest).flashBytes : 16777216;
  await loader.detectChip(); // Deliberately do not call main(): it uploads a stub.
  if(loader.chip.CHIP_NAME !== 'ESP32-C5' || loader.secureDownloadMode) throw Error('Requires an ESP32-C5 in normal download mode');
  if(loader.chip.postConnect) await loader.chip.postConnect(loader);
  await loader.flashSpiAttach(0);
  const size = await loader.detectFlashSize();
  const capacity = /^(8|16|32|64|128)MB$/.test(size || '') ? Number(size.slice(0,-2))*1024*1024 : 0;
  if(capacity < minimum) throw Error(`Requires at least ${minimum/1048576}MB flash; detected ${size || 'unknown'}. Nothing written.`);
  // Configure the ROM's flash geometry with the detected capacity BEFORE any FLASH_(DEFL_)BEGIN.
  // Without this the C5 ROM keeps its default geometry and rejects the full app-region erase
  // (status 1,6). A failure here throws, so install()/writeFlash never runs.
  await loader.flashSetParameters(capacity);
  return {chip:loader.chip.CHIP_NAME, size, mac:await loader.chip.readMac(loader)};
}
export async function loadParts(manifest, base, fetcher=fetch, cryptoApi=crypto) {
  validateManifest(manifest);
  const files=[];
  for(const p of manifest.parts) {
    const r=await fetcher(new URL(p.path,base));
    if(!r.ok) throw Error(`Download failed: ${p.path}`);
    const data=new Uint8Array(await r.arrayBuffer());
    const digest=new Uint8Array(await cryptoApi.subtle.digest('SHA-256',data));
    const hex=Array.from(digest,b=>b.toString(16).padStart(2,'0')).join('');
    if(data.length !== p.bytes || hex !== p.sha256) throw Error(`Integrity check failed: ${p.path}. Nothing written.`);
    files.push({data,address:p.offset});
  }
  return files;
}
export async function install(loader,manifest,files,progress) {
  validateManifest(manifest);
  if(files.length!==4 || files.some((f,i)=>f.address!==manifest.parts[i].offset || f.data.length!==manifest.parts[i].bytes)) throw Error('Invalid write plan');
  await loader.writeFlash({fileArray:files,flashSize:'keep',flashMode:'keep',flashFreq:'keep',eraseAll:false,compress:true,reportProgress:progress});
  for(const p of manifest.parts) {
    let actual=await loader.flashMd5sum(p.offset,p.bytes);
    // esptool-js hex-encodes the ROM's 32 ASCII MD5 bytes (stub returns 16 binary bytes).
    if(/^[a-f0-9]{64}$/i.test(actual)) actual=String.fromCharCode(...actual.match(/../g).map(x=>parseInt(x,16)));
    if(actual.toLowerCase()!==p.md5) throw Error(`Flash verification failed: ${p.path}. Do not disconnect; retry installation.`);
  }
  // No automatic reset: the bundled RTS reset does not reliably restart a C5 over
  // USB-Serial/JTAG, and attempting it here would let a post-verify failure mask a
  // successful install. The caller reports verification; the user presses RESET.
}
