import {chromium} from 'playwright-core';
import chromiumBinary from '@sparticuz/chromium';
import {readFile} from 'node:fs/promises';
import assert from 'node:assert/strict';
import {resolve} from 'node:path';
const base=process.env.TEST_BASE_URL;
const browser=await chromium.launch({executablePath:await chromiumBinary.executablePath(),args:chromiumBinary.args,headless:true});
try {
 const page=await browser.newPage({acceptDownloads:true});
 for(const viewport of [{width:1280,height:800},{width:390,height:844}]) {
  await page.setViewportSize(viewport);
  await page.goto(`${base}/device/`);await page.getByText('仅供硬件验证，不能用于生产计数').waitFor();
  const ini=await readFile(resolve('firmware/hardware-test/platformio.ini'),'utf8');
  const cpp=await readFile(resolve('firmware/hardware-test/src/main.cpp'),'utf8');
  assert.equal(await page.getByLabel('platformio.ini 代码').inputValue(),ini);
  assert.equal(await page.getByLabel('src/main.cpp 代码').inputValue(),cpp);
  const downloadPromise=page.waitForEvent('download');await page.getByRole('button',{name:'下载文件'}).nth(1).click();
  const download=await downloadPromise;assert.equal(download.suggestedFilename(),'main.cpp');
  assert.equal(await readFile(await download.path(),'utf8'),cpp);
  await page.getByRole('button',{name:'复制代码'}).first().click();
  await page.waitForFunction(()=>document.querySelector('[role=status]')?.textContent?.length>0);
  assert.ok(['已复制','请手动复制已选中的代码'].includes(await page.getByRole('status').first().textContent()));
  assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth <= innerWidth),true);
 }
 await page.close();
 console.log('hardware download page mobile and desktop tests passed');
} finally {await browser.close();}
