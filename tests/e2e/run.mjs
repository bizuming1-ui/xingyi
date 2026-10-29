import {spawn, execFileSync} from 'node:child_process';
import {mkdtempSync, readFileSync, writeFileSync, rmSync} from 'node:fs';
import {brotliDecompressSync} from 'node:zlib';
import {tmpdir} from 'node:os';
import {join, resolve} from 'node:path';
import {fileURLToPath} from 'node:url';

const root=resolve(fileURLToPath(new URL('../..',import.meta.url)));
const temp=mkdtempSync(join(tmpdir(),'xingyi-e2e-'));
let server;
try {
  const tar=join(temp,'libs.tar');
  const compressed=readFileSync(join(root,'tests/e2e/node_modules/@sparticuz/chromium/bin/al2023.tar.br'));
  writeFileSync(tar,brotliDecompressSync(compressed));
  execFileSync('tar',['-xf',tar,'-C',temp]);
  server=spawn(process.execPath,[join(root,'node_modules/vite/bin/vite.js'),'--host','127.0.0.1','--port','5174','--strictPort'],{cwd:root,env:{...process.env,VITE_SUPABASE_URL:'https://example.supabase.co',VITE_SUPABASE_ANON_KEY:'test-publishable-key'},stdio:['ignore','pipe','pipe']});
  await new Promise((done,fail)=>{
    const timer=setTimeout(()=>fail(new Error('Vite did not start within 30 seconds')),30000);
    server.stdout.on('data',chunk=>{if(chunk.toString().includes('ready in')){clearTimeout(timer);done()}});
    server.stderr.on('data',chunk=>process.stderr.write(chunk));
    server.on('exit',code=>{clearTimeout(timer);fail(new Error(`Vite exited before startup: ${code}`))});
  });
  const child=spawn(process.execPath,[join(root,'tests/e2e/pr1-regression.mjs')],{cwd:root,env:{...process.env,TEST_BASE_URL:'http://127.0.0.1:5174',LD_LIBRARY_PATH:`${join(temp,'lib')}:${process.env.LD_LIBRARY_PATH||''}`},stdio:'inherit'});
  const code=await new Promise((done,fail)=>{child.on('error',fail);child.on('exit',code=>done(code??1))});
  if(code!==0)throw new Error(`Regression tests failed: ${code}`);
} finally {
  if(server && server.exitCode===null && server.signalCode===null){server.kill('SIGTERM'); await new Promise(done=>server.once('exit',done));}
  rmSync(temp,{recursive:true,force:true});
}
