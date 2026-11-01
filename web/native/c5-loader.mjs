import {ESPLoader} from './vendor/esptool-js-0.7.0.js';

// Two esptool-js 0.7.0 ROM incompatibilities for ESP32-C5 in no-stub flashing:
//  1. FLASH_WRITE_SIZE defaults to the stub's 0x4000, but the C5 ROM download
//     loader expects 0x400 (Python esptool's ROM value). Sending 0x4000 as the
//     FLASH_DEFL_BEGIN block size makes the ROM reject the command (status 1,5).
//  2. esptool-js omits the ROM encryption word for C5 (stale {S2,S3,C3,C2}
//     list); Python appends it for every SUPPORTS_ENCRYPTED_FLASH chip in ROM.
// This installer is ROM-only (detectChip, never main()/stub), so pin the ROM
// block size and scope the extended-param word to C5 ROM. Pinned vendor unchanged.
export class C5Loader extends ESPLoader {
  constructor(options) {
    super(options);
    this.FLASH_WRITE_SIZE = 0x400; // ROM download block size (esptool-js default is the stub's 0x4000)
  }
  async checkCommand(description, op, data, ...args) {
    if (this.chip?.CHIP_NAME === 'ESP32-C5' && this.IS_STUB === false &&
        op === this.ESP_FLASH_DEFL_BEGIN && data?.length === 16) {
      const extended = new Uint8Array(20);
      extended.set(data);
      data = extended;
    }
    return super.checkCommand(description, op, data, ...args);
  }
}
