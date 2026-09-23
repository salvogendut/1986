const assert=require('assert');
const fs=require('fs');
const path=require('path');
const create1986=require('./dist/1986.js');
const wasmPath=path.join(__dirname,'dist','1986.wasm');

create1986({
  locateFile:name=>path.join(__dirname,'dist',name),
  wasmBinary:fs.readFileSync(wasmPath),
}).then(Module=>{
  assert.equal(Module._poc_init(),0);
  assert.equal(Module._poc_display(),80);
  assert.equal(Module._poc_width(),640);
  for(let i=0;i<10;i++)Module._poc_step();
  assert(Module._poc_pixels()>0);
  assert(Module._poc_audio_avail()>0);
  Module._poc_set_display(40);
  assert.equal(Module._poc_display(),40);
  assert.equal(Module._poc_width(),384);
  assert.equal(Module._poc_set_real_drive(1),0);
  assert.equal(Module._poc_real_drive(),1);
  console.log('WASM machine smoke test passed');
}).catch(error=>{console.error(error);process.exitCode=1;});
