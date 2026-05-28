---
name: kugou-api-review
description: 酷狗音乐概念版 API 变更审查清单。防止设备指纹污染、签名盐错误、接口参数硬编码等回归问题。
---

## 何时使用
修改或新增以下任一内容时：
- 酷狗网络请求接口（`SongUrlService`、`CatalogService`、`UserService`、`DeviceRegisterService` 等）
- 设备指纹生成逻辑（`DeviceService`、`DeviceRegisterService`）
- 签名/加密相关代码
- VIP、登录、听歌上报等业务接口

## 审查清单

### [ ] 设备指纹边界检查
- [ ] **全局 `DeviceInfo.mid/uuid` 未被改为 Android decimal mid**（仅 `SongUrlService` 局部需要）
- [ ] `DeviceService::NormalizeDeviceInfo` 未引入新的 mid/uuid 派生公式
- [ ] 歌单/用户歌单/云歌单接口仍使用 dfid 血缘的持久化 mid/uuid

### [ ] 参数派生源检查
- [ ] `appid`、`clientver`、`busi_type` 未在业务代码中硬编码
- [ ] 上述参数通过 `GetKuGouProfile()` 派生（唯一豁免：`KuGouProfile.h/cpp`）
- [ ] 盐选择正确：
  - Android 通用签名：`LnT6xpN3khm36zse0QzvmgTZ3waWdRSA`
  - Song URL key：`185672dd44712f60bb1736df5a377e82`

### [ ] 接口路径检查
- [ ] `/v5/url` 使用 `clientver=11430`
- [ ] `/v6/priv_url` 的 mid 格式正确（38-39 位 Android decimal mid，不是 raw hex）
- [ ] `kgcheckin` 路径的 `clientver` 与 `PROJECT_LOGIC` 一致（11436 或 11440）

### [ ] 响应处理检查
- [ ] 兼容 JSON 与 typed DTO 分离（不长期保留原始 JSON）
- [ ] 分页接口不一次性拉取全部历史
- [ ] 失败时返回稳定错误，不伪造成功

### [ ] 线程安全检查
- [ ] 网络请求不在 UI 线程执行
- [ ] SQLite 操作不在 UI 线程执行
- [ ] 图片解码/播放底层不在 UI 线程执行

## 参考文件
- `docs/PROJECT_LOGIC.zh-CN.md` —— 项目事实源
- `native/core/KuGouProfile.h` —— 参数派生唯一合法硬编码位置
- `native/core/SongUrlService.cpp` —— mid 局部计算示例
- `native/core/DeviceService.cpp` —— 全局设备指纹

## 禁止事项（红线）
1. 不要把酷狗标准版参数当作概念版默认事实
2. 不要把 MakcRe `platform=lite` 当作 BottleMusic 业务身份
3. 不要把 m.kugou.com Cookie-only GET 当作已确认的 VIP 路线
4. 不要把临时 EchoCompatServer HTTP 形态污染最终业务 Interface

## 输出要求
审查完成后，输出：
- **通过 / 需修改 / 需确认**
- 如有问题，给出具体文件和行号引用
