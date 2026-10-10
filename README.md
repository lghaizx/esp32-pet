# ESP32 Pixel Pet（像素宠物）

一块 **ESP32 + 1.8" ST7735 128×160 SPI TFT** 做的电子宠物：会眨眼、会看你看的方向、会说话、会饿会脏会长大，还带一个「接食物」小游戏。UI 用 **LVGL v8** 重写（也可以用原始的 TFT_eSPI 即时模式界面），并且内置一个 **WiFi 配网页面**，用手机就能调参数。

```
固件名 : ESP32 Pixel Pet
版本   : v1.0.0
主控   : ESP32-WROOM-32 (esp32dev)
显示   : ST7735 128x160 SPI（横屏，无触摸）
声音   : 无源蜂鸣器（LEDC / tone，可放 3 首小曲）
输入   : 6 个按键
传感   : 光敏(LDR) + 热敏电阻(NTC)
```
创客 esp32  https://v.douyin.com/iMCyxXh0x3M/ 复制此链接，打开Dou音搜索，直接观看视频！
---

## 功能一览

- **会动的脸**：12 种表情（中性/开心/难过/生气/惊讶/困/爱心/晕/生病/死亡/眨眼/好奇），自动眨眼、眼球跟随、呼吸起伏、说话气泡。
- **环境感知**：脸上两角实时显示 `光xx%`（左）和 `温xx.x`（右），并影响宠物的情绪。
- **「盖住我」彩蛋**：用手盖住光敏（GPIO36），光照跌到 **3% 以下**，宠物就会说「好舒服啊」并冒爱心（实测：待机 27~31%、手盖 0~2%，余量约 10 倍，详见下文）。
- **养成系统**：饥饿 / 快乐 / 体力 / 健康 / 清洁 五条状态，会随时间下降；会拉便便、会生病、会睡着；分 5 个成长阶段（蛋 → 婴儿 → 小孩 → 少年 → 成年）。
- **互动菜单**（11 项）：喂食、玩耍、睡觉、洗澡、吃药、状态、音乐、表情、网络、关于、重来。
  喂食 / 洗澡 / 吃药是**三拍动作**：先播 0.65 s 动画（果子飞进嘴 / 泡泡 / 药片），动画结束才改数值，
  再按真实结果给表情和台词（吃饱了会拒绝、睡着时会「睡着了」），详见「照顾动作：先动画，后加值」。
- **小游戏**：「接食物」——水果/糖果加分，石头扣命，3 条命。
- **音乐**：欢乐颂、小星星、生日快乐（无源蜂鸣器演奏）。
- **WiFi 配网**：打开「网络」菜单会开一个软 AP，用手机连上后可在浏览器里改**话术、对话间隔、表情变化间隔、成长速度**，保存即时生效；
  同一个页面还能**上传开机图片**（选图后手机先把图缩到 160×128，再传给宠物）。
- **开机图**：上电先显示一张全屏图片（默认是宠物脸 + `ESP32` / `PIXEL PET` 字标），约 1.5 秒后进入欢迎页，再进入主界面；
  按 **A/B** 可跳过。图片可以**用手机上传**（存进 Flash，优先于内置图，可随时删掉换回默认），
  也可以由 `tools/gen_splash.ps1` 生成并编进固件（见「开机图」）。
- **掉电保存**：宠物状态和设置都存进 ESP32 的 NVS（Flash）。

---

## 硬件与接线

引脚定义都在 [`include/config.h`](include/config.h)，可自行修改。

### 按键（6 键，默认 GND/低电平触发，内部上拉）

| 功能 | GPIO | 备注 |
|------|------|------|
| UP   | 2    | |
| DOWN | 13   | |
| LEFT | 27   | |
| RIGHT| 35   | 仅输入，无内部上拉 |
| A（确定）| 34 | 仅输入，无内部上拉 |
| B（返回）| 12 | 启动时须保持低电平（strap 引脚） |

> `BTN_AUTO_POLARITY 1`：开机自动识别按键是上拉还是下拉，34/35 这类没有内部上拉的引脚也能用。

### 蜂鸣器

| 功能 | GPIO |
|------|------|
| 无源蜂鸣器 `+` | 14 |

### 传感器（ADC1）

| 功能 | GPIO | 说明 |
|------|------|------|
| LDR（光敏）| 36 | 分压后读亮度百分比 |
| NTC（热敏）| 39 | 分压后算温度 |

### TFT（ST7735，SPI，与 SD 共用总线）

| 信号 | GPIO |
|------|------|
| MOSI | 23 |
| SCLK | 18 |
| CS   | 5  |
| DC   | 4  |
| RST  | 19 |

### 其他预留

| 功能 | GPIO |
|------|------|
| 预留 I2C SCL / SDA | 15 / 21 |
| microSD CS | 22（`USE_SD 0`，默认关闭） |

---

## 软件结构

同一份固件里有 **两套 UI**，由 [`include/config.h`](include/config.h) 里的 `USE_LVGL` 选择：

| `USE_LVGL` | UI 实现 | 提供 `setup()/loop()` 的文件 |
|-----------|---------|------------------------------|
| `1`（默认）| LVGL v8 控件界面，动画脸用 `lv_canvas` 显示 | [`src/app_lvgl.cpp`](src/app_lvgl.cpp) |
| `0` | 原始 TFT_eSPI 即时模式界面 | [`src/main.cpp`](src/main.cpp) |

