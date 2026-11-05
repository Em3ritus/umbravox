// What differs between the boards on the install side. Pure data and lookups (no DOM), so Node can test them.
// The pack itself is identical for both boards (same two files, same device loader); only the preview, the USB port the
// browser should offer, and the words around it change.
import { CH340, ESP_NATIVE_USB } from './usbinstall.js';

export const BOARD_INFO = {
  cyd: {
    name: 'the CYD', short: 'CYD', portLabel: "the CYD's CH340", filters: [CH340],
    connect: 'Connect to the CYD...', plug: 'Plug the CYD into this computer with its USB cable, using the USB-C port that goes to the CH340 chip (the other USB-C port has no console).',
    restart: 'Restart the CYD yourself: press its reset (RST) button, or unplug the USB cable and plug it back in. This page does not restart the board.',
    dialPreset: true,
  },
  waveshare: {
    name: 'the Waveshare board', short: 'Waveshare', portLabel: "the Waveshare's native USB (Espressif 303a:1001)", filters: [ESP_NATIVE_USB],
    connect: 'Connect to the Waveshare...', plug: 'Plug the Waveshare into this computer with the USB-C cable. Its console is the chip\'s own USB port, so the browser lists it as an Espressif USB JTAG/serial debug unit.',
    restart: 'Restart the Waveshare yourself: tap its reset button, or unplug the USB cable and plug it back in. The port disappears and comes back when the board restarts, which is normal. This page does not restart the board.',
    dialPreset: false,
  },
};

const need = (board) => { const b = BOARD_INFO[board]; if (!b) throw new Error('unknown board ' + board); return b; };
// The `filters` option for navigator.serial.requestPort: undefined (no filter) when showing every port.
export const usbFilters = (board, showAll) => (showAll ? undefined : need(board).filters);
export const boardWords = (board) => { const { name, short, portLabel, connect, plug, restart } = need(board); return { name, short, portLabel, connect, plug, restart }; };
