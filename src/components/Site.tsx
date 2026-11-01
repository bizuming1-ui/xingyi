import { useEffect, useState } from "react";
import { QRCodeSVG } from "qrcode.react";
import {
  STORAGE_KEY,
  addToNow,
  applyDevicePress,
  digits,
  loadBoard,
  saveBoard,
  type BoardData,
} from "../lib/board";
import { useDeviceLink } from "../lib/useDeviceLink";

interface Props {
  /** 二维码里编码的网址（手机扫码后打开） */
  qrUrl: string;
  /** 点击顶部“进入复制代码界面”时触发（仅浏览器模拟器里可用） */
  onCaptionClick?: () => void;
}

export default function Site({ qrUrl, onCaptionClick }: Props) {
  const [data, setData] = useState<BoardData>(loadBoard);
  const [msg, setMsg] = useState("");

  // 自动保存
  useEffect(() => {
    saveBoard(data);
  }, [data]);

  // 多个标签页之间同步；模拟器里的“模拟传感器”也通过事件传进来
  useEffect(() => {
    const onStorage = (e: StorageEvent) => {
      if (e.key === STORAGE_KEY) setData(loadBoard());
    };
    const onPress = (e: Event) => {
      const delta = Number((e as CustomEvent<{ delta?: number }>).detail?.delta) || 1;
      setData((d) => ({ ...d, now: addToNow(d.now, delta) }));
    };
    window.addEventListener("storage", onStorage);
    window.addEventListener("board-press", onPress);
    return () => {
      window.removeEventListener("storage", onStorage);
      window.removeEventListener("board-press", onPress);
    };
  }, []);

  // ESP32 传感器：A、B 两个光电对射同时遮光一次，“现在”+1
  const link = useDeviceLink((session, count, retained) =>
    setData((d) => applyDevicePress(d, session, count, retained))
  );

  const set = (patch: Partial<BoardData>) => setData((d) => ({ ...d, ...patch }));

  /** 确认：把“现在”的数字累加到“当前生产数”，然后“现在”清零 */
  const confirm = () => {
    const n = Number(data.now);
    if (!data.now || !Number.isFinite(n) || n <= 0) {
      setMsg("请先让“现在”大于 0（传感器计数或手动输入）");
      return;
    }
    const next = data.current + n;
    set({ current: next, now: "" });
    const reached = data.total && next >= Number(data.total);
    setMsg(`已确认：+${n}，当前生产数 = ${next}${reached ? "（已达到生产总数）" : ""}`);
  };

  const caption = "进入复制代码界面";
  const pill =
    !link.server
      ? { cls: "bad", text: "未连接服务器" }
      : link.device === "online"
        ? { cls: "ok", text: "传感器设备在线" }
        : { cls: "wait", text: "已连接 · 等待传感器设备" };

  return (
    <div className="site">
      <div className="site-inner">
        <div className="site-head">
          {onCaptionClick ? (
            <button type="button" className="site-caption" onClick={onCaptionClick}>
              {caption}
            </button>
          ) : (
            <div className="site-caption">{caption}</div>
          )}
          <div className={`link-pill ${pill.cls}`} role="status">
            <i className="dot" />
            {pill.text}
          </div>
        </div>

        <div className="frame">
          <span className="frame-border" />

          <div className="qr-box">
            <span className="qr-label">二维码</span>
            <div className="qr-img">
              <QRCodeSVG
                value={qrUrl || "about:blank"}
                size={256}
                level="M"
                marginSize={0}
                style={{ width: "100%", height: "100%", display: "block" }}
              />
            </div>
          </div>

          <div className="fld f-order">
            <label className="fld-label" htmlFor="f-order">
              生产单
            </label>
            <input
              id="f-order"
              className="box"
              value={data.order}
              onChange={(e) => set({ order: e.target.value })}
              autoComplete="off"
            />
          </div>

          <div className="fld f-product">
            <label className="fld-label" htmlFor="f-product">
              产品
            </label>
            <input
              id="f-product"
              className="box"
              value={data.product}
              onChange={(e) => set({ product: e.target.value })}
              autoComplete="off"
            />
          </div>

          <div className="fld f-total">
            <label className="fld-label" htmlFor="f-total">
              生产总数
            </label>
            <input
              id="f-total"
              className="box"
              inputMode="numeric"
              value={data.total}
              onChange={(e) => set({ total: digits(e.target.value) })}
              autoComplete="off"
            />
          </div>

          <div className="fld f-current">
            <label className="fld-label" htmlFor="f-current">
              当前生产数
            </label>
            {/* 只读：不能手动填写，只能通过“确认”得到 */}
            <input
              id="f-current"
              className="box"
              value={data.current ? String(data.current) : ""}
              readOnly
              tabIndex={-1}
              inputMode="none"
              aria-readonly="true"
              title="当前生产数不能手动填写，请在“现在”累加后点击确认"
            />
          </div>

          <div className="fld f-now">
            <label className="fld-label" htmlFor="f-now">
              现在
            </label>
            <input
              id="f-now"
              className="box"
              inputMode="numeric"
              value={data.now}
              onChange={(e) => set({ now: digits(e.target.value) })}
              onKeyDown={(e) => {
                if (e.key === "Enter") confirm();
              }}
              autoComplete="off"
            />
          </div>

          <div className="foot">
            <p className="site-msg" role="status">
              {msg}
            </p>
            <button type="button" className="btn-ok" onClick={confirm}>
              确认
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