两者**恰好只有一个**会被编译。TFT_eSPI 始终是底层显示驱动，LVGL 通过一块 `160×40` 的绘制缓冲刷屏（见 `lv_port.cpp`）。

### 目录 / 文件

```
include/
  config.h        全局引脚、屏幕几何、调色板、行为参数
  buttons.h       6 键去抖 + 边沿 / 长按
  buzzer.h        无源蜂鸣器驱动 + 音效/歌曲枚举
  eyes.h          动画脸引擎 EyeEngine（表情、眨眼、气泡、HUD、照顾动作）
  pet.h           宠物状态机 Pet（五维状态、阶段、归档）
  careaction.h    照顾动作状态机 CareAction：动画 → 结算 → 表演（两套 UI 共用）
  game.h          「接食物」小游戏
  sensors.h       LDR + NTC 采样
  ui.h            状态条 / 菜单 / 信息页绘制（原始界面用）
  cnfont.h        16×16 中文点阵字体的取字 / 绘制 API
  lv_font_cn.h    生成的 LVGL 中文字体
  lv_port.h       LVGL <-> TFT_eSPI 桥接
  settings.h      NVS 持久化的用户参数 PetCfg
  netconfig.h     软 AP + 设置网页
  splash.h        开机图片：位图 / 上传图接口

src/
  app_lvgl.cpp    LVGL 界面（12 个屏幕）+ LVGL 版 setup()/loop()
  main.cpp        原始 TFT_eSPI 界面 + 原始 setup()/loop()
  eyes.cpp        脸/眼睛的绘制（精灵缓冲）
  pet.cpp         宠物逻辑、成长、拉屎、生病、读写 NVS
  careaction.cpp  照顾动作三拍：先动画、再结算、后表演（两套 UI 共用）
  game.cpp        小游戏逻辑
  sensors.cpp     ADC 采样与换算
  ui.cpp          原始界面的面板绘制
  buttons.cpp     按键扫描
  buzzer.cpp      蜂鸣器 / 播放器
  cnfont.cpp      16×16 中文点阵绘制（含缩小绘制 Cn...Sm）
  cnfont_data.cpp 【生成】中文点阵数据
  lv_font_cn.c    【生成】LVGL 中文字体
  lv_port.cpp     LVGL 移植层
  settings.cpp    PetCfg 默认值 / 读写 / 话术池
  netconfig.cpp   软 AP + WebServer 配置页（含开机图上传）
  splash.cpp      开机图片的显示 + 上传图的读写（两套 UI 共用）
  splash_data.cpp 【生成】开机图片位图 160×128 RGB565

tools/
  gen_cnfont.ps1       扫描源码生成 src/cnfont_data.cpp
  gen_lvgl_font.ps1    扫描源码生成 src/lv_font_cn.c
  gen_splash.ps1       生成 src/splash_data.cpp（开机图片，可 -Image 换成自己的图）
  dump_lvgl_glyph.ps1  反查生成字体里某个字的点阵（调试）
  read_serial.py       [COMx] [秒] 复位并从启动日志开始读串口（默认 6 秒）
  _diag_page.js        把设置页里的 JS 抽出来跑（缩放几何 / RGB565 字节序 / 留边色）
```

> 根目录里的 `LVGL前.zip` / `LVGL后.zip` 是历史备份，不参与编译。

### 屏幕清单（`AppScreen`）

`WELCOME` 欢迎 → `FACE` 脸 → `MENU` 菜单，以及 `STAT` 状态 / `SLEEP` 睡觉 / `EMOTE` 表情 / `MUSIC` 音乐 / `ABOUT` 关于 / `NET` 网络 / `CANT` / `RESULT` / `GAME`。

---

## 编译与烧录

需要 **PlatformIO**（VS Code 插件或命令行）。依赖（`TFT_eSPI`、`lvgl`）在 `platformio.ini` 里声明，首次编译会自动下载。

```powershell
# 编译
pio run

# 编译并烧录到 COM5（按你的实际串口修改）
pio run -t upload --upload-port COM5

# 打开串口监视器（115200）
pio device monitor -p COM5 -b 115200
```

> 本仓库环境里 PlatformIO 装在用户目录，等价写法：
> ```powershell
> & "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -t upload --upload-port COM5
> ```

`platformio.ini` 里的 TFT 驱动、引脚、颜色表都是通过 `build_flags` 配置的，**不需要**去改库里的 `User_Setup.h`。

### 常用排错开关（`platformio.ini` 的 build_flags）

| 现象 | 追加/修改 flag |
|------|----------------|
| 画面整体偏移 / 颜色不对 | 试 `-DST7735_REDTAB=1` 或 `-DST7735_GREENTAB=1` |
| 红蓝互换 | `-DTFT_RGB_ORDER=TFT_BGR` |
| 画面反色 | `-DTFT_INVERSION_ON=1` |
| 竖屏 | 把 `config.h` 里 `SCR_ROTATION` 改成 `0` 或 `2` |

> 编译结尾出现 `[Command exited with code 1]` 但日志里有 `[SUCCESS]`，通常是 TFT_eSPI 在 stderr 上打了一条 `#warning`，是**无害**的。

### 烧录卡在下载模式 / 找不到串口

ESP32 若没有自动进下载模式，用工具手动拉 `EN`（可用 `tools/read_serial.py COM5` 的 RTS 手法），或按住 BOOT 再按 EN。

---

## ⚠️ 中文字体必须重新生成

项目的中文字是**编译期烘焙**成点阵/字体的，生成脚本会扫描 `src/*.cpp` 与 `include/*.h` 里所有 **码点 > 0x7F 的字符**再渲染。

