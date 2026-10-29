import { defineConfig } from 'vite';
export default defineConfig({base: process.env.BASE_PATH || '/', server: {host:'0.0.0.0', allowedHosts: true}});
