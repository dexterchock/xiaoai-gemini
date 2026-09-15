# xiaoai-plus toolchain

ARM (armv7) 交叉编译工具链，所有依赖静态链接。

## 前置条件

- 本机已安装并可使用 Docker
- 在项目根目录执行命令

## 交叉编译流程

```bash
make -C toolchain build
make -C toolchain build-sherpa-static
make -C toolchain compile
```

## Docker 运行测试

先完成交叉编译，再执行运行冒烟测试：

```bash
make -C toolchain runtime-smoke-test
```

该测试会：
- 构建 `xiaoai-plus-runtime-test:dev` 镜像，并在容器内通过 `qemu-arm-static` 启动 `build-armv7/xiaoai_plus_speaker`
- 自动生成 `dist/runtime-test-config.ini` 作为测试配置
- 运行 8 秒后发送 `SIGTERM`，要求进程正常退出
- `runtime-smoke-test` 会自动确保 `xiaoai-plus-toolchain:dev` 基础镜像存在

首次使用请先创建真实配置：

```bash
cp config.ini.example config.ini
# 然后填写 realtime 的密钥
```

## Make 目标

| 命令 | 说明 |
|------|------|
| `make build` | 构建 Docker 工具链镜像 |
| `make build-sherpa-static` | 从源码编译 sherpa-onnx + onnxruntime 静态库 |
| `make compile` | 交叉编译 `xiaoai_plus_speaker`（全静态链接） |
| `make runtime-test-image` | 构建运行测试镜像（包含 `qemu-arm-static` 与 armhf `alsa-utils`） |
| `make runtime-smoke-test` | 在 Docker 中执行运行冒烟测试 |

仅依赖设备系统库：libstdc++, libc, libm, libgcc_s。