**只要你改动/新增了界面里的中文文字，就必须重新跑脚本再编译**，否则新字显示为空白：

```powershell
powershell -ExecutionPolicy Bypass -File tools/gen_cnfont.ps1      # -> src/cnfont_data.cpp
powershell -ExecutionPolicy Bypass -File tools/gen_lvgl_font.ps1   # -> src/lv_font_cn.c
```

脚本输出会报告字数与「无墨（空白）」字符数，正常应为 `0 CJK codepoints had no ink`。

> 想确认某个字生成得对不对，可用 `tools/dump_lvgl_glyph.ps1 0x41 0x597D` 把它从生成字体里还原出来看。

---

## 操作说明

### 开机图
上电后先显示一张 160×128 的**全屏图片**（[`src/splash_data.cpp`](src/splash_data.cpp) 里的位图，由 `tools/gen_splash.ps1` 生成），
停留 `SPLASH_MS`（默认 1500 ms，按 **A/B** 直接跳过），然后 LVGL 的第一帧把它整个覆盖成下面的欢迎页——
所以开机顺序是 **开机图 → 欢迎页 → 主界面**。它由 `setup()` 在 `tft.init()` 之后、LVGL 启动之前用
TFT_eSPI 的 `pushImage()` 直接推到屏上（`src/splash.cpp`），因此**不占 LVGL 内存池**，两套 UI 表现一致。

- **换成自己的图**（PNG/JPG/BMP 都行，GDI+ 能读的都可以）：
  ```powershell
  powershell -ExecutionPolicy Bypass -File tools/gen_splash.ps1 -Image C:\logo.png
  ```
  也可以把图存成 `tools\splash.png` 后**直接跑脚本**（不带参数）。脚本会按 `-Fit cover`（默认，铺满并裁掉多余）
  缩放；想留边不裁切用 `-Fit contain`。**跑完重新编译烧录**才会生效。
- **不传图片时**，脚本会画一张内置图案（宠物脸 + `ESP32` / `PIXEL PET` 字标），所以仓库里不必存图片文件。
- **代价**：位图是**编译进固件**的，占 `160×128×2 = 40 KB` flash（app 槽 1.25 MB，加完约 87%）。
  不想要就把 [`config.h`](include/config.h) 的 `USE_SPLASH` 设为 `0`，位图整段被裁掉，开机直接进欢迎页。
- **像素对齐**：位图按面板横向布局（`SPLASH_W/H` 必须等于 `SCR_W/SCR_H`），`splash_data.cpp` 里有 `static_assert` 守着，
  改屏幕几何忘了改这里会**编译报错**而不是画面错位。

#### 用手机上传自己的开机图（不用重烧固件）
菜单 → **网络** → 手机浏览器打开 `http://192.168.4.1` → 页面底部「**开机图片**」→ 选图 → **上传为开机图片**。

- **缩放发生在手机里**：页面用 `<canvas>` 把选中的图缩到 160×128、转成 RGB565 的 **40960 字节**再上传，
  所以原图（哪怕 4000×3000）根本不走 WiFi，宠物这边也不需要解码器和它的内存。
  缩小时按 2× 逐次减半再走最后一档（一次从 4000 px 直接缩到 160 px 会糊）。
- **铺满 / 完整显示**：与脚本的 `-Fit cover|contain` 一一对应（`cover` 裁掉多出来的边，`contain` 四周留底色 `COL_BG`）。
- **存哪儿**：写进 **`spiffs` 分区**（默认分区表里 `0x290000` 起的 1.4 MB，一直没人用）的 `/splash.bin`，由 LittleFS 管理，
  首次挂载时格式化这一块。**app 槽、OTA 槽和 NVS 设置都不动**，所以「上传图片」和「烧固件」互不影响。
  想要的话 `pio run -t uploadfs` 写的也是同一块（`data/` 目录，文件得是同样的裸 RGB565）。
- **生效时间**：图片只在**开机时**显示；上传完页面里有「**重启宠物**」按钮直接重启它（热点会断开），也可以按 RST。
  上传的图**优先于**编进固件的内置图。
- **改回默认**：页面里「**删掉它，恢复默认图**」。
- **代价**：上传功能要带 LittleFS，约 **+49 KB** flash（90.6%）。把 [`include/config.h`](include/config.h) 的
  `USE_SPLASH_UPLOAD` 设为 `0` 就整段去掉（回到 86.8%），页面里也不再出现这一栏。
- **为什么不直接 POST 原始 body**：core 的 `WebServer` 对**非表单** body 会先整段读成一个 `String`（遇到 `0x00` 还会截断），
  位图根本不能走那条路；上传因此用 `multipart/form-data`——core 里唯一会**流式**回调的形式，每 1436 字节写一次 Flash，
  40 KB 图片不会整块进 RAM。
- **自检**：`node tools/_diag_page.js` 把页面里那段 JS 抽出来喂给桩 canvas 跑，检查缩放几何、
  RGB565 的字节序（低位在前）以及留边色是否和 PC 端脚本一致。

### 欢迎页
- 第一行是**开机标题**、第二行是**问候语**（在 WiFi 页面里改，见「WiFi 配网」）；两者都在开机图之后显示。
- 按 **A** 或 **B** 进入（停留超过 6 秒也会自动进入）。

### 主界面（脸）
| 键 | 作用 |
|----|------|
| **A** | 打开菜单 |
| **B** | 摸它（开心 + 爱心表情）|
| **↑ ↓ ← →** | 让眼睛朝对应方向看 |

