"use strict";
const $=id=>document.getElementById(id);
const CODE2SCAN={KeyA:4,KeyB:5,KeyC:6,KeyD:7,KeyE:8,KeyF:9,KeyG:10,KeyH:11,KeyI:12,KeyJ:13,KeyK:14,KeyL:15,KeyM:16,KeyN:17,KeyO:18,KeyP:19,KeyQ:20,KeyR:21,KeyS:22,KeyT:23,KeyU:24,KeyV:25,KeyW:26,KeyX:27,KeyY:28,KeyZ:29,Digit1:30,Digit2:31,Digit3:32,Digit4:33,Digit5:34,Digit6:35,Digit7:36,Digit8:37,Digit9:38,Digit0:39,Enter:40,Escape:41,Backspace:42,Tab:43,Space:44,Minus:45,Equal:46,BracketLeft:47,BracketRight:48,Backslash:49,Semicolon:51,Quote:52,Backquote:53,Comma:54,Period:55,Slash:56,CapsLock:57,F1:58,F2:59,F3:60,F4:61,F5:62,F6:63,F7:64,F8:65,F9:66,F10:67,Home:74,PageUp:75,Delete:76,ArrowRight:79,ArrowLeft:80,ArrowDown:81,ArrowUp:82,NumpadEnter:88,Numpad1:89,Numpad2:90,Numpad3:91,Numpad4:92,Numpad5:93,Numpad6:94,Numpad7:95,Numpad8:96,Numpad9:97,Numpad0:98,NumpadDecimal:99,ControlLeft:224,ShiftLeft:225,AltLeft:226,ControlRight:228,ShiftRight:229,AltRight:230};
const canvas=$("screen"),ctx=canvas.getContext("2d",{alpha:false}),statusEl=$("status"),toastEl=$("toast");
let Module,framePtr=0,image=null,mediaSerial=0,audioCtx=null,audioNode=null,lastTime=performance.now(),frameDebt=0,gamepadState=[0,0,0,0,0,0];
const mounted={disk:null,tape:null,cart:null};
const DISPLAY_STORAGE_KEY="javascript1986.display";

function status(s){statusEl.textContent=s;}
let toastTimer;function toast(s){toastEl.textContent=s;toastEl.classList.add("show");clearTimeout(toastTimer);toastTimer=setTimeout(()=>toastEl.classList.remove("show"),2600);}
function applyTheme(name){const names=["c128-dcr","retro-crt","sapporo","sapporo-dark"];if(!names.includes(name))name="c128-dcr";document.documentElement.dataset.theme=name;$("theme").value=name;try{localStorage.setItem("javascript1986.theme",name);}catch(_){}}
try{applyTheme(localStorage.getItem("javascript1986.theme")||"c128-dcr");}catch(_){applyTheme("c128-dcr");}
$("theme").addEventListener("change",e=>applyTheme(e.target.value));

function setDisplayUi(){const cols=Module._poc_display();$("displayState").textContent=cols===80?"80-column VDC":"40-column VIC-IIe";canvas.setAttribute("aria-label",$("displayState").textContent+" display");}
function switchDisplay(){Module._poc_set_display(Module._poc_display()===80?40:80);try{localStorage.setItem(DISPLAY_STORAGE_KEY,String(Module._poc_display()));}catch(_){}setDisplayUi();toast($("displayState").textContent);canvas.focus();}

function draw(){
  const w=Module._poc_width(),h=Module._poc_height(),ptr=Module._poc_pixels();
  if(canvas.width!==w||canvas.height!==h||!image){canvas.width=w;canvas.height=h;image=ctx.createImageData(w,h);}
  if(!image||ptr!==framePtr)framePtr=ptr;
  const src=Module.HEAPU32.subarray(ptr>>>2,(ptr>>>2)+w*h),dst=image.data;
  for(let i=0,j=0;i<src.length;i++,j+=4){const p=src[i];dst[j]=(p>>>16)&255;dst[j+1]=(p>>>8)&255;dst[j+2]=p&255;dst[j+3]=255;}
  ctx.putImageData(image,0,0);
  const activity=Module._poc_activity();
  $("driveLed").classList.toggle("active",!!(activity&1));
  $("cpuLed").classList.toggle("active",!!(activity&4));
  $("z80Led").classList.toggle("active",!!(activity&8));
  setDisplayUi();
}

