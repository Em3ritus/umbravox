import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BOARD_INFO, usbFilters, boardWords } from '../js/boards.js';
import { CH340, ESP_NATIVE_USB } from '../js/usbinstall.js';

test('each board names its USB-serial chip, and the filters follow the board', () => {
  assert.deepEqual(usbFilters('cyd', false), [CH340]);
  assert.deepEqual(usbFilters('waveshare', false), [ESP_NATIVE_USB]);
  assert.deepEqual(usbFilters('cyd', true), undefined, 'show every port: no filter at all');
  assert.deepEqual(usbFilters('waveshare', true), undefined);
  assert.deepEqual(ESP_NATIVE_USB, { usbVendorId: 0x303a, usbProductId: 0x1001 }, 'the ESP32-C5 USB-Serial/JTAG unit');
  assert.throws(() => usbFilters('toaster', false), /board/);
});

test('the words on the install page come from the board, never a hard-coded CYD', () => {
  for (const b of Object.keys(BOARD_INFO)) {
    const w = boardWords(b);
    for (const k of ['name', 'short', 'portLabel', 'restart', 'connect', 'plug']) assert.ok(typeof w[k] === 'string' && w[k].length, `${b}.${k}`);
  }
  assert.match(boardWords('waveshare').name, /Waveshare/);
  assert.doesNotMatch(JSON.stringify(boardWords('waveshare')), /CYD|CH340/);
  assert.match(boardWords('cyd').portLabel, /CH340/);
  assert.match(boardWords('waveshare').restart, /BOOT|RST|reset|unplug/i);
});

test('the dial preset note is only for boards where the preset matters', () => {
  assert.equal(BOARD_INFO.cyd.dialPreset, true);
  assert.equal(BOARD_INFO.waveshare.dialPreset, false);
});
