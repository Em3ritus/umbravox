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
export class C5Loader extends ESPLoader {
  constructor(options) {
    super(options);
    this.FLASH_WRITE_SIZE = 0x400; // ROM download block size (vendor default is the stub's 0x4000)
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
