const assert=require('assert');const media=require('./media-url.js');
const parsed=media.parseStartupMedia('?disk=games/demo.d64&tape=https://example.test/a.tap&drive=real','https://1986.test/app/');
assert.equal(parsed.disk,'https://1986.test/app/games/demo.d64');assert.equal(parsed.tape,'https://example.test/a.tap');assert.equal(parsed.realDrive,true);
assert.throws(()=>media.parseStartupMedia('?disk=file:///tmp/a.d64','https://1986.test/'),/HTTP/);
assert.equal(media.filenameFromUrl('https://x.test/My%20Disk.d64','disk'),'My Disk.d64');console.log('media URL tests passed');
