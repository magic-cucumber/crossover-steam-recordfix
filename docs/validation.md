# 验证记录

## 早期代理版

环境：Apple Silicon、CrossOver 26.3、D3DMetal 4.0b1、《怪物猎人：世界》DX12。

- 系统 DXGI 代理 + COM 虚表适配消除了 Overlay 的内存保护错误。
- 仅包装虚表仍不足以录制：NV12 render-target 分配触发 Metal `invalid pixelFormat (0)`。
- 对该分配返回 E_INVALIDARG 后，Steam 日志显示 `no renderable NV12 surface, leaving the YUV conversion to the CPU`。
- 关闭硬件编码后，捕获方法成为 `Game D3D12 RGB + libx264 high (4 threads)`。
- 完整解码过约42秒视频：960×600、约60fps、H.264；AAC双声道48kHz、峰值 -22.8dB；画面确认为游戏主菜单。
- 源视频2508个包的DTS严格递增，容器重启后录制仍成功。

## MinHook 重构版

- 官方 MinHook v1.3.4 固定源码，Windows x64 Zig 交叉编译成功。
- Wine 独立测试成功：显式初始化、三个 DXGI 导出钩子启用、创建 factory、检查适配方法为已提交且可修改保护的 PE 代码页、Release 正常返回。
- 原游戏插件加载器会在首次 factory 创建之后加载 DLL，已实测不适合本补丁；最终实现改为独立 YAML preload 启动器，挂起目标、加载 DLL、显式初始化后再恢复主线程。
- Steam 还要求 factory 创建方法属于 dxgi.dll：最终实现通过 MinHook 复用原 DXGI 的 PE 方法入口，并在适配对象中保留真正的原方法，不依赖修改系统 DLL。
- 2026-11-04 的 MHW 实测：原厂系统 DXGI 哈希保持不变，未部署本项目的 nativePC 插件；仅通过 examples/mhw.yaml 启动，Steam 成功后台录制。
- 最终捕获方法为 Game D3D12 RGB + libx264（4线程）；抽取48.01秒完整片段，H.264 960×600约60fps，无解码错误，画面确认为游戏标题页；AAC 音频峰值 -22.8dB，非静音。
- 通用启动器测试覆盖：主函数前 MinHook 初始化、Unicode、空参数、引号、末尾反斜杠、工作目录、环境、相对路径、未知/重复字段、多文档/错误 YAML、初始化导出缺失时终止挂起子进程。
- 适配限定于 Wine 环境；原生 Windows 不修改 COM 虚表。Windows CI 验证 MSVC/MASM 构建、MinHook 导出初始化、YAML 配置和挂起子进程注入。D3DMetal 捕获路径由上述 CrossOver MHW 实测覆盖。

## 无控制台启动器

- Zig 构建的 PE 子系统验证为 Windows GUI（2），子进程测试确认 `GetConsoleWindow()` 为空。
- CrossOver Steam-2 通过完整参数、注入、配置错误与失败清理回归。
- 真实错误弹窗测试：启动缺少参数的 launcher，检测其 MessageBox，自动关闭并验证退出码为2。
- 自动化失败用例设置 `RECORDFIX_NO_ERROR_DIALOG=1`，日常 Steam 启动默认启用致命错误弹窗。

所有结果都只覆盖列出的环境。日志、游戏文件、用户账号和原厂二进制不纳入开源仓库。
