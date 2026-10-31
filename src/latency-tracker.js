// A deliberately narrow observational test: one idle device, one order, one
// isolated short press, no confirmation or other writers. The browser's serial
// line receipt and animation frame share performance.now(); device and browser
// clocks are never subtracted. Results are estimates, not proof of identity.
export class LatencyTracker {
  constructor() {this.reset();}
  reset(){this.local=null;this.ack=null;this.reply=null;this.draft=null;this.phase='calibrate';this.pending=null;this.result=null;this.reason='请短按一次作校准，等待上传完成。';this.lastUptime=null;this.frozen=null;this.loss=null;}
  fail(reason){this.phase='invalid';this.pending=null;this.reason=reason;}
  line(line,now){
    if(typeof line!=='string' || line.length>200 || !Number.isFinite(now))return;
    let m;
    if((m=/^COUNT t=(\d+) total=(\d+) p=(\d+)$/.exec(line))) {
      const uptime=Number(m[1]),total=Number(m[2]),pending=Number(m[3]);
      if(![uptime,total,pending].every(Number.isSafeInteger))return this.fail('设备计数数据无效，请重新校准。');
      if(this.lastUptime!==null && uptime<this.lastUptime)return this.fail('设备时间回退或重启，请重新校准。');
      this.lastUptime=uptime;this.local={total,pending,at:now,uptime};
      if(this.phase==='armed') {
        if(total!==this.base.total+1 || pending!==1)return this.fail('计数并非单次新增，结果无法可靠关联。');
        this.phase='waiting';this.pending={start:now,total,baseline:this.base.draft};this.reason='已收到串口计数，等待网页更新。';
      } else if(this.phase==='waiting' && total!==this.pending.total) return this.fail('测试期间出现多次计数，结果无法可靠关联。');
      else if(this.phase==='ready' && total!==this.base.total) this.fail('校准后计数发生变化，请重新校准。');
    } else if((m=/^WIFI=(\d) TIME=(\d) BOUND=(\d) PENDING=(\d+) t=(\d+)$/.exec(line))) {
      const uptime=Number(m[5]);if(!Number.isSafeInteger(uptime))return;
      if(this.lastUptime!==null && uptime<this.lastUptime)return this.fail('设备时间回退或重启，请重新校准。');this.lastUptime=uptime;
      this.status={online:m[1]==='1',time:m[2]==='1',bound:m[3]==='1',pending:Number(m[4]),at:now};
      if((this.phase==='armed'||this.phase==='waiting'||this.phase==='ready') && (!this.status.online||!this.status.time||!this.status.bound))this.fail('设备离线或可信时间不可用，结果无效。');
    } else if((m=/^FREEZE b=(\d) f=(\d) c=(\d) i=(\d)$/.exec(line))) {
      this.frozen=m[1]!=='0'||m[2]!=='0';if(this.frozen && ['ready','armed','waiting'].includes(this.phase))this.fail('确认冻结期间不能测速。');
    } else if((m=/^M1 i=(\d+) t=(\d+) k=(\d+) d=(\d+) r=(\d+) b=(\d+)$/.exec(line))) {
      const id=Number(m[1]);if(Number.isSafeInteger(id))this.reply={id,kind:Number(m[3]),result:Number(m[5]),at:now};
    } else if((m=/^M3 i=(\d+) x=(\d+)$/.exec(line))) {
      const id=Number(m[1]),total=Number(m[2]);
      if(this.reply && this.reply.id===id && this.reply.kind===2 && this.reply.result===0 && Number.isSafeInteger(total))this.ack={total,at:now};
    } else if((m=/^MON v=1 t=(\d+) lost=(\d+)$/.exec(line))) {
      const lost=Number(m[2]);if(!Number.isSafeInteger(lost))return;
      if(this.loss!==null && lost!==this.loss && ['ready','armed','waiting'].includes(this.phase))this.fail('监视事件有丢失，请重新校准。');
      this.loss=lost;
    } else if(/^FAULT/.test(line))this.fail('设备故障，测速无效。');
  }
  snapshot(value,now,startedAt=now){
    if(!Number.isSafeInteger(value)||value<0||!Number.isFinite(now))return this.fail('网页计数读取失败，测速无效。');
    if(!Number.isFinite(startedAt)||startedAt>now)return this.fail('网页时间异常，测速无效。');
    this.draft={value,at:now,startedAt};
    if(this.phase==='armed' && value!==this.base.draft)return this.fail('按键前网页计数已变化，请重新校准。');
    if(this.phase==='waiting' && value!==this.pending.baseline && value!==this.pending.baseline+1)return this.fail('网页数变化不止一次，无法可靠关联。');
  }
  canArm(now){return this.phase==='calibrate' && this.local && this.ack && this.status && this.draft && this.loss!==null && this.frozen===false && this.local.total===this.ack.total && this.ack.at>=this.local.at && this.status.at>=this.ack.at && this.status.pending===0 && this.status.online && this.status.time && this.status.bound && this.draft.startedAt>=this.ack.at && now-this.status.at<3000 && now-this.draft.at<3000;}
  arm(now){if(!this.canArm(now))return false;this.base={total:this.local.total,draft:this.draft.value};this.phase='armed';this.armedAt=now;this.reason='已准备：仅短按一次，不要进行设备确认。';return true;}
  painted(value,now){
    if(this.phase!=='waiting'||value!==this.pending.baseline+1)return null;
    const elapsed=now-this.pending.start;
    if(!Number.isFinite(elapsed)||elapsed<0||elapsed>30000){this.fail('结果超时或时间异常，请重新校准。');return null;}
    this.result={milliseconds:Math.round(elapsed),from:this.pending.baseline,to:value};this.phase='done';this.pending=null;this.reason='已测得近似时间。';return this.result;
  }
  expire(now){if(this.phase==='armed'&&now-this.armedAt>30000)this.fail('等待按键超时，请重新校准。');else if(this.phase==='waiting'&&now-this.pending.start>30000)this.fail('超过 30 秒未看到匹配变化，请核对计数。');}
}
