<p align="center">
  <img src="assets/logo-circuit-ink.png" alt="AInk — 电路墨水" width="360">
</p>

<p align="center">
  <strong>基于 ESP32-S3 的口袋级墨水屏计算机</strong><br>
  200×200 单色显示 · 双键交互 · 七款迷你应用 · 中英双语固件
</p>

<p align="center">
  <a href="README.md">English</a> · <a href="README.zh-CN.md">简体中文</a>
</p>

---

## 1. 产品概述

**AInk**（电路墨水）是一套面向 **Seeed XIAO ESP32-S3 Sense** + **Waveshare 1.54 寸** 黑白墨水屏的完整固件。它既是可以日常佩戴/携带的信息终端，也是一套可在资源受限墨水屏上运行 LVGL 的**可二次开发参考实现**。

| 项目 | 规格 |
|------|------|
| 屏幕 | 200×200 px，1-bit，支持局刷 |
| 输入 | 2 个按键（A / B），可选串口模拟 |
| 联网 | WiFi +  captive portal 配网 |
| 界面 | LVGL 8.3，自研 1-bit 刷新管线 |
| 语言 | 英文 / 中文（NVS 持久化） |
| 应用 | 时钟、天气、AI 识图、答案之书、股票、生命游戏、设置 |

**设计原则**

- **克制刷新** — 分级刷新策略（`FAST` / `NAV` / `QUALITY` / `FULL`）在响应速度与残影之间取舍。
- **首页与应用分离** — 启动器保留 20 px 状态栏；进入任意应用后**全屏 200×200**，最大化内容区。
- **能离线则离线** — 答案之书、生命游戏无需 API；天气/股票/识图在无密钥时优雅降级。

---

## 2. 系统架构

```
┌─────────────────────────────────────────────────────────┐
│  Aink.ino — 启动、配网门户、刷新调度、NTP 对时          │
├─────────────────────────────────────────────────────────┤
│  ui_nav / ui_home — 分页 2×2 启动器、路由               │
│  ui_status_bar — 日期、WiFi、天气摘要、电量             │
│  ui_* — 各应用界面                                      │
├─────────────────────────────────────────────────────────┤
│  ui_lvgl + epaper_canvas — LVGL 8.3 → 1-bit 帧缓冲      │
├─────────────────────────────────────────────────────────┤
│  服务层：weather、stock、vision、voice、camera、NVS     │
└─────────────────────────────────────────────────────────┘
         │ HTTPS / gzip (puff)              │ EPD SPI
         ▼                                  ▼
   和风、OpenAI、MiMo 等                  Waveshare 1.54″ V2
```

---

## 3. 应用矩阵

| 应用 | 功能说明 |
|------|----------|
| **时钟** | 40 px 大字号时间，可选日期 |
| **天气** | 实况、3 日预报，湿度 / UV / 风速 / AQI / 日出日落 / 气压 / **PM2.5 & PM10** |
| **AI 识图** | 相机 JPEG → 云端视觉 API → 不超过 40 字的诗意描述 |
| **答案之书** | 完全离线，100 条内置答案；拍照 / 语音 / 随缘三种起卦 |
| **股票** | 最多 **6** 只自选；**B** 进入详情（价格、涨跌、分时图） |
| **生命游戏** | Conway 生命游戏；脉冲星、飞船、震荡、七格、枪阵等经典图案 |
| **设置** | WiFi、和风 API、AI 提供商与密钥、显示、关于 |

### 天气数据链路

1. IP 定位 → 和风 Geo 查询（LocationID）
2. 请求 `weather/now`、`weather/7d`、`airquality/v1/current/{lat}/{lon}`
3. 鉴权：`X-QW-Api-Key` 请求头；响应 gzip 由固件内 `puff.c` 解压
4. 城市显示地级市（`adm2`）；预报不含「今天」

### 云端能力

