# Javascript 1986

This is the browser host for the same C128DCR core used by the desktop application. It provides VIC-IIe and VDC video, SID audio, keyboard and gamepad input, one disk drive, one datasette, and one native C128 cartridge slot.

Build inside the development container:

```sh
make -C web EMCC=/var/home/salvogendut/emsdk/upstream/emscripten/emcc
make -C web serve
```

Then open `http://localhost:8080`. The normal configuration uses the fast virtual drive. The Real 1571 switch restarts the emulated machine with the ROM-backed drive core.

Startup links may use `?disk=https://…/disk.d64`, `tape=`, `cartridge=`, and `drive=real`. Remote media servers must permit cross-origin requests.

The ROM files are embedded into the generated WebAssembly package and are not exposed as downloadable items by the interface. Do not publish a build unless you have permission to distribute its configured ROM set.