function pollGamepad(){const pad=[...navigator.getGamepads()].find(Boolean);const next=pad&&globalThis.JS1986Gamepad?JS1986Gamepad.mapGamepad(pad):[0,0,0,0,0,0];for(let i=0;i<6;i++)if(next[i]!==gamepadState[i])Module._poc_joy(i,next[i]);gamepadState=next;}
function loop(now){frameDebt+=Math.min(100,now-lastTime);lastTime=now;while(frameDebt>=20){Module._poc_step();frameDebt-=20;}pollGamepad();draw();requestAnimationFrame(loop);}

function startAudio(){
  if(audioCtx){audioCtx.resume();return;}
  const AC=window.AudioContext||window.webkitAudioContext;if(!AC)return;
  audioCtx=new AC({sampleRate:44100});audioNode=audioCtx.createScriptProcessor(1024,0,1);
  audioNode.onaudioprocess=e=>{const out=e.outputBuffer.getChannelData(0);let avail=Module._poc_audio_avail(),pos=Module._poc_audio_read_pos(),take=Math.min(avail,out.length),ring=Module._poc_audio_buffer()>>>1;for(let i=0;i<take;i++)out[i]=Module.HEAP16[ring+((pos+i)%(44100*4))]/32768;for(let i=take;i<out.length;i++)out[i]=0;Module._poc_audio_advance(take);};
  audioNode.connect(audioCtx.destination);
}

function inputActive(){return document.activeElement===canvas||document.activeElement.closest?.(".keyboard");}
document.addEventListener("keydown",e=>{if(!inputActive())return;if(e.code==="F10"){e.preventDefault();switchDisplay();return;}const scan=CODE2SCAN[e.code];if(scan===undefined)return;e.preventDefault();if(!e.repeat)Module._poc_key(scan,1);startAudio();});
document.addEventListener("keyup",e=>{const scan=CODE2SCAN[e.code];if(scan===undefined||!inputActive())return;e.preventDefault();Module._poc_key(scan,0);});
window.addEventListener("blur",()=>{for(const scan of Object.values(CODE2SCAN))Module?._poc_key(scan,0);});
canvas.addEventListener("pointerdown",()=>{canvas.focus();startAudio();});

const latched=new Set();function keyButton(button,down){const scan=Number(button.dataset.scan);Module._poc_key(scan,down?1:0);button.classList.toggle("latched",down);}
$("keyboard").addEventListener("pointerdown",e=>{const b=e.target.closest("button");if(!b)return;e.preventDefault();startAudio();if(b.dataset.action==="display"){switchDisplay();return;}if(b.dataset.scan===undefined)return;if(b.hasAttribute("data-modifier")){if(latched.has(b)){latched.delete(b);keyButton(b,false);}else{latched.add(b);keyButton(b,true);}}else{keyButton(b,true);b.setPointerCapture(e.pointerId);}});
function releaseVirtual(e){const b=e.target.closest("button[data-scan]");if(!b||b.hasAttribute("data-modifier"))return;keyButton(b,false);for(const m of latched){const mb=$("keyboard").querySelector(`[data-scan="${m}"]`);if(mb)keyButton(mb,false);}latched.clear();}
$("keyboard").addEventListener("pointerup",releaseVirtual);$("keyboard").addEventListener("pointercancel",releaseVirtual);

$("keyboardToggle").addEventListener("click",()=>{const k=$("keyboard"),show=k.hidden;k.hidden=!show;$("keyboardToggle").setAttribute("aria-expanded",String(show));});
$("switchDisplay").addEventListener("click",switchDisplay);
$("reset").addEventListener("click",()=>{Module._poc_reset();Module._poc_audio_reset();status("Machine reset");toast("C128DCR reset");canvas.focus();});
$("fullscreen").addEventListener("click",()=>{const target=$("screenStage");if(document.fullscreenElement)document.exitFullscreen();else target.requestFullscreen();});

