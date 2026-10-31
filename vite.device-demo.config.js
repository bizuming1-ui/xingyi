import {defineConfig} from 'vite';
import {viteSingleFile} from 'vite-plugin-singlefile';
import {resolve} from 'node:path';
// Public standalone download page, separate from the production deployment.
export default defineConfig({base:'/xingyi-isolated-test/device/',plugins:[viteSingleFile()],build:{outDir:'.device-demo-output',emptyOutDir:true,rollupOptions:{input:resolve('device/index.html')}}});
