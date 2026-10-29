import React, {useRef, useState} from 'react';
import {createRoot} from 'react-dom/client';
import ini from '../firmware/hardware-test/platformio.ini?raw';
import cpp from '../firmware/hardware-test/src/main.cpp?raw';
import './device-download.css';

function FileCard({name,content,description}) {
 const ref=useRef(null),[message,setMessage]=useState('');
 async function copy(){
  try {
   if(navigator.clipboard && window.isSecureContext)await navigator.clipboard.writeText(content);
   else {ref.current.focus();ref.current.select();if(!document.execCommand('copy'))throw new Error('copy failed');ref.current.setSelectionRange(0,0);}
   setMessage('已复制');
  } catch {ref.current.focus();ref.current.select();setMessage('请手动复制已选中的代码');}
 }
 function download(){const url=URL.createObjectURL(new Blob([content],{type:'text/plain;charset=utf-8'}));const a=document.createElement('a');a.href=url;a.download=name.split('/').pop();a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}
 return <section className="file"><div className="file-head"><div><p className="caption">{description}</p><h2>{name}</h2></div><div className="actions"><button className="secondary" onClick={download}>下载文件</button><button onClick={copy}>复制代码</button></div></div><textarea ref={ref} readOnly value={content} aria-label={`${name} 代码`} spellCheck="false"/><span role="status" className="copied">{message}</span></section>;
}
function App(){return <main><header><div className="mark">X</div><div><strong>界面2</strong><span>ESP32-S3 设备代码</span></div><span className="tag">硬件验证版</span></header>
 <div className="hero"><p className="eyebrow">PLATFORMIO · ESP32-S3-N16R8</p><h1>把代码复制到开发板</h1><p>两份文件，验证按键与 Wi-Fi。凭据只在您的电脑中填写，无需发送到网页或聊天。</p></div>
 <div className="warning"><strong>仅供硬件验证，不能用于生产计数</strong><p>此版本不连接生产单或数据库、不保存离线计数，重启后数字归零。烧录会替换板上当前程序，请勿用于正在生产的设备。</p></div>
 <section className="steps"><div><span>01</span><p>在 PlatformIO 新建 <b>ESP32-S3 DevKitC-1</b> Arduino 项目。</p></div><div><span>02</span><p>分别复制下方两个文件，替换项目中同名文件。</p></div><div><span>03</span><p>仅在电脑里的 <b>src/main.cpp</b> 填写 2.4 GHz Wi-Fi 名称及密码；也可暂不填写先测按键。</p></div><div><span>04</span><p>按键一端接 <b>GPIO 4</b>，另一端接 <b>GND</b>，不要接 3.3V。上传后打开串口监视器，波特率 <b>115200</b>。</p></div></section>
 <FileCard name="platformio.ini" content={ini} description="项目根目录"/><FileCard name="src/main.cpp" content={cpp} description="项目 src 文件夹"/>
 <section className="result"><h2>板子上如何验证</h2><p>短按一次显示 <b>Added: 1</b>；稳定按住约 0.5 秒后，每 0.1 秒继续加数。Wi-Fi 连接成功时显示 <b>Wi-Fi: connected</b> 和 IP；断网时按键计数仍继续。</p><p>打开串口后如未看到启动信息，请按开发板的 RESET 键。若无法上传或打开串口，请检查 USB 数据线、端口选择及开发板的 USB 接口。</p></section>
 <footer>测试代码仅在您的板子和电脑上运行，不会更改原有生产系统。</footer></main>}
createRoot(document.getElementById('root')).render(<App/>);