### 菜单（11 项）
`↑/↓` 移动高亮，**A** 确认，**B** 返回。菜单项：

**喂食 · 玩耍 · 睡觉 · 洗澡 · 吃药 · 状态 · 音乐 · 表情 · 网络 · 关于 · 重来**

每行是「**图标 + 文字**」：左边一个图标（LVGL 自带的 `LV_SYMBOL_*`，清单见 `app_lvgl.cpp` 的 `MENU_ICON[]`），
后面是两项中文。逐个对应 `BELL · PLAY · EYE_CLOSE · TINT · PLUS · BARS · AUDIO · IMAGE · WIFI · FILE · REFRESH`。
图标全部来自已经链接进去的 Montserrat 字体，既不占 flash 也不需要图片资源；它和中文写在
**同一个 label** 里，靠 `uistyle.h` 的 `uiFontSymbols()`（CJK 字体 + Montserrat 的 fallback 链）合并显示——
所以菜单**没有多一个部件**，池占用实测不变（仍 74%）。焦点行整行变亮，图标与文字同色。
原始 TFT_eSPI 界面（`USE_LVGL 0`）的菜单仍是纯文字。

- **喂食**：先看它吃（约 0.65 s 动画），**动画结束才加饥饿、涨体重**；吃饱了会拒绝。
- **玩耍**：进入「接食物」小游戏。
- **睡觉**：切换睡觉/起床（睡觉时体力回复）。
- **洗澡**：先看泡泡（动画），再清洁度拉满。
- **吃药**：先看药片飞进嘴（动画），再生病治疗 + 回血。
- **状态**：显示五维状态、体重、年龄、阶段等。
- **音乐**：选曲播放（见下）。
- **表情**：表情画廊，`←/→` 翻看全部 12 种表情。
- **网络**：开启/关闭 WiFi 配网 AP（见下文）。
- **关于**：版本、作者、引脚信息。
- **重来**：仅在宠物死亡后可重置。

### 音乐
`↑/↓` 选曲，**A** 播放，**B** 停止/返回。三首：**欢乐颂、小星星、生日快乐**。

### 睡觉 / 表情 / 信息页
- 睡觉页：**A** 起床，**B** 返回。
- 表情页：**←/→** 切换表情（**↑/↓** 和 **A** 也能切换），**B** 返回。屏幕下方那行提示写的就是「左右切换」。
- 信息/关于页：**A** 或 **B** 返回菜单。

### 彩蛋：用手盖住传感器
在主界面（脸）上用**手盖住光敏（GPIO36）**，宠物就会说「好舒服啊」并冒爱心。

#### 判定
**光照读数接近 0**（`light <= COVER_DARK_PCT` = 3%）**并持续约 0.4 秒**（`COVER_HOLD_MS`）就算一次手势。实测余量很大，光敏这一路本身就是主力：

| 状态 | 光敏原始值 | 显示值 |
|---|---|---|
| 待机（房间灯亮） | raw 1100~1250 | 27~31 % |
| 待机（关灯，只余一点余光） | raw 300~780 | 5~9 % |
| **手盖住** | **raw 19~161** | **0~2 %** |

#### 温度这一条只是次要条件
`COVER_RISE_DECI`（0.1 ℃，即「比之前有变化」）实测**几乎不提供鉴别力**：
- 热敏**基本感觉不到手**——手盖住时，光敏从 raw 1220 掉到 19，而热敏原始值只动了不到 10 个数（手掌的热量传不到 NTC 上）；
- 而「慢速参考温度」在 100 ms 采样节拍下时间常数约 10 秒，室温只要缓慢漂移就会稳定滞后出 **0.1~0.4 ℃** 的 `rise`——**没有手的时候也是这个量级**。

所以它**永远不会挡住一次真手势**（保持原样即可），也不要指望它区分「手」和「晃过的影子」——真正把关的是光敏那一路。

#### 复原 / 重新武装
松手后光照回升到环境光基线的 **80%**（`COVER_REARM`）以上才重新武装。环境光基线是**峰值保持**：立刻跟随任何变亮，再以 `COVER_AMB_DECAY`（约 10 秒时间常数）缓慢衰减，并且在「被盖住」期间冻结——这样长时间按住手只会触发一次，松手后立刻又能玩。

（环境光基线只用于这个「复原」判断，**不参与**「是否被盖住」的判定——否则半覆盖时的低值会被当成「环境光」，把重新武装的门槛拉到 2% 左右，手一直按着就会反复触发。）

#### 实测（本机 COM5，连续 90 秒，跨越「灯亮 30% → 关灯 5~9% → 再开灯」）
- **手盖 5 次 → 触发 5 次**（无漏报）；
- **关灯 15 秒 → 0 次误触发**（该房间关灯后光敏仍有 5~9%，高于 3% 阈值）；
- 手扫过 / 影子造成的短暂下陷（读数 5、6、14、15、17、18、20 %）**全部被正确忽略**。

#### 诊断
出厂默认 `COVER_DEBUG 0`（不刷屏）。想看判定过程就把 [`include/config.h`](include/config.h) 里的 `COVER_DEBUG` 改成 `1` 重新编译：串口会每秒打一行 `[cover] light=..% amb=..% temp=..C ambT=..C rise=.. idle/COVERED`（`amb` 就是那个峰值保持的环境光基线，`rise` 是温度高出参考值的幅度）。

