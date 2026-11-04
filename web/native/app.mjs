import {Transport} from './vendor/esptool-js-0.7.0.js';
import {inspectDevice,loadParts,install,validateManifest} from './flash.mjs?v=20261103-3';
import {C5Loader} from './c5-loader.mjs?v=20261103-3';
const $=id=>document.getElementById(id);
let loader,transport,manifest,base,busy=false;
function status(s){$('status').textContent=s;}
function log(s){$('log').textContent=($('log').textContent+s+'\n').slice(-18000);}
function controls(){const selected=!!$('board').value;const ready=selected&&$('ack').checked&&'serial'in navigator&&window.isSecureContext;$('connect').disabled=!ready||busy||!!loader;$('install').disabled=!loader||!manifest||busy;$('disconnect').disabled=!transport||busy;$('board').disabled=busy||!!transport;$('ack').disabled=busy||!!transport;}
async function disconnect(){if(transport){try{await transport.disconnect();}catch{}}loader=transport=manifest=null;controls();}
$('board').onchange=async()=>{
 $('package').hidden=true;$('version').textContent='';
 const node=$('board').value==='xiao-c5-node';
 $('boardinfo').textContent=node?'XIAO ESP32-C5: headless scanning node, 8 MB flash. No display or GPS. Not for XIAO C3/C6/S3.':$('board').value==='nm-cyd-c5-engine'?'NM: native 320×240 UI, resistive touch and UART GPS. Flashing and operation confirmed.':'Choose the exact board size. Chip detection cannot distinguish these ESP32-C5 board models.';
 $('migration').textContent=node?'Installs the node partition table and 3 MiB application slot. Existing internal filesystem data may become inaccessible. No full-chip erase is performed.':'First installation replaces the partition table with two 6 MiB application slots. Existing internal filesystem data may become inaccessible. External SD files are not written. No full-chip erase is performed.';
 controls();status('Ready to connect.');
 const key=$('board').value;if(!key)return;
 try{const r=await fetch(new URL(`firmware/${key}/manifest.json`,location.href));if(!r.ok)throw Error('Download unavailable');const m=validateManifest(await r.json());if(key!==$('board').value)return;if(m.boardKey!==key)throw Error('Board manifest mismatch');$('package').href=m.packageUrl;$('package').hidden=false;$('version').textContent=`Commit ${m.commit.slice(0,7)}`;}catch(e){status(e.message);}
};
$('ack').onchange=controls;
$('connect').onclick=async()=>{
 busy=true;controls();$('log').textContent='';status('Choose the board’s USB serial port.');
 try{
  const port=await navigator.serial.requestPort();transport=new Transport(port);
  loader=new C5Loader({transport,baudrate:115200,romBaudrate:115200,terminal:{clean:()=>{},write:log,writeLine:log}});
  base=new URL(`firmware/${$('board').value}/manifest.json`,location.href);
  const response=await fetch(base);if(!response.ok)throw Error('Firmware manifest unavailable');manifest=validateManifest(await response.json());
  if(manifest.boardKey!==$('board').value)throw Error('Board manifest mismatch');
  const info=await inspectDevice(loader,manifest);
  $('version').textContent=`Commit ${manifest.commit.slice(0,7)}`;$('package').href=manifest.packageUrl;$('package').hidden=false;
  status(`${info.chip} · ${info.size} · ${info.mac}. Selected: ${$('board').selectedOptions[0].text}. Ready to install.`);
 }catch(e){await disconnect();status(`Connection stopped: ${e.message}`);}
 finally{busy=false;controls();}
};
$('install').onclick=async()=>{
 busy=true;controls();$('progress').value=0;status('Downloading and verifying all four firmware files…');
 try{
  const files=await loadParts(manifest,base);
  status('Installing. Keep USB connected.');
  await install(loader,manifest,files,(i,w,t)=>{$('progress').value=(i+w/t)*25;status(`Writing file ${i+1}/4 — ${Math.floor(w/t*100)}%`);});
  $('progress').value=100;status('Verified: all four images hashed OK. The board stays in download mode — press its RESET button to start umbravox.');
 }catch(e){status(`Installation stopped: ${e.message}`);log(e.stack||String(e));}
 finally{await disconnect();busy=false;controls();}
};
$('disconnect').onclick=async()=>{await disconnect();status('Disconnected.');};
if(!('serial'in navigator)||!window.isSecureContext)status('Open this HTTPS page in desktop Chrome or Edge to install. Safari and Firefox do not support this installer.');
controls();
