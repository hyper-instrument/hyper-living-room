---
name: home-assist
description: 查询室内温湿度、控制空调（Tapo P110M 智能插座）。当用户问室内温度/湿度、房间冷热、空调开着没有，或让开/关空调（air conditioner / AC / 空调 / 温度 / 湿度 / temperature / humidity）时使用。
version: 1.0.0
platforms: [linux, macos, windows]
metadata:
  hermes:
    tags: [home, climate, temperature, humidity, ac, air-conditioner, tapo, m5stick, 温度, 湿度, 空调]
    related_skills: []
---

# Home Assist — 室内环境与空调控制

一个 M5StickS3 中控（hub）暴露的 MCP 接口。它读取小米 LYWSDCGQ 蓝牙温湿度计，并通过 TP-Link Tapo P110M 智能插座控制接在上面的**空调**。

## 依赖

需要名为 **`m5stick`** 的 MCP server 已连接（Streamable-HTTP）：

```
http://m5stick.local/mcp
```

- Claude Code：`claude mcp list` 应显示 `m5stick ... ✔ Connected`
- Hermes：`hermes mcp test m5stick` 应显示 `✓ Connected`

如果没连上，先确认 Stick 通电、和你在同一个 2.4GHz Wi-Fi 下，`.local` 无法解析时可临时换成它的 IP。

## 工具

| 工具 | 参数 | 说明 |
|---|---|---|
| `get_status` | 无 | 返回 JSON：`temperature_c`、`humidity_pct`、`sensor_battery_pct`、`sensor_stale`、`ac_plug_connected`、`ac_on`、`power_w`、`today_kwh`、`today_runtime_min`、`wifi_rssi_dbm` |
| `set_ac_power` | `{ "on": true \| false }` | true=开空调，false=关空调 |

## 常见用法

- **查环境**：用户问「室内多少度」「现在湿度多少」「空调开着吗」→ 调 `get_status`，用其中相关字段直接回答，不要念整段 JSON。
- **开/关空调**：用户说「开空调」「太热了」→ 调 `set_ac_power {on:true}`；「关空调」「有点冷」→ `set_ac_power {on:false}`。
- **确认结果**：`set_ac_power` 大约 1 秒后才生效。改完状态后，若用户需要确认，隔一两秒再调一次 `get_status` 看 `ac_on` 是否已变。

## 注意

- **数据新鲜度**：`sensor_stale` 为 `true` 表示温湿度已超过 5 分钟没更新（传感器可能离线或超出蓝牙范围），此时读数不可信，要如实告知。
- **插座状态**：`ac_plug_connected` 为 `false` 表示 Stick 还没连上插座，此时无法控制空调，别假装成功。
- **硬断电**：`set_ac_power` 是给空调整体断电/通电。空调能否在通电后自动恢复制冷，取决于它有没有「来电自动开机」功能——不确定就提醒用户。
- 只读操作（`get_status`）可随时调用；`set_ac_power` 是真实物理动作，按用户意图执行即可，不用反复确认。