触发反应时另打印 `[sensor] hand-cover gesture -> the pet says 好舒服啊`（这条由 `BTN_DEBUG` 控制，默认开着）。注意触发反应还有门控：宠物必须**醒着且停在脸界面**（`screen == SCR_FACE && !dead && !asleep`）。

#### 可调阈值
都在 [`include/config.h`](include/config.h)：`COVER_DARK_PCT`、`COVER_RISE_DECI`、`COVER_HOLD_MS`、`COVER_REARM`、`COVER_AMB_DECAY`、`COVER_DEBUG`。

⚠️ **已知边界**：若房间本身暗到光敏读数为 0~3 %（例如深夜毫无余光），「被盖住」与「本来就黑」在光敏上无法区分，而温度判据又恒为真，理论上会误触发。缓解办法：把 `COVER_DARK_PCT` 调低（如 2），或让宠物夜间保持睡眠（睡眠时不反应）。

---

## 养成机制

- 状态每 **2 秒** tick 一次（`PET_TICK_MS`）。各项每满足周期掉 1 点：
  - 饥饿 −1 / 96 秒（满→空约 **2.7 小时**）
  - 快乐 −1 / 120 秒（约 **3.3 小时**）
  - 体力 −1 / 112 秒（约 **3.1 小时**）
  - 清洁 −1 / 360 秒（约 **10 小时**）
  - 健康缓慢漂移
- 睡觉时体力回复（`ENERGY_SLEEP_GAIN`）。
- 会随机拉便便（最多 4 堆）、挨饿或太脏时有概率生病。
- **年龄跟真实时钟走**：100% 成长速度时 **1 龄分钟 = 1 真实分钟**（`AGE_MIN_MS`），所以开一小时就是 `龄 1时`。
  状态页的「龄」用中文单单位（`petAgeText()`）：`42分` → `23时` → `6天`，不会出现「好几千 m」这种数字。
  只给一个单位是因为数据页的值列只有 38 px（`app_lvgl.cpp` 的 `colW - 26`，右对齐、超出即裁切），
  而 16 px 中文一格，「6天6时」就已经塞不下了；**精确到分钟的原始值在开机日志里**
  （`[pet] state: inherited from NVS, age 9060 min`）。
- **成长阶段**：蛋 → 婴儿 → 小孩 → 少年 → 成年（`AGE_CHILD/TEEN/ADULT` = 30 / 120 / 360 龄分钟，
  即 100% 时约 **30 分钟 / 2 小时 / 6 小时**，整条曲线可被「成长速度」20%~400% 缩放，
  见 `pet.cpp` 的 `tick()`）。开机日志会把这条换算直接算给你看：
  `[pet] age: 1 age-min per 60 s (speed 100%), 30 min to child, 6 h to adult`。
- 状态会自动存进 NVS，每分钟自动保存一次（`PET_AUTOSAVE_MS`）。
- 升级说明：老存档里的 `ageMin` 是老口径（**1 tick = 1 龄分钟**，快了 30 倍）攒出来的，
  升级后不清零，只是按新口径继续往下走、并显示成一个很大的天数；想从头养一只新宠物就
  清空 NVS（`pio run -t erase` 之后再烧录）——「重来」只在宠物死亡后可用。

所有数值都能在 [`include/config.h`](include/config.h) 里调；间隔/成长速度也能在 WiFi 页面在线调。

---

## 小游戏：接食物

- 移动 *左右键* 接住掉落的食物：🍎水果、🍬糖果 加分，🪨石头 扣命。
- 3 条命用完游戏结束，结算会反馈给宠物（快乐/体重）。
- 退出：游戏内按返回键。

---

## WiFi 配网（手机改参数）

菜单 → **网络**：此时 ESP32 会开启一个软 AP 并启动一个小网页服务。

1. 手机 WiFi 连接热点：**`ESP32-Pet-XXXX`**（XXXX 取自芯片 MAC，唯一）
2. 密码：**`pet12345`**
3. 浏览器打开：**`http://192.168.4.1`**
4. 在页面里设置，点 **保存**：话术 / 间隔 / 成长速度**立即生效**，
   开机标题 / 问候语在**下次开机**显示（先显示**开机图**，再显示这两行，见「开机图」）。
5. 页面底部的「**开机图片**」可以直接选图上传（同样见「开机图」），旁边还有「重启宠物」和「恢复默认图」。

| 项目 | 说明 |
|------|------|
| **话术** | 勾选/取消内置句子（共 24 句）。勾上的才会被随机说出。 |
| **对话间隔（秒）** | 主动说话的间隔范围（最短~最长）。 |
| **表情变化间隔（秒）** | 表情「小动作」的间隔范围（越小越活泼）。 |
| **成长速度** | 20~400%，100 为正常（1 龄分钟 = 1 真实分钟），数值越大长得越快。 |
| **开机标题** | 欢迎屏第一行，只能填**英文/数字**（最多 16 字符，留空恢复默认 `ESP32 Pixel Pet`）。 |
| **开机问候语** | 欢迎屏第二行，从 5 句内置中文问候语里选一句；「关闭」则显示版本号。 |
| **开机图片** | 手机选图 → 页面先缩成 160×128 → 上传到 Flash，开机优先显示（可铺满/完整显示，可一键恢复默认图）。 |

设置项存进 NVS 的 `cfg` 命名空间，掉电不丢。

