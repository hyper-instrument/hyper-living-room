---
name: home-assist
description: 查询室内温湿度、用红外遥控控制大金空调（开关/温度/模式/风速/扫风），以及控制 Tapo P110M 智能插座的市电。当用户问室内温度/湿度、房间冷热、空调状态，或让开/关空调、调温度、调模式风速（air conditioner / AC / 空调 / 冷房 / 暖房 / 温度 / 湿度 / temperature / humidity）时使用。
version: 2.3.0
platforms: [linux, macos, windows]
metadata:
  hermes:
    tags: [home, climate, temperature, humidity, ac, air-conditioner, daikin, ir, tapo, m5stick, 温度, 湿度, 空调, 冷房, 暖房]
    related_skills: []
---

# Home Assist — 室内环境 + 大金空调（红外）+ 智能插座

一个 M5StickS3 中控暴露的 MCP 接口。它：
- 读取小米 LYWSDCGQ 蓝牙温湿度计；
- 通过**红外**遥控真实的**大金空调**（型号 AJT22UNS-W，遥控器 ARC478A33，协议 **DAIKIN152**）——开关/温度/模式/风速/扫风；
- 读取/控制一个 **Tapo P110M 智能插座**（通断电 + 电量统计）。**插座与空调是独立设备**，插座上插的不是空调。

## 连接方式（二选一，优先 1）

1. **MCP 工具**：若当前环境已连接名为 `m5stick` 的 MCP server，直接调用其工具（下表）。
2. **自带脚本（无需 MCP，可移植）**：运行本 skill 的 `scripts/ac.py`（纯 Python3 标准库）：
   ```sh
   python3 scripts/ac.py status        # 状态 JSON
   python3 scripts/ac.py on            # 开空调（制冷 24°C + 扫风）
   python3 scripts/ac.py off | temp 26 | mode cool | fan high | swing on | plug off
   ```
   端点解析顺序：`$HOME_ASSIST_URL` → `~/.config/home-assist/url` → `http://m5stick.local/mcp`（仅家里局域网）。
   **换一台机器部署**：拷贝本 skill 目录 + 把控制端点 URL 写入 `~/.config/home-assist/url` 即可（URL 含访问密钥，不要提交进任何仓库）。

## 工具

| 工具 | 参数 | 说明 |
|---|---|---|
| `get_status` | 无 | 温湿度、传感器电量、插座市电状态/功率/用电，以及**最后一次发给空调的红外设置**（`ac_ir_power/mode/temp_c/fan/swing/streamer`）|
| `set_ac` | `power?`(bool), `mode?`(auto/cool/heat/dry/fan), `temp?`(16–30), `fan?`(auto/quiet/low/medium/high), `swing?`(bool) | **红外遥控空调**，只改传入的字段。这是控制空调的主要方式 |
| `set_plug_power` | `on`(bool) | Tapo 插座通断电（插座上是独立电器，**不是空调**）。仅在用户明确要求开关这个插座时使用 |

## 重要行为约定（务必遵守）

- **「开空调 / 开机」= 制冷。** 现在是夏天，用户说「开空调」「开机」「有点热」时，调用
  `set_ac` 传 **`power=true, mode=cool, temp=24, swing=true`**（默认 24°C + 上下扫风）。
- **默认温度 24°C、默认开上下扫风**（`swing=true`）。用户没特别说温度时就用 24。
- 空调有**记忆功能**：真实遥控器按「冷房」会恢复上次设置。但我们主动明确设成 制冷/24°C/扫风，不要依赖记忆。
- **只调温度/模式/风速**时，用 `set_ac` 只传对应字段即可（例如「调到 26 度」→ `set_ac {temp:26}`）。
- **空调只走红外**：所有空调操作一律用 `set_ac`。「关空调」= `set_ac {power:false}`。插座 `set_plug_power` 和空调无关，别用它开关空调。
- **物理按钮**：Stick 正面 A 键 = 红外开关空调（关→开时用夏季默认 制冷/24°C/扫风）；插座不再由按钮控制。
- 红外是**单向**的：`get_status` 里的 `ac_ir_*` 是「最后一次发送的指令」，不是空调实测状态。发完指令要说「已发送」，不要说「已开启/已关闭」。**注意：智能插座和空调是两个独立设备（空调不插在插座上），不能用插座功率判断空调状态。**唯一的物理确认是空调收到时的「嘀」声，需要用户在场确认。
- 用户问冷热/湿度用 `get_status` 的 `temperature_c` / `humidity_pct` 直接回答，不要念整段 JSON。

## 注意

- **红外距离是最常见的故障原因**：Stick 的红外 LED 功率比原装遥控器弱得多，需要近距离（1–2 米内）、正对空调室内机接收窗、无遮挡。用户说「没反应」时，第一建议是把 Stick 移近、对准空调，而不是怀疑系统故障。
- `sensor_stale=true` 表示温湿度超过 5 分钟没更新，读数不可信，要如实说明。
- `get_status` 里 `ac_ir_known=false` 表示本次开机后还没发过任何红外指令。
