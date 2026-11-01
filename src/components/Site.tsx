import { useEffect, useState } from "react";
import { QRCodeSVG } from "qrcode.react";

interface Data {
  order: string; // 生产单
  product: string; // 产品
  total: string; // 生产总数
  current: number; // 当前生产数（只读，只能通过“确认”增加）
  now: string; // 现在
}

const KEY = "production-board-v1";
const EMPTY: Data = { order: "", product: "", total: "", current: 0, now: "" };

function load(): Data {
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) return EMPTY;
    const d = JSON.parse(raw);
    return {
      order: String(d.order ?? ""),
      product: String(d.product ?? ""),
      total: String(d.total ?? ""),
      current: Number(d.current) || 0,
      now: String(d.now ?? ""),
    };
  } catch {
    return EMPTY;
  }
}

const digits = (v: string) => v.replace(/\D/g, "").slice(0, 9);

interface Props {
  /** 二维码里编码的网址（手机扫码后打开） */
  qrUrl: string;
  /** 点击顶部“进入复制代码界面”时触发（仅浏览器模拟器里可用） */
  onCaptionClick?: () => void;
}

export default function Site({ qrUrl, onCaptionClick }: Props) {
  const [data, setData] = useState<Data>(load);
  const [msg, setMsg] = useState("");

  useEffect(() => {
    try {
      localStorage.setItem(KEY, JSON.stringify(data));
    } catch {
      /* ignore */
    }
  }, [data]);

  const set = (patch: Partial<Data>) => setData((d) => ({ ...d, ...patch }));

  /** 确认：把“现在”的数字累加到“当前生产数”，然后“现在”清零 */
  const confirm = () => {
    const n = Number(data.now);
    if (!data.now || !Number.isFinite(n) || n <= 0) {
      setMsg("请先在“现在”里输入大于 0 的数量");
      return;
    }
    const next = data.current + n;
    set({ current: next, now: "" });
    const reached = data.total && next >= Number(data.total);
    setMsg(`已确认：+${n}，当前生产数 = ${next}${reached ? "（已达到生产总数）" : ""}`);
  };

  const caption = "进入复制代码界面";

  return (
    <div className="site p-2 sm:p-3">
      <div className="site-inner">
        {onCaptionClick ? (
          <button type="button" className="site-caption" onClick={onCaptionClick}>
            {caption}
          </button>
        ) : (
          <div className="site-caption">{caption}</div>
        )}

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
              title="当前生产数不能手动填写，请在“现在”输入后点击确认"
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

          <button type="button" className="btn-ok" onClick={confirm}>
            确认
          </button>
        </div>
      </div>
      <p className="site-msg" role="status">
        {msg}
      </p>
    </div>
  );
}