> **为什么话术是「勾选」、开机标题只能填英文？**
> 因为中文字体是**编译期烘焙**的，运行时你手打的字没有任何点阵可以显示。所以内置了 24 句话术字面量（脚本会把它们的字全部打进字库），用户通过勾选来「增/减」；开机标题因此只收英文/数字，中文改用内置的**开机问候语**（同样是字面量，字已被烘进字库）。
> 改了 `settings.cpp` 的 `BOOT_LINES[]` 问候语内容后，**记得重跑两个字体脚本**再编译。
>
> **省电**：软 AP **只在「网络」这一屏显示时**开启，按 **B/A** 退出该屏会立刻关掉无线，正常使用时射频是关闭的。

---

## 可调参数

### 编译期（[`include/config.h`](include/config.h)）
- 按键引脚/极性、蜂鸣器/传感器引脚
- 屏幕旋转 `SCR_ROTATION`、几何、调色板
- 行为数值：`PET_TICK_MS`、各项 `*_PERIOD_TICKS`、`PET_TALK_MIN/MAX_MS`、`PET_AUTOSAVE_MS`
- 照顾动作动画时长 `CARE_ANIM_MS`（默认 650 ms，见「照顾动作：先动画，后加值」）
- 开机图片 `USE_SPLASH` / `SPLASH_MS`（默认开、1500 ms，按 A/B 跳过；见「开机图」）
- 开机图上传 `USE_SPLASH_UPLOAD`（默认开，带 LittleFS，约 +49 KB flash）、
  `SPLASH_STRIP_ROWS`（每批推几行，默认 16 = 5 KB RAM，见「开机图」）
- `USE_LVGL` 切 UI、`USE_SD` 开 SD 存档、`UI_SWEEP` 开机自检（12 屏 + 三种照顾动作）、`BTN_AUTO_POLARITY`/`BTN_DEBUG` 等

### 运行期（WiFi 页面）
- 话术勾选、对话间隔、表情变化间隔、成长速度（存 NVS）

### 代码里的小旋钮
- `src/eyes.cpp` 顶部 `HUD_GLYPH_PX`：角落 `光/温` 两个字的像素大小（默认 `12`，改 `10` 更小、`16` 还原）。
- `src/settings.cpp` 的 `PHRASES[]`：内置话术字面量（改这里后**记得重新生成字体**）。

---

## LVGL 界面设计系统（[`include/uistyle.h`](include/uistyle.h)）

所有 LVGL 屏幕（`src/app_lvgl.cpp`）都只用 `uistyle.h` 里的设计令牌取色、取尺寸，
所以改一个数字就是全界面统一改。**不要在 app_lvgl.cpp 里写死颜色/字号。**

| 类别 | 令牌 | 值 | 用途 |
|------|------|----|------|
| 底色 | `COL_BG` | `#080A14` | 全屏背景（不用纯黑，避免 OLED/IPS 死黑）|
| 卡面 | `UI_CARD1` → `UI_CARD2` | `#20273F` → `#111626` | 卡片/焦点行：竖向渐变 |
| 边线 | `UI_EDGE` / `UI_SEP` | `#38466C` / `#1E243A` | 卡片描边；表格行分隔（1 px 发丝线）|
| 强调 | `COL_ACCENT` / `UI_ACC` | `#00C8FF` / `#60CDF5` | 标题、焦点标记；次级强调 |
| 状态 | `COL_GOOD/WARN/BAD` | 绿/琥珀/红 | **只**用在状态条和状态词上 |
| 圆角 | `UI_R_CARD` / `UI_R_PILL` | 5 px / 8 px | 卡片与焦点行；胶囊提示条 |
| 节奏 | `UI_PAD` / `UI_GAP` / `UI_ROW_H` | 4 / 2 / 17 px | 页边距、行内间距、行距 |
| 字号 | `UI_TXT_CJK` / `UI_TXT_SM` / `UI_TXT_HERO` | 16 / 14 / 20 px | 汉字、数字与拉丁、页面唯一的大数字 |

约定（照抄就能保持观感一致）：

- **一屏一主角**：数据页只有一个「大数字」（`UI_TXT_HERO`，见 `setHero()`），
  其他数字一律 `UI_TXT_SM`，汉字一律 `UI_TXT_CJK`；`setValue()` 会按内容自动选字体。
- **三段式页面**：`mkHeader()` 标题带（渐变 + 发丝线 + 强调条 + 右侧 `B:返回`）→ 一张
  `mkCard()` → 卡内表格/居中行；数据页是「暗汉字标签 + 亮数字值」，消息页是居中对话框。
- **图标**：只用 LVGL 自带的 `LV_SYMBOL_*`（Font Awesome，已包含在 Montserrat 字体里），
  不引入任何图片资源；几何图形（强调条、发丝线、圆点、胶囊）都用对象画。
  菜单每行 = `MENU_ICON[]` 里的图标 + 16 px 中文，写在同一个 label 里
  （`uiFontSymbols()` 用 fallback 链把 CJK 字体接上 Montserrat，见 `buildMenu()`）。
