import {requestId} from './request-id.js';

// Experimental controller only: it is not wired into the production UI.
// Persist request identity before network calls so a lost response is retried
// with the same server-side idempotency key. No credentials are stored here.
export const DEVICE_CONFIRMATION_KEY='xingyi_device_confirmation_v1';
const uuid=x=>typeof x==='string' && /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(x);
const nonnegative=x=>Number.isSafeInteger(x) && x>=0;
function validResult(value) {
  return value && nonnegative(value.requested) && value.requested>0 && nonnegative(value.applied) &&
    value.applied<=value.requested && nonnegative(value.current) && value.current>=value.applied;
}
function recordValid(r) {
  return r && r.version===1 && uuid(r.requestId) && uuid(r.deviceId) && uuid(r.orderId) &&
    (r.barrierId===null || uuid(r.barrierId)) && typeof r.commitAttempted==='boolean' &&
    (r.terminal===null || (r.terminal?.state==='completed' && validResult(r.terminal.result)) || r.terminal?.state==='cancelled');
}
function verifiedOpen(value,deviceId) {
  if(value===null)return null;
  if(!value || !uuid(value.id) || !uuid(value.request_id) || value.device_id!==deviceId || !uuid(value.order_id) ||
     !['requested','ready','expired'].includes(value.state)) throw new Error('设备未结束请求状态无效，请勿继续。');
  return value;
}
function verifiedView(value,record) {
  if(!value || value.id!==record.barrierId || value.order_id!==record.orderId ||
     !['requested','ready','expired','completed','cancelled'].includes(value.state)) throw new Error('设备确认状态无效，请勿继续。');
  if(value.state==='ready' && (!Number.isSafeInteger(value.frozen_count) || value.frozen_count<=0)) throw new Error('冻结计数无效，请勿继续。');
  if(value.state==='completed' && !validResult(value.result)) throw new Error('确认结果无效，请保留记录并联系管理员。');
  return value;
}

