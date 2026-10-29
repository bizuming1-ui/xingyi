import React, {useRef,useState} from 'react';
// This entry is visible only when the build explicitly selects the isolated
// test project. The password is neither persisted nor sent to any other API.
export function TestSetup({db,onDone}) {
 const [password,setPassword]=useState(''),[again,setAgain]=useState(''),[message,setMessage]=useState(''),[busy,setBusy]=useState(false);
 const sending=useRef(false);
 async function submit(e){
  e.preventDefault();if(sending.current)return;
  if(password.length<12 || password.length>128 || password!==again){setMessage('两次密码需一致，且长度为 12 至 128 个字符。');return;}
  const value=password;setPassword('');setAgain('');sending.current=true;setBusy(true);setMessage('');
  try {const {data,error}=await db.rpc('test_initial_setup',{p_password:value});
   if(error || data!==true){setMessage('设置未确认；请先重新检查账户状态，勿反复操作。');return;}
   onDone();
  } catch {setMessage('设置未确认；请先重新检查账户状态，勿反复操作。');}
  finally {sending.current=false;setBusy(false);}
 }
 return <main className="center"><section className="panel login"><h1>隔离测试 · 首次设置</h1>
  <p>为测试项目设置独立管理员密码（至少 12 位），不要使用正式系统密码，也不要发送到聊天。</p>
  <form onSubmit={submit}>
   <label>测试管理员密码<input type="password" autoComplete="new-password" value={password} onChange={e=>setPassword(e.target.value)} required disabled={busy}/></label>
   <label>再次输入<input type="password" autoComplete="new-password" value={again} onChange={e=>setAgain(e.target.value)} required disabled={busy}/></label>
   <button disabled={busy}>{busy?'设置中…':'完成设置'}</button>
  </form>
  <p role="alert">{message}</p><button type="button" onClick={onDone} disabled={busy}>重新检查账户状态</button>{' '}<button type="button" onClick={()=>db.auth.signOut()} disabled={busy}>退出登录</button>
 </section></main>;
}