- **动效**（都在 `app_lvgl.cpp`，不额外占内存）：

  | 动效 | 位置 | 说明 |
  |------|------|------|
  | 切屏淡入 | `setScreen()` | `lv_scr_load_anim(FADE_IN, 140 ms)`；游戏/欢迎屏直切 |
  | 焦点淡变 | `uiStyleFade()` | 菜单行（含图标）、表情圆点共用一条 120 ms ease-out transition |
  | 呼吸 | `animOpaLoop()` | 睡觉页 `z z z`、欢迎页「A开始」明暗呼吸 |
  | 电平表 | `animEqLoop()` + `eqSetPlaying()` | 音乐页 8 根柱子，只在播放时跳动 |
  | 状态条 | `setBar()` | 数值变化时逐帧跟手（`LV_ANIM_OFF`，不排队补间）|
  | 表情 | `eyes` 引擎 | 12 种表情 + 眨眼/眼球跟随，画在 canvas 上（非 LVGL 部件）|
  | 照顾动作 | `eyes` 引擎 `drawAction()` | 喂食（果子飞进嘴+咀嚼碎屑）/ 洗澡（两列泡泡上升）/ 吃药（药片飞入+苦味冲击环），同样画在 canvas 上 |

### 照顾动作：先动画，后加值（`careaction.h`）

喂食 / 洗澡 / 吃药不是「点一下数值就跳」，而是**三拍**：

```
[A] 起手 ──① 动画 650 ms（CARE_ANIM_MS，数值一动不动）
          ──② 结算：pet.feed()/wash()/medicine() + pet.save()   ← 数值只在这一刻变
          ──③ 表演：按「结算结果」选表情 + 气泡 + 音效
```

| 结果 | 何时发生 | 看的/听的是 |
|------|----------|-------------|
| `CARE_OUT_OK` | 正常生效 | 喂食=开心 + 「好吃」+ 咀嚼音；洗澡=开心 + 「好舒服」；吃药=好奇 + 「好多了」|
| `CARE_OUT_FULL` | 饥饿 ≥ 95 还喂 | 生气 0.9 s + 「吃不下了」（并且记一次照顾失误，快乐 −4）|
| `CARE_OUT_ASLEEP` | 睡着时喂 | 困倦 + 「睡着了」（数值不变）|
| `CARE_OUT_DEAD` | 死亡时喂 | 死亡表情 + 叹气音，**不说话**（数值不变）|

为什么不「先加值再动画」（改之前就是这样）：喂睡着的宠物以前会**什么都没发生却照样说「好吃」**，
吃饱了也只会静默扣分。把结算放在动画之后，UI 才能用对应的脸去解释真实结果 —— 而且
`feed()` 现在返回 `CareOutcome`（原来是 `void`），"拒绝"这件事才第一次有了反馈。
动画结束时才判定，也顺带覆盖了「这 650 ms 里它睡着了/死了」的情况。

实现要点（改这块之前务必知道）：

- 定时**不能**用 `delay()`：`CareAction::update()` 由 `loop()` 每帧调用，按 `millis()` 记时，屏幕在动画
  期间随便切，动作照样会结束；
- `eyes.playAction()` / `endAction()` 与结算在同一帧配对，动画和数值不会各走各的；
- 动画画在脸精灵里（`src/eyes.cpp` 的 `drawAction()`），**不新增 LVGL 部件，不占 LVGL 内存池**；
- 动画期间 `frameFace()` 会静音闲聊和随机表情，避免气泡盖住动画；
- 结算那一帧要把 `lastExpr` 设成 `pet.expressionMood()`（**不要**设 `MOOD_COUNT`）：设成
  `MOOD_COUNT` 会让下一帧的 `setBaseMood()` 把三拍的表情闪现在 33 ms 内抹掉（老代码的
  `flashMood` 其实一直是这样被秒清的）；
- 两套 UI（`app_lvgl.cpp` 与 `main.cpp`）共用这一个状态机，改行为只改 `careaction.cpp`，
  改画面才动各自的 `menuAction()` / 刷新函数。

### LVGL 内存（加部件之前先看这一节）

LVGL 的内存池是**静态数组**（`lv_conf.h` 的 `LV_MEM_SIZE`，位于 `.dram0`），而本项目的
12 个页面在开机时一次性建好，所以池要一次装下整界面。实测数字如下：

| | |
|---|---|
| 池大小 | 65536 B（64 KB）|
| 建完 12 屏后 | 74% used / 17 KB free |
| 走完所有页面后 | 81% used / 12.8 KB free（缓存了渐变贴图等）|
| 系统堆剩余 | 176.9 KB（`[mem] free heap ...`）|

- 开机日志里有两行体检结果，**动 UI 之前先看它们**：

  ```
  [pet] state: inherited from NVS, age 9060 min
  [pet] age: 1 age-min per 60 s (speed 100%), 30 min to child, 6 h to adult
  [mem] free heap 176880 B after the scene sprite
  [ui] LVGL pool 65536 B: 74% used, 17088 B free (biggest block 17088 B, frag 0%)
  ```

  `free` 掉到 1~2 KB 就是危险区：下一次分配失败不会报错，而是以「莫名其妙重启 / 花屏」
  的形式出现。
- 踩过的坑：池原来是 48 KB，实测 **99% 满（只剩 704 B）**。LVGL 画渐变卡片时会临时向池里
  要 300~400 B，拿不到就返回 NULL，而 LVGL 8.4 的 `draw_bg()` 不判空、直接解引用 ——
  结果「切到第二个屏就 panic（`LoadProhibited`，`lv_draw_sw_rect.c:267`）」。现在两处一起治：
  `LV_MEM_SIZE` 提到 64 KB（余量 ~13 KB），并把 `LV_GRAD_CACHE_DEF_SIZE` 设为 4 KB，
  渐变贴图走专用缓存，不再和普通分配抢内存。
