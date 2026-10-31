import {mergeConfig} from 'vite';
import base from './vite.config.js';
// Only the standalone preview redirects its root; production entry points do not.
export default mergeConfig(base, {plugins:[{name:'device-preview-home',configureServer(server){server.middlewares.use((req,res,next)=>{if(req.url==='/'){res.writeHead(302,{Location:'/device/'});res.end();}else next();});}}]});
