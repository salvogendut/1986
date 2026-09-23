(function(root,factory){const api=factory();if(typeof module==='object'&&module.exports)module.exports=api;root.JS1986Gamepad=api;}(typeof globalThis!=='undefined'?globalThis:this,function(){
  'use strict';
  const pressed=(pad,n)=>!!(pad.buttons&&pad.buttons[n]&&(pad.buttons[n].pressed||pad.buttons[n].value>.5));
  const axis=(pad,n)=>Number.isFinite(pad.axes&&pad.axes[n])?pad.axes[n]:0;
  function mapGamepad(pad){
    const state=[0,0,0,0,0,0],x=axis(pad,0),y=axis(pad,1);
    state[0]=+(y<-.5||pressed(pad,12)); state[1]=+(y>.5||pressed(pad,13));
    state[2]=+(x<-.5||pressed(pad,14)); state[3]=+(x>.5||pressed(pad,15));
    state[4]=+pressed(pad,0); state[5]=+(pressed(pad,1)||pressed(pad,2)||pressed(pad,3));
    return state;
  }
  return {mapGamepad};
}));
