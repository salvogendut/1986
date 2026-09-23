const fs=require('fs'),assert=require('assert');const html=fs.readFileSync(__dirname+'/index.html','utf8'),app=fs.readFileSync(__dirname+'/app.js','utf8');
for(const id of ['screen','diskFile','tapeFile','cartFile','realDrive','keyboard','switchDisplay'])assert(html.includes(`id="${id}"`),id);
for(const theme of ['c128-dcr','retro-crt','sapporo','sapporo-dark'])assert(html.includes(`value="${theme}"`),theme);
assert(app.includes('_poc_set_real_drive'));assert(app.includes('_poc_set_display'));console.log('UI contract tests passed');
