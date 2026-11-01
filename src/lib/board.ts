/** 生产看板的数据模型、本地存储，以及“按键设备累计值 -> 现在 +N”的换算逻辑 */

export interface BoardData {
  order: string; // 生产单
  product: string; // 产品
  total: string; // 生产总数
  current: number; // 当前生产数（只读，只能通过“确认”增加）
  now: string; // 现在（按键 +1 / 长按连加 / 手动输入）
  devSession: string; // 最近一次见到的按键设备“开机编号”
  devCount: number; // 该设备已经统计到的累计按键次数
}

export const STORAGE_KEY = "production-board-v1";

const EMPTY: BoardData = {
  order: "",
  product: "",
  total: "",
  current: 0,
  now: "",
  devSession: "",
  devCount: 0,
};

export const digits = (v: string) => v.replace(/\D/g, "").slice(0, 9);

export function loadBoard(): BoardData {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return EMPTY;
    const d = JSON.parse(raw);
    return {
      order: String(d.order ?? ""),
      product: String(d.product ?? ""),
      total: String(d.total ?? ""),
      current: Number(d.current) || 0,
      now: String(d.now ?? ""),
      devSession: String(d.devSession ?? ""),
      devCount: Number(d.devCount) || 0,
    };
  } catch {
    return EMPTY;
  }
}

export function saveBoard(d: BoardData) {
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(d));
  } catch {
    /* ignore */
  }
}

/** “现在”加上 delta（最多 9 位数） */
export function addToNow(now: string, delta: number): string {
  const n = (Number(now) || 0) + delta;
  return String(Math.min(n, 999999999));
}

/**
 * 处理按键设备发来的“累计按键次数”。
 * 为什么用累计值而不是“+1 事件”？——网络丢一个包也没关系，下一个包自带正确的总数，
 * 重复收到同一个包（两个服务器、两个标签页）也不会多算，所以不会漏数、不会重复数。
 *
 *  - 设备开机编号(session)没见过 + 是服务器保存的旧消息(retained)：只记基准，不加（避免打开网页时把历史次数加进来）
 *  - 设备开机编号没见过 + 是实时消息：设备刚开机从 0 开始，整个累计值都是新增的
 *  - 设备开机编号见过：只加上新增的差值（也能补回网页断线期间漏掉的按键）
 */
export function applyDevicePress(
  d: BoardData,
  session: string,
  count: number,
  retained: boolean
): BoardData {
  if (!session || !Number.isFinite(count) || count < 0) return d;
  if (d.devSession !== session) {
    if (retained || count > 100000) return { ...d, devSession: session, devCount: count };
    return { ...d, devSession: session, devCount: count, now: addToNow(d.now, count) };
  }
  const delta = count - d.devCount;
  if (delta <= 0) return d;
  if (delta > 100000) return { ...d, devCount: count };
  return { ...d, devCount: count, now: addToNow(d.now, delta) };
}
