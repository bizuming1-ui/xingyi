// Experimental data adapter, not yet wired into the production page.
// Values here are server-side drafts / requested bindings, not proof that the
// device is online, synchronized, or safe to confirm.
const uuid=value=>typeof value==='string' && /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(value);
const count=value=>Number.isSafeInteger(value) && value>=0;
function validateDevice(device) {
  if(!device || !uuid(device.id) || typeof device.label!=='string' || device.label.trim().length<1 || device.label.length>100) throw new Error('设备列表数据无效。');
  const {desired_binding_id:binding,desired_order_id:order,desired_binding_revision:revision}=device;
  const absent=binding===null && order===null && revision===null;
  if(!absent && !(uuid(binding) && uuid(order) && count(revision) && revision>0)) throw new Error('设备绑定状态无效。');
  return {id:device.id,label:device.label,desiredBindingId:binding,desiredOrderId:order,desiredRevision:revision};
}
export class DeviceData {
  constructor(db) {this.db=db;}
  async load(orderId) {
    if(!uuid(orderId))throw new Error('生产单无效。');
    // These independent reads are not a transactional snapshot. Neither one is
    // suitable for deciding whether a device is online or fully synchronized.
    const [listResult,draftResult]=await Promise.allSettled([
      this.db.rpc('device_list'),
      this.db.from('device_order_drafts').select('order_id,count').eq('order_id',orderId).maybeSingle()
    ]);
    if(listResult.status!=='fulfilled' || draftResult.status!=='fulfilled')throw new Error('设备数据刷新失败，请检查网络后重试。');
    const list=listResult.value,draft=draftResult.value;
    if(!list || !draft || list.error || draft.error)throw new Error('设备数据刷新失败，请检查网络后重试。');
    if(!Array.isArray(list.data) || list.data.length>1)throw new Error('设备列表数据无效。');
    const device=list.data.length?validateDevice(list.data[0]):null;
    if(draft.data!==null && (!draft.data || draft.data.order_id!==orderId || !count(draft.data.count))) throw new Error('设备计数数据无效。');
    return {orderId,device,draftCount:draft.data===null?0:draft.data.count};
  }
  async provision(adminPassword,label) {
    if(typeof adminPassword!=='string' || !adminPassword || typeof label!=='string' || !label.trim() || label.trim().length>100) throw new Error('请输入管理员密码和设备名称。');
    // A dropped response is uncertain: never retry automatically. Server stores
    // only a hash and cannot show the generated secret again.
    let result;
    try {result=await this.db.rpc('device_create',{p_admin_password:adminPassword,p_label:label.trim()});}
    catch {throw new Error('创建设备结果未知；不要重复创建，请先核对设备列表。');}
    if(!result || result.error)throw new Error('创建设备结果未知；不要重复创建，请先核对设备列表。');
    if(result.data===null)throw new Error('管理员密码错误或暂时锁定，未创建设备。');
    const {id,secret}=result.data;
    if(!uuid(id) || typeof secret!=='string' || !/^[0-9a-f]{64}$/.test(secret))throw new Error('创建设备回复无效；不要重复创建，请先核对设备列表。');
    return {id,secret};
  }
  async requestBinding(deviceId,orderId,confirmed) {
    if(confirmed!==true)throw new Error('请先确认设备要绑定的生产单。');
    if(!uuid(deviceId) || !uuid(orderId))throw new Error('设备或生产单无效。');
    const {data,error}=await this.db.rpc('device_request_binding',{p_device:deviceId,p_order:orderId});
    if(error)throw new Error('绑定请求未确认，请刷新状态后重试。');
    if(!uuid(data))throw new Error('绑定回复无效，请刷新状态后重试。');
    // The result is a desired binding only; offline counts must drain first.
    return {requestedBindingId:data};
  }
}

// Read only the server-side draft for an order. Avoid an unrelated device-list
// request on the order page; it cannot establish online or synchronized state.
export class DeviceDraftData {
  constructor(db) {this.db=db;}
  async load(orderId) {
    if(!uuid(orderId))throw new Error('生产单无效。');
    const result=await this.db.from('device_order_drafts').select('order_id,count').eq('order_id',orderId).maybeSingle();
    if(!result || result.error || (result.data!==null && (!result.data || result.data.order_id!==orderId || !count(result.data.count))))throw new Error('设备计数刷新失败。');
    return {orderId,draftCount:result.data===null?0:result.data.count};
  }
}

// One polling request at a time per active run. Starting a new order or stopping
// invalidates late completions from previous runs, including errors. A slow
// network can exceed the interval: this is not a one-second delivery guarantee.
export class DeviceMonitor {
  constructor(data,{interval=500,setTimer=(fn,ms)=>globalThis.setTimeout(fn,ms),clearTimer=id=>globalThis.clearTimeout(id)}={}) {
    if(!Number.isFinite(interval) || interval<200)throw new Error('刷新间隔无效。');
    this.data=data;this.interval=interval;this.setTimer=setTimer;this.clearTimer=clearTimer;this.generation=0;this.timer=null;
  }
  start(orderId,onUpdate) {
    if(!uuid(orderId) || typeof onUpdate!=='function')throw new Error('设备监控参数无效。');
    this.stop();const generation=this.generation;
    const poll=async()=>{
      if(this.generation!==generation)return;
      this.timer=null;
      let event;
      try {event={type:'snapshot',snapshot:await this.data.load(orderId)};}
      catch {event={type:'error',message:'设备数据刷新失败，请检查网络。'};}
      if(this.generation!==generation)return;
      onUpdate(event);
      // Network failures back off to avoid a 4 Hz retry storm on weak links.
      if(this.generation===generation)this.timer=this.setTimer(poll,event.type==='error'?Math.max(1000,this.interval):this.interval);
    };
    void poll();
  }
  stop() {
    ++this.generation;
    if(this.timer!==null){this.clearTimer(this.timer);this.timer=null;}
  }
}
