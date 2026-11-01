/**
 * 两个光电对射传感器的“同时遮光 -> 计 1 次”判定。
 * 这是固件 platformio/src/main.cpp 里 sensorTask 的同一套状态机，浏览器模拟器用它模拟硬件行为。
 *
 *  - 待命(armed) 且 A、B 同时遮光稳定 CONFIRM_MS -> 计 1 次，然后退出待命
 *  - 退出待命后，A、B 都通光稳定 CLEAR_MS -> 重新待命
 *  - 只有一个传感器被挡住：不计数，也不待命
 *  - 开机(初始)时 armed=false：如果开机时就被挡住，不会误计数
 */
export interface SensorState {
  armed: boolean;
  bothSince: number | null;
  clearSince: number | null;
}

export const initialSensorState = (): SensorState => ({
  armed: false,
  bothSince: null,
  clearSince: null,
});

export function stepSensors(
  s: SensorState,
  a: boolean,
  b: boolean,
  now: number,
  confirmMs: number,
  clearMs: number
): { state: SensorState; counted: boolean } {
  if (a && b) {
    if (!s.armed) return { state: { ...s, bothSince: null, clearSince: null }, counted: false };
    if (s.bothSince === null) return { state: { ...s, bothSince: now, clearSince: null }, counted: false };
    if (now - s.bothSince >= confirmMs) {
      return { state: { armed: false, bothSince: null, clearSince: null }, counted: true };
    }
    return { state: s, counted: false };
  }
  if (!a && !b) {
    if (s.armed) return { state: { ...s, bothSince: null, clearSince: null }, counted: false };
    if (s.clearSince === null) return { state: { ...s, bothSince: null, clearSince: now }, counted: false };
    if (now - s.clearSince >= clearMs) {
      return { state: { armed: true, bothSince: null, clearSince: null }, counted: false };
    }
    return { state: { ...s, bothSince: null }, counted: false };
  }
  return { state: { ...s, bothSince: null, clearSince: null }, counted: false };
}
