import {chromium} from 'playwright-core';
import chromiumBinary from '@sparticuz/chromium';
import {readFile} from 'node:fs/promises';
import assert from 'node:assert/strict';
import {unzipSync,strFromU8} from 'fflate';
const browser=await chromium.launch({executablePath:await chromiumBinary.executablePath(),args:chromiumBinary.args,headless:true});
try {
 const page=await browser.newPage({acceptDownloads:true,viewport:{width:1280,height:800}});const errors=[];
 page.on('pageerror',e=>errors.push(e.message));page.on('console',m=>{if(m.type()==='error' && m.text().includes('Content Security Policy'))errors.push(m.text());});
 const response=await page.goto('https://bizuming1-ui.github.io/xingyi-isolated-test/device/');assert.equal(response.status(),200);
 await page.getByText('以下两份代码仅供硬件验证，不能连接界面1').waitFor();
 const promise=page.waitForEvent('download');await page.getByRole('button',{name:'下载候选项目 ZIP'}).click();
 const download=await promise;assert.equal(download.suggestedFilename(),'xingyi-device-candidate.zip');
 const archive=unzipSync(await readFile(await download.path()));
 assert.equal(strFromU8(archive['xingyi-device/production/src/main.cpp']),await readFile('firmware/production/src/main.cpp','utf8'));
 assert.equal(strFromU8(archive['xingyi-device/production/src/config.example.h']),await readFile('firmware/production/src/config.example.h','utf8'));
 assert.ok(archive['xingyi-device/include/SyncPlanner.h']);assert.ok(!Object.keys(archive).some(n=>n.includes('config.local.h')));assert.deepEqual(errors,[]);
 console.log('live public device page downloaded and verified candidate ZIP');
} finally {await browser.close();}