| 功能 | 提供商 / 接口 |
|------|----------------|
| 天气 | [和风天气](https://console.qweather.com) |
| 识图 | OpenAI、Gemini、Kimi、MiMo Token Plan |
| 语音 | MiMo ASR + 对话（需 Token Plan 密钥） |

答案之书、生命游戏**无需 API Key**。

---

## 4. 交互规范

| 输入 | 全局行为 |
|------|----------|
| **A** 短按 | 下一项 / 应用内操作 |
| **A** 双击 | 上一项 |
| **A** 长按 | 返回首页 |
| **B** 短按 | 打开 / 确认 |
| **B** 双击 | 语音录制开关 |

在 `btn_input.h` 中设 `BTN_SERIAL_SIM=1`，可用串口 **115200** 发送 `n` / `p` / `b` / `c` / `v` 模拟按键。

---

## 5. 硬件与固件要求

| 部件 | 说明 |
|------|------|
| 主控 | Seeed **XIAO ESP32-S3 Sense**（含相机与 PDM 麦克风） |
| 屏幕 | [EPD_1in54_V2](https://www.waveshare.com/1.54inch-e-paper.htm) |
| 按键 A | D6 / GPIO43 |
| 按键 B | D7 / GPIO44 |
| 墨水屏接线 | 见 `DEV_Config.h` |

| Arduino 选项 | 推荐值 |
|--------------|--------|
| 开发板 | Seeed XIAO ESP32S3 Sense |
| PSRAM | OPI PSRAM **已启用** |
| 分区 | Huge APP (3 MB+) |
| LVGL | **8.3.x**（勿用 9.x） |
| ESP32 核心 | **3.x** |

---

## 6. 开发者指南

### 6.1 快速上手

```text
1. 安装 Arduino IDE 2.x（或 arduino-cli）及 esp32 开发板包 3.x
2. 安装 lvgl 8.3.x
3. 打开 Aink.ino（本目录即为 Sketch 根目录）
4. 烧录 → 串口监视器 115200
```

首次无 WiFi 配置时自动进入**配网门户**（二维码 / AP），可设置 WiFi、天气 Key、可选 AI Key。

### 6.2 自定义开机动画

```powershell
python tools/bmp_to_boot_splash.py path\to\splash.bmp --out boot_splash_image.h
```

需 200×200 单色 BMP。

### 6.3 在 PC 上测试识图 API

```powershell
cd Aink
$env:MIMO_API_KEY = "your-key"
python tools/test_vision_api.py --provider mimo --image photo.jpg
```

### 6.4 字体与图标

```bash
python tools/build_cn_symbols.py
python tools/build_fonts.py          # 需 Node npx
python tools/build_clock_font.py
python tools/svg_to_weather_icons.py
python tools/png_to_tile_icons.py    # gear.png / eye.png 放 Sketch 根目录（本地）
```

中文：约 3500 常用字，见 `tools/cn_font_symbols.txt` → 生成 `aink_3500_12.c` / `aink_3500_14.c`。  
手动方案：[LVGL 字体转换器](https://lvgl.io/tools/fontconverter)（LVGL 8.x，bpp 1）。若 `.c` 中出现 `.static_bitmap = 0` 请删除（LVGL 9 产物）。

### 6.5 代码结构

| 模块 | 职责 |
|------|------|
| `Aink.ino` | 启动、门户、刷新调度 |
| `epaper_canvas.*` | 帧缓冲、旋转、异步上屏 |
| `ui_nav / ui_home` | 启动器与导航 |
| `ui_status_bar.*` | 状态栏（仅首页） |
| `ui_lvgl.*` | LVGL 初始化；普通屏 / 全屏配置 |
| `ui_*` | 各应用 UI |
| `*_service.*` | 天气、股票、识图、语音后端 |
| `settings_api.*` | NVS 配置 |
| `app_locale.*` | 中英文字符串 |
| `puff.c` | Gzip 解压 |
| `tools/` | 构建与测试脚本 |

---

## 7. 运维与排错

| 现象 | 排查方向 |
|------|----------|
| 中文显示方框 | LVGL 8.3.x；字是否在 `cn_font_symbols.txt`；字体 `.c` 勿含 `.static_bitmap` |
| 相机 / PSRAM 失败 | 开启 OPI PSRAM；Huge APP 分区；Sense 款带相机 |
| 天气 401/403 | API Host 与 Key 不匹配；套餐是否含空气质量 API |
| PM2.5 / PM10 为 `--` | 该坐标无污染物数据 |
| `undefined reference to puff` | 清理重编；确认 `puff.c` 在 Sketch 中 |
| 识图 HTTP -1 | WiFi 与 PSRAM；用 `test_vision_api.py` 验证密钥 |

串口日志前缀：`[Weather]`、`[Vision]`、`[Stock]`、`[Life]`、`[LVGL]`。

---

## 8. 许可证

MIT — 见 `LICENSE`。Waveshare EPD 驱动文件保留原版权声明。

---

<p align="center">
  <sub>AInk · 电路墨水 · 固件 v0.1</sub>
</p>
