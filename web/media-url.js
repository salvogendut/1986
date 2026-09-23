(function(root,factory){const api=factory();if(typeof module==='object'&&module.exports)module.exports=api;root.JS1986Media=api;}(typeof globalThis!=='undefined'?globalThis:this,function(){
  'use strict';
  function httpUrl(value,base,name){if(!value)return null;let url;try{url=new URL(value,base);}catch(_){throw new Error(name+' is not a valid URL');}if(!/^https?:$/.test(url.protocol))throw new Error(name+' must use HTTP or HTTPS');return url.href;}
  function parseStartupMedia(search,base){const p=new URLSearchParams(search||'');return{disk:httpUrl(p.get('disk'),base,'disk'),tape:httpUrl(p.get('tape'),base,'tape'),cartridge:httpUrl(p.get('cartridge'),base,'cartridge'),realDrive:p.get('drive')==='real'};}
  function filenameFromUrl(value,fallback){try{const s=new URL(value).pathname.split('/').pop();return decodeURIComponent(s)||fallback;}catch(_){return fallback;}}
  return{parseStartupMedia,filenameFromUrl};
}));