- 池为什么不能随便再调大：它是 `.bss` 静态数组，和 `.dram0` 抢地方（再多 3 KB 链接就报
  `region dram0_0_seg overflowed`）。所以 160x40 的 LVGL 绘制缓冲（12.8 KB）已改成开机时
  `malloc` 放堆里（见 `lv_port.cpp`），把 `.dram0` 让给内存池。
- 想一次性确认所有页面都画得出来：把 `config.h` 的 `UI_SWEEP` 设 1 编译一次，开机会每屏渲染
  6 帧并打印 `[ui] sweep n/12 ok`；有问题的页面会在启动日志里当场暴露，而不是等你手按进去。
  它随后还会把三种照顾动作各跑一遍（`[ui] care demo n/3 ok`，并打印 `mood/base` —— 用来确认
  三拍的表情闪现没有被 `lastExpr` 记账抹掉），跑完自动把宠物状态还原，不会真的喂到它。

### 表情 / emoji 字体能不能上？（结论：暂不引入）

- 本项目的宠物表情是 `src/eyes.cpp` **自己画的 12 种脸**（眼睛/嘴型/眨眼/HUD），
  比任何 emoji 字形都生动，也不需要新字体；
- 文字里的装饰图标用 `LV_SYMBOL_*` 即可：0 资源、0 额外 flash（Montserrat 已含）；
- 真要「emoji 字形」有两条路，但都不值得现在做（本轮已实测/查证）：
  1. **生成一张单色 emoji 字体**（Noto Emoji / OpenMoji-black + `npx lv_font_conv`
     `--range 0x1F600-0x1F64F`）→ LVGL 8.4 的 UTF-8 解码与稀疏 cmap 都支持
     4 字节码点，技术上可行；但本项目自带的 `tools/gen_lvgl_font.ps1` 用 GDI+
     按 UTF-16 单元扫描源码，**渲染不了 U+1F600 以上的码点**，得再引一套 node
     字体工具链，且每多一档字号约为 16 KB flash（当前 flash 已用 83%）。
  2. **`LV_USE_IMGFONT` + 16×16 RGB565 小图**（`lv_imgfont_create()` 回调按码点
     返回图片，配 `lv_font_cn` 做 fallback）→ 支持任意码点、不需要图片解码器，
     但每张图 512 B、还要手绘素材。
- 结论：保持「自绘表情 + `LV_SYMBOL_*` 图标 + 三档字号」，flash 与 RAM 都留有余量；
  哪天真要在句子里插 emoji，按上面第 2 条加一张 imgfont 最省事。

---

## 常见问题

| 问题 | 处理 |
|------|------|
| 屏幕花屏 / 偏移 / 颜色不对 | 改 `platformio.ini` 的颜色表 flag（REDTAB/GREENTAB、`TFT_RGB_ORDER`、`TFT_INVERSION_ON`）|
| 画面上下颠倒 | 改 `config.h` 的 `SCR_ROTATION`（1↔3 或 0↔2）|
| 新增中文显示为空白 | **重新跑两个字体生成脚本**再编译：`tools/gen_cnfont.ps1` + `tools/gen_lvgl_font.ps1`（它们扫源码里的非 ASCII 字符；`时`/`分`/`天` 已烘进去）|
| 加完界面后上电几秒就重启 / 画面卡住 | 看串口 `[ui] LVGL pool` 的 free 值，太少就调大 `LV_MEM_SIZE`（见「LVGL 内存」）|
| 改了 UI，想确认 12 个页面都画得出来 | `config.h` 把 `UI_SWEEP` 设 1 编译一次，看串口 `[ui] sweep n/12 ok`（顺带跑 `[ui] age text 7/7 ok`、菜单图标与气泡自检）|
| 喂食/洗澡/吃药「没反应」或台词不对 | 正常：数值在 0.65 s 动画**之后**才变；睡着/吃饱/死亡会按结果回「睡着了 / 吃不下了 / 不说话」，详见「照顾动作」|
| 想改照顾动作的动画时长 | `config.h` 的 `CARE_ANIM_MS`（别超过气泡时长 1.5 s）|
| 想统一改配色 / 圆角 / 字号 / 间距 | 改 [`include/uistyle.h`](include/uistyle.h)，不要改 `app_lvgl.cpp` |
| 想加 emoji 字形 / 表情库 | 见上文「表情 / emoji 字体能不能上？」，默认不引入 |
| 年龄涨得太快 / 状态页出现「好几千 m」 | 已修：`龄` 现在按**真实分钟**计（100% 时 1 龄分钟 = 1 真实分钟）并显示成中文单单位 `42分 / 23时 / 6天`；想看原始分钟数看开机日志 `[pet] state: ..., age N min`；老存档留下的大数字见「养成系统」的升级说明 |
| 想改「多久成年」 | WiFi 页面「成长速度」20%~400%（存 NVS、即时生效），或改 `pet.cpp` 的 `AGE_CHILD/TEEN/ADULT`（单位是龄分钟）|
| 想确认当前的成长节奏 | 看开机日志 `[pet] age: 1 age-min per 60 s (speed 100%) ...`（把 NVS 里的速度也算好了）|
| 按键没反应或方向相反 | 设 `BTN_AUTO_POLARITY 1`，或按实际接线改 `BTN_PRESSED_HIGH` |
| 编译末尾报 `exited with code 1` 但看到 `[SUCCESS]` | TFT_eSPI 的 `#warning`，无害 |
| 连不上 `ESP32-Pet-XXXX` | 确认停在「网络」屏；AP 只在该屏开启 |

---

## 许可

个人学习/玩具项目，随意取用。作者：LGH。