// A shared Web Lock is mandatory to serialize tabs using this browser profile.
// Server row locks and the unique open-barrier index remain essential across
// different phones/browsers. The caller must handle unavailable storage/locks.
export class DeviceConfirmation {
  constructor({db, storage, locks=globalThis.navigator?.locks, newId=requestId}) {
    this.db=db;this.storage=storage;this.locks=locks;this.newId=newId;
  }
  async exclusive(action) {
    if(!this.locks || typeof this.locks.request!=='function') throw new Error('此浏览器不支持安全的多标签页协调，不能发起设备确认。');
    return this.locks.request(DEVICE_CONFIRMATION_KEY,{mode:'exclusive'},action);
  }
  load() {
    const raw=this.storage.getItem(DEVICE_CONFIRMATION_KEY);
    if(raw===null)return null;
    let record;try{record=JSON.parse(raw);}catch{throw new Error('设备确认恢复记录损坏，请勿新建请求。');}
    if(!recordValid(record))throw new Error('设备确认恢复记录无效，请勿新建请求。');
    return record;
  }
  save(record) {
    if(!recordValid(record))throw new Error('设备确认记录无效。');
    const raw=JSON.stringify(record);
    this.storage.setItem(DEVICE_CONFIRMATION_KEY,raw);
    if(this.storage.getItem(DEVICE_CONFIRMATION_KEY)!==raw)throw new Error('无法保存恢复记录，不能继续。');
  }
  async call(name,args) {
    const {data,error}=await this.db.rpc(name,args);
    if(error)throw new Error('设备确认请求未完成，请检查状态后使用原请求重试。');
    return data;
  }
  async current() { return this.exclusive(()=>this.load()); }
  async start({deviceId,orderId,operatorPaused}) {
    return this.exclusive(async()=>{
      if(operatorPaused!==true)throw new Error('请先提醒操作员暂停按键。');
      if(!uuid(deviceId)||!uuid(orderId))throw new Error('设备或生产单无效。');
      let record=this.load();
      if(record) {
        if(record.deviceId!==deviceId || record.orderId!==orderId || record.terminal)throw new Error('存在未处理的设备确认，请先恢复或处理。');
      } else {
        // Never create a new local identity while another browser has an open
        // barrier. The server unique index still resolves cross-device races.
        const open=await this.readOpen(deviceId);
        if(open)throw new Error('设备存在未结束的确认，请先恢复该请求。');
        const id=this.newId();if(!uuid(id))throw new Error('无法生成安全请求编号。');
        record={version:1,requestId:id,deviceId,orderId,barrierId:null,commitAttempted:false,terminal:null};
        this.save(record); // must succeed before a request can reach the server
      }
      if(!record.barrierId) {
        const barrier=await this.call('device_confirmation_request',{p_device:record.deviceId,p_order:record.orderId,p_request:record.requestId});
        if(!uuid(barrier))throw new Error('确认请求回复无效，请保留记录并重试。');
        record.barrierId=barrier;this.save(record);
      }
      return record;
    });
  }
  async readOpen(deviceId) {
    if(!uuid(deviceId))throw new Error('设备无效。');
    return verifiedOpen(await this.call('device_confirmation_open',{p_device:deviceId}),deviceId);
  }
  async inspectOpen(deviceId) {
    return this.exclusive(()=>this.readOpen(deviceId));
  }
  async recoverOpen({deviceId,orderId,operatorPaused}) {
    return this.exclusive(async()=>{
      if(operatorPaused!==true)throw new Error('请先提醒操作员暂停按键。');
      if(!uuid(deviceId)||!uuid(orderId))throw new Error('设备或生产单无效。');
      if(this.load())throw new Error('本浏览器存在未处理的设备确认，不能覆盖。');
      const open=await this.readOpen(deviceId);
      if(!open)throw new Error('没有可恢复的未结束设备确认。');
      if(open.order_id!==orderId)throw new Error('设备正在处理其他生产单，请先处理原请求。');
      // Another browser could have a commit in flight; keep that possibility
      // visible and let server-side device/barrier locks arbitrate all actions.
      const record={version:1,requestId:open.request_id,deviceId,orderId,barrierId:open.id,commitAttempted:true,terminal:null};
      this.save(record);return record;
    });
  }
  async readView(record) {
    if(!record?.barrierId)throw new Error('确认请求尚未恢复，请先使用原请求重试。');
    return verifiedView(await this.call('device_confirmation_view',{p_barrier:record.barrierId}),record);
  }
  async observe() {
    return this.exclusive(async()=>{
      const record=this.load();if(!record)throw new Error('没有待处理的设备确认。');
      const view=await this.readView(record);
      if(view.state==='completed' || view.state==='cancelled') {
        record.terminal=view.state==='completed'?{state:'completed',result:view.result}:{state:'cancelled'};this.save(record);
      }
      return view;
    });
  }
  async confirm({operatorPaused}) {
    return this.exclusive(async()=>{
      if(operatorPaused!==true)throw new Error('请先提醒操作员暂停按键。');
      const record=this.load();if(!record || !record.barrierId)throw new Error('确认请求尚未恢复。');
      const view=await this.readView(record);
      if(view.state==='completed') {record.terminal={state:'completed',result:view.result};this.save(record);return view.result;}
      if(view.state!=='ready')throw new Error('设备尚未完成冻结同步，不能确认。');
      record.commitAttempted=true;this.save(record); // an unknown outcome must never be silently discarded
      const result=await this.call('device_confirmation_commit',{p_barrier:record.barrierId});
      if(!validResult(result) || result.requested!==view.frozen_count)throw new Error('确认结果无效，请保留记录并查询状态。');
      record.terminal={state:'completed',result};this.save(record);
      return result;
    });
  }
  async cancel() {
    return this.exclusive(async()=>{
      const record=this.load();if(!record || !record.barrierId)throw new Error('确认请求尚未恢复。');
      if(record.terminal?.state==='completed')throw new Error('已确认的计数不能取消。');
      // The server serializes cancellation against a possibly in-flight commit.
      // A commit that won the race is reported as an error; retain the record.
      const ok=await this.call('device_confirmation_cancel',{p_barrier:record.barrierId});
      if(ok!==true)throw new Error('取消未确认，请保留记录并查询状态。');
      record.terminal={state:'cancelled'};this.save(record);return true;
    });
  }
  async acknowledge() {
    return this.exclusive(()=>{
      const record=this.load();
      if(!record?.terminal)throw new Error('确认请求尚未结束，不能清除恢复记录。');
      this.storage.removeItem(DEVICE_CONFIRMATION_KEY);
      if(this.storage.getItem(DEVICE_CONFIRMATION_KEY)!==null)throw new Error('无法清除已结束的恢复记录。');
    });
  }
}
