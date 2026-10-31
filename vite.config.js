import { defineConfig } from 'vite';
import { resolve } from 'node:path';
export default defineConfig({
  base: process.env.BASE_PATH || '/',
  server: {host:'0.0.0.0', allowedHosts: true},
  build: {rollupOptions: {input: {main: resolve('index.html'), device: resolve('device/index.html')}}}
});