async function putFile(file,kind){const ext=(file.name.match(/\.[^.]+$/)||[".bin"])[0].toLowerCase(),path=`/media-${kind}-${++mediaSerial}${ext}`;Module.FS.writeFile(path,new Uint8Array(await file.arrayBuffer()));return path;}
async function mount(file,kind){if(!file)return;try{const path=await putFile(file,kind);let rc;if(kind==="disk")rc=Module.ccall("poc_load_disk","number",["string"],[path]);else if(kind==="tape")rc=Module.ccall("poc_load_tape","number",["string"],[path]);else rc=Module.ccall("poc_load_cart","number",["string"],[path]);if(rc!==0)throw new Error("unsupported or damaged image");mounted[kind]=path;$(kind+"Name").textContent=file.name;$(kind+"Eject").disabled=false;if(kind==="tape"){$("tapePlay").disabled=false;$("tapeRewind").disabled=false;}status(`${file.name} loaded`);toast(`${kind} loaded`);canvas.focus();}catch(err){status(`Could not load ${file.name}: ${err.message}`);toast("Media load failed");}}
for(const kind of ["disk","tape","cart"]){$(kind+"File").addEventListener("change",e=>mount(e.target.files[0],kind));$(kind+"Eject").addEventListener("click",()=>{Module["_poc_eject_"+kind]();mounted[kind]=null;$(kind+"Name").textContent=kind==="cart"?"No cartridge":kind==="tape"?"No tape":"No disk";$(kind+"Eject").disabled=true;if(kind==="tape"){$("tapePlay").disabled=true;$("tapeRewind").disabled=true;}status(`${kind} ejected`);});}
$("screenStage").addEventListener("dragover",e=>{e.preventDefault();e.dataTransfer.dropEffect="copy";});
$("screenStage").addEventListener("drop",e=>{e.preventDefault();const file=e.dataTransfer.files[0];if(!file)return;const ext=(file.name.match(/\.[^.]+$/)||[""])[0].toLowerCase();const kind=[".d64",".d71",".d81",".prg"].includes(ext)?"disk":[".tap",".t64"].includes(ext)?"tape":[".crt",".bin",".rom"].includes(ext)?"cart":null;if(kind)mount(file,kind);else toast("Unsupported media type");});
$("tapePlay").addEventListener("click",()=>{Module._poc_tape_control(1);startAudio();status("Tape playing");});$("tapeRewind").addEventListener("click",()=>{Module._poc_tape_control(2);status("Tape rewound");});
$("realDrive").addEventListener("change",e=>{const wanted=e.target.checked;status(`Restarting with ${wanted?"real 1571":"fast virtual"} drive…`);if(Module._poc_set_real_drive(wanted?1:0)!==0){e.target.checked=false;status("Drive mode change failed");return;}e.target.checked=!!Module._poc_real_drive();Module._poc_audio_reset();status(e.target.checked?"Real 1571 drive enabled":"Fast virtual drive enabled");toast(statusEl.textContent);});

async function fetchMedia(url,kind){const response=await fetch(url);if(!response.ok)throw new Error(`HTTP ${response.status}`);const name=JS1986Media.filenameFromUrl(url,kind);return new File([await response.arrayBuffer()],name);}
async function startupMedia(){try{const q=JS1986Media.parseStartupMedia(location.search,location.href);if(q.realDrive){$("realDrive").checked=true;Module._poc_set_real_drive(1);}for(const kind of ["disk","tape","cartridge"]){const url=q[kind];if(url)await mount(await fetchMedia(url,kind),kind==="cartridge"?"cart":kind);}}catch(err){status("Startup media: "+err.message);}}

create1986({locateFile:path=>path}).then(m=>{Module=m;if(m._poc_init()!==0)throw new Error("embedded C128 ROM set could not be loaded");try{const saved=Number(localStorage.getItem(DISPLAY_STORAGE_KEY));if(saved===40||saved===80)m._poc_set_display(saved);}catch(_){}$("realDrive").checked=false;setDisplayUi();status("C128DCR ready — fast virtual drive");startupMedia();requestAnimationFrame(loop);}).catch(err=>{console.error(err);status("Emulator failed to start: "+err.message);});
