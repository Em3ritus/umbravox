import {ESPLoader} from './vendor/esptool-js-0.7.0.js';

// esptool-js 0.7.0 builds several ROM command packets in their shorter stub form.
// This installer is ROM-only for ESP32-C5 (detectChip, never main()/stub), so match
// what Python esptool sends to the C5 ROM while keeping the pinned vendor unchanged:
//  1. FLASH_WRITE_SIZE: vendor defaults to the stub's 0x4000; the C5 ROM download
//     loader expects 0x400. An oversized block makes FLASH_DEFL_BEGIN fail (status 1,5).
//  2. FLASH_DEFL_BEGIN: vendor omits the ROM encryption word for C5 (stale
//     {S2,S3,C3,C2} list); Python appends it (16 -> 20 bytes).
//  3. SPI_ATTACH: vendor sends one word; the ROM expects a trailing reserved
//     word (4 -> 8 bytes).
//  4. SPI_SET_PARAMS: vendor 0.7.0 has no such command at all. In ROM mode nothing
//     configures the ROM's flashchip geometry, so it keeps its power-on default and
//     rejects the full ~3 MB app-region erase at FLASH_DEFL_BEGIN (status 1,6) while
//     the small sub-0x10000 images pass. Python's attach_flash sends SPI_SET_PARAMS
//     with the real detected size even in flash_size="keep" mode. Mirror it.
export class C5Loader extends ESPLoader {
  constructor(options) {
    super(options);
    this.FLASH_WRITE_SIZE = 0x400; // ROM download block size (vendor default is the stub's 0x4000)
    this.ESP_SPI_SET_PARAMS = 0x0b; // not defined by the pinned vendor
  }
  // Tell the ROM the flash chip geometry, mirroring Python esptool's flash_set_parameters:
  // <IIIIII> fl_id, total_size, block(64K), sector(4K), page(256), status_mask. `size` is the
  // validated detected physical capacity in bytes (not the manifest's partition-layout size).
  async flashSetParameters(size) {
    const fields = [0, size, 0x10000, 0x1000, 0x100, 0xffff];
    const data = new Uint8Array(24);
    const dv = new DataView(data.buffer);
    fields.forEach((v, i) => dv.setUint32(i * 4, v >>> 0, true));
    await this.checkCommand('set SPI params', this.ESP_SPI_SET_PARAMS, data);
  }
  async checkCommand(description, op, data, ...args) {
    if (this.chip?.CHIP_NAME === 'ESP32-C5' && this.IS_STUB === false) {
      let size = 0;
      if (op === this.ESP_FLASH_DEFL_BEGIN && data?.length === 16) size = 20; // + encryption word
      else if (op === this.ESP_SPI_ATTACH && data?.length === 4) size = 8;    // + reserved word
      if (size) { const extended = new Uint8Array(size); extended.set(data); data = extended; }
    }
    return super.checkCommand(description, op, data, ...args);
  }
}
