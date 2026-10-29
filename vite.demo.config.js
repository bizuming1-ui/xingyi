import {defineConfig} from 'vite';
import {viteSingleFile} from 'vite-plugin-singlefile';
import {resolve} from 'node:path';
// Explicit build for the isolated demo only, never used by the production site.
export default defineConfig({base:'/xingyi-isolated-test/',plugins:[viteSingleFile()],build:{outDir:'.demo-output',emptyOutDir:true,rollupOptions:{input:resolve('index.html')}}});
