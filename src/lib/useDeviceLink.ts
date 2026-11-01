import { useEffect, useRef, useState } from "react";
import { BROKER_URLS, TOPIC_PRESS, TOPIC_STATUS } from "./config";
import { connectMqtt } from "./mqttLite";

export type DeviceState = "online" | "offline" | "unknown";

export interface LinkState {
  /** 是否已连上至少一个 MQTT 服务器 */
  server: boolean;
  /** ESP32 按键设备是否在线 */
  device: DeviceState;
}

/**
 * 订阅 ESP32 按键设备。每收到一次按键数据，就调用 onPress(设备开机编号, 累计次数, 是否为旧消息)。
 */
export function useDeviceLink(
  onPress: (session: string, count: number, retained: boolean) => void
): LinkState {
  const cb = useRef(onPress);
  useEffect(() => {
    cb.current = onPress;
  });

  const [link, setLink] = useState<LinkState>({ server: false, device: "unknown" });

  useEffect(() => {
    const up: Record<string, boolean> = {};
    const dev: Record<string, DeviceState> = {};

    const refresh = () => {
      const urls = Object.keys(up);
      const server = urls.some((u) => up[u]);
      const states = urls.filter((u) => up[u]).map((u) => dev[u] ?? "unknown");
      const device: DeviceState = states.includes("online")
        ? "online"
        : states.includes("offline")
          ? "offline"
          : "unknown";
      setLink((p) => (p.server === server && p.device === device ? p : { server, device }));
    };

    const handles = BROKER_URLS.map((url) => {
      up[url] = false;
      return connectMqtt({
        url,
        topics: [TOPIC_PRESS, TOPIC_STATUS],
        onState: (ok) => {
          up[url] = ok;
          if (!ok) dev[url] = "unknown";
          refresh();
        },
        onMessage: (m) => {
          if (m.topic === TOPIC_PRESS) {
            try {
              const j = JSON.parse(m.payload);
              cb.current(String(j.s ?? ""), Number(j.c), m.retain);
            } catch {
              /* 忽略格式不对的消息 */
            }
          } else if (m.topic === TOPIC_STATUS) {
            dev[url] = m.payload === "online" ? "online" : "offline";
            refresh();
          }
        },
      });
    });

    return () => handles.forEach((h) => h.close());
  }, []);

  return link;
}
