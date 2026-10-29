import {chromium} from 'playwright-core';
import chromiumBinary from '@sparticuz/chromium';
import {readFile} from 'node:fs/promises';
import assert from 'node:assert/strict';
import {unzipSync,strFromU8} from 'fflate';
import {resolve} from 'node:path';
const base=process.env.TEST_BASE_URL;
const browser=await chromium.launch({executablePath:await chromiumBinary.executablePath(),args:chromiumBinary.args,headless:true});
try {
 const page=await browser.newPage({acceptDownloads:true});
 for(const viewport of [{width:1280,height:800},{width:390,height:844}]) {
  await page.setViewportSize(viewport);
  await page.goto(`${base}/device/`);await page.getByText('以下两份代码仅供硬件验证，不能连接界面1').waitFor();
  const ini=await readFile(resolve('firmware/hardware-test/platformio.ini'),'utf8');
  const cpp=await readFile(resolve('firmware/hardware-test/src/main.cpp'),'utf8');
  assert.equal(await page.getByLabel('platformio.ini 代码').inputValue(),ini);
  assert.equal(await page.getByLabel('src/main.cpp 代码').inputValue(),cpp);
  const downloadPromise=page.waitForEvent('download');await page.getByRole('button',{name:'下载文件'}).nth(1).click();
  const download=await downloadPromise;assert.equal(download.suggestedFilename(),'main.cpp');
  assert.equal(await readFile(await download.path(),'utf8'),cpp);
  const candidatePromise=page.waitForEvent('download');await page.getByRole('button',{name:'下载候选项目 ZIP'}).click();
  const candidate=await candidatePromise;assert.equal(candidate.suggestedFilename(),'xingyi-device-candidate.zip');
  const archive=unzipSync(await readFile(await candidate.path()));
  assert.equal(strFromU8(archive['xingyi-device/production/src/main.cpp']),await readFile(resolve('firmware/production/src/main.cpp'),'utf8'));
  assert.equal(strFromU8(archive['xingyi-device/production/src/config.example.h']),await readFile(resolve('firmware/production/src/config.example.h'),'utf8'));
  assert.ok(archive['xingyi-device/include/SyncPlanner.h']);
  assert.ok(!Object.keys(archive).some(name=>name.includes('config.local.h')));
  await page.getByRole('button',{name:'复制代码'}).first().click();
  await page.waitForFunction(()=>document.querySelector('[role=status]')?.textContent?.length>0);
  assert.ok(['已复制','请手动复制已选中的代码'].includes(await page.getByRole('status').first().textContent()));
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth <= innerWidth),true);
 }
 await page.close();
 console.log('hardware download page mobile and desktop tests passed');
} finally {await browser.close();}
