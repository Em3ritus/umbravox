import {ESPLoader} from './vendor/esptool-js-0.7.0.js';

// esptool-js 0.7.0 omits C5 from its ROM FLASH_DEFL_BEGIN extension list.
// Espressif Python esptool adds a zero encryption word for this ROM.
// Keep the pinned vendor unchanged and scope the compatibility fix to C5 ROM.
export class C5Loader extends ESPLoader {
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
