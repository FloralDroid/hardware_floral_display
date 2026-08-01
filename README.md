# Floral Display HAL

[English](#english) | [中文](#中文)

## English

`hardware_floral_display` provides the Floral HWC2 physical-display backend for
headless Android devices. The display is an Android physical output, not a
capture or recording path. Encoding and host transport remain separate from the
Composer call path.

### Current scope

- Composer/HWC2 2.4 dispatch surface.
- One permanent `INTERNAL` physical display on port 0.
- 15, 30, and 60 Hz configurations in one config group.
- CLIENT composition only.
- Monotonic VSync generation and stable Floral EDID identity.
- FrameSink submission with sequence, dataspace, damage, and fence metadata.
- Multiple physical external displays controlled by FloralStream topology.
- No HWC virtual display, readback, or protected-content claim.

The production HWC uses `StreamFrameSink` to connect to FloralStream through a
versioned AIDL endpoint. Until the service advertises an active generation, the
sink passes through the client-target acquire fence. Protected client targets
remain local to the display path and are never registered with the stream
service.

`libfloral_display_stream_bridge` keeps this boundary behind injectable
consumer and client-target resolver interfaces. Its tests cover
generation-scoped buffer registration, drop fallback, protected-buffer
rejection, and release-fence ownership. Encoding and socket transport remain
outside the Composer module.

### Display topology

The version-independent topology core can connect and disconnect physical
external displays while preserving the permanent primary identity. Registry
lookups retain shared display ownership so an in-flight HWC call can finish
safely after hot-unplug.

The Android 12 frontend subscribes to the container-internal
`floral.device.display.topology` VINTF AIDL service. Each increasing generation
replaces the complete external-display snapshot. Removal or a configuration
change emits disconnect events before replacement displays become visible. The
primary display is implicit in HWC and cannot be removed or renumbered by the
service.

Hotplug callback installation shares the same ordering boundary as topology
mutations, so the initial connected snapshot cannot race later events. If the
topology service remains unavailable, HWC retains the last external snapshot
for `ro.boot.floral_control_disconnect_lease_ms`, then removes it.

### Boot properties

The Floral display path reads these read-only boot properties:

| Property | Default | Valid range | Purpose |
| --- | ---: | ---: | --- |
| `ro.boot.floral_width` | `1920` | `320`-`7680` | Primary logical width in pixels. |
| `ro.boot.floral_height` | `1080` | `320`-`4320` | Primary logical height in pixels. |
| `ro.boot.floral_fps` | `60` | `1`-`60` | Requested primary refresh rate. |
| `ro.boot.floral_dpi` | `320` | `72`-`640` | Primary density and `ro.sf.lcd_density`. |

Invalid values fall back to deterministic defaults. Phase one caps the
advertised refresh rate at 60 Hz. HWC selects the lowest advertised rate that
is not lower than the request: 24 FPS selects 30 Hz and 45 FPS selects 60 Hz.
All refresh configurations remain in one config group, so a switch does not
change resolution, display identity, or Android logical display.

### Build and Android versions

Map this repository to `hardware/floral/display`, inherit `display.mk` from the
product, and use the platform Composer 2.4 service with its matching VINTF
manifest.

`libfloral_display_core` owns version-independent identity, topology, EDID, and
VSync policy. Android 12 uses an HWC2.4 frontend; Android 13 and later can add a
Composer 3 AIDL frontend without duplicating the core. Long-lived Android
baselines should use matching branches such as `android-12.0`, `android-13.0`,
and `android-14.0`.

### Security boundary

This HAL does not call capture APIs, expose readback, or provide raw-frame debug
interfaces. DRM protected buffers are outside the current contract and fail
closed.

## 中文

`hardware_floral_display` 为无头 Android 设备提供 Floral HWC2 物理显示后端。
该显示器属于 Android 的物理输出设备，不是录屏或旁路采集路径。编码和宿主传输
与 Composer 调用路径相互独立。

### 当前范围

- 提供 Composer/HWC2 2.4 调度接口。
- 在端口 0 上保留一个永久存在的 `INTERNAL` 物理主屏。
- 在同一配置组中提供 15、30 和 60 Hz 模式。
- 仅使用 CLIENT 合成。
- 生成单调递增的 VSync，并提供稳定的 Floral EDID 标识。
- FrameSink 提交包含序列号、dataspace、damage 和 fence 元数据。
- 支持由 FloralStream 拓扑控制的多个物理外屏。
- 不声明 HWC 虚拟显示、readback 或受保护内容能力。

生产 HWC 通过带版本的 AIDL 接口，使用 `StreamFrameSink` 连接
FloralStream。在服务公布有效 generation 之前，FrameSink 直接透传
client target 的 acquire fence。受保护的 client target 只停留在显示路径
内部，绝不会注册到串流服务。

`libfloral_display_stream_bridge` 使用可注入的消费者和 client target
resolver 接口隔离这条边界。测试覆盖按 generation 管理的缓冲区注册、丢帧
回退、受保护缓冲区拒绝和 release fence 所有权。编码与 Socket 传输不属于
Composer 模块。

### 显示拓扑

与 Android 版本无关的拓扑核心可以连接和断开物理外屏，同时保持永久主屏身份
不变。Registry 查询持有共享显示对象，因此热拔出发生后，正在执行的 HWC 调用
仍可安全结束。

Android 12 前端订阅容器内部的 `floral.device.display.topology` VINTF AIDL 服务。
每个递增的 generation 都会替换完整外屏快照。移除或配置变化会先发送断开事件，
再发布替换后的显示器。主屏由 HWC 隐式持有，服务不能删除主屏或修改其编号。

热插拔回调安装与拓扑变更共用同一个顺序边界，初始已连接快照不会和后续事件
竞态。拓扑服务持续不可用时，HWC 会在
`ro.boot.floral_control_disconnect_lease_ms` 指定的有界租约内保留最后一次
外屏快照，租约到期后再移除外屏。

### 启动属性

Floral 显示路径读取以下只读启动属性：

| 属性 | 默认值 | 有效范围 | 用途 |
| --- | ---: | ---: | --- |
| `ro.boot.floral_width` | `1920` | `320`-`7680` | 主屏逻辑宽度，单位为像素。 |
| `ro.boot.floral_height` | `1080` | `320`-`4320` | 主屏逻辑高度，单位为像素。 |
| `ro.boot.floral_fps` | `60` | `1`-`60` | 请求的主屏刷新率。 |
| `ro.boot.floral_dpi` | `320` | `72`-`640` | 主屏密度及 `ro.sf.lcd_density`。 |

非法值会回退到确定的默认值。第一阶段最高发布 60 Hz。HWC 选择不低于请求值的
最低已发布刷新率：例如 24 FPS 选择 30 Hz，45 FPS 选择 60 Hz。所有刷新率
配置位于同一配置组，因此切换时不会改变分辨率、显示器身份或 Android 逻辑屏幕。

### 构建与 Android 版本

将本仓库映射到 `hardware/floral/display`，由产品继承 `display.mk`，并使用平台
Composer 2.4 服务及匹配的 VINTF manifest。

`libfloral_display_core` 保存与 Android 版本无关的显示身份、拓扑、EDID 和
VSync 策略。Android 12 使用 HWC2.4 前端；Android 13 及以后可以增加
Composer 3 AIDL 前端而无需复制核心逻辑。长期维护的 Android 基线应使用
`android-12.0`、`android-13.0`、`android-14.0` 等对应分支。

### 安全边界

该 HAL 不调用采集 API，不提供 readback 或原始帧调试接口。DRM 受保护缓冲区
不属于当前能力契约，并按失败关闭原则处理。
