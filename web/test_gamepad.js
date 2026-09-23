const assert=require('assert');const {mapGamepad}=require('./gamepad.js');
const buttons=Array.from({length:16},()=>({pressed:false,value:0}));buttons[0].pressed=true;buttons[12].pressed=true;
assert.deepEqual(mapGamepad({axes:[0,0],buttons}),[1,0,0,0,1,0]);
assert.deepEqual(mapGamepad({axes:[-.8,.9],buttons:[]}),[0,1,1,0,0,0]);console.log('gamepad tests passed');
