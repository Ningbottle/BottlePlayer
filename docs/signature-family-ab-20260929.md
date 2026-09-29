# 签名族 A/B（3116 重签）— 2026-09-29 实测结论

目的只有一个：把「签名族错配」与「权益未下发」这两件事分开判，不再让后者
的不可修掩盖前者的可修。

## 仪器与前置条件

- 触发方式：`EchoSignatureFamilyProbeRunner`（debug-only
  `/diagnostics/signature-family` 路由的独立 runner），它用同一份会话、同一
  台设备分别以两族签名各打一次 vip 与 playlist 探针，只输出脱敏摘要
  （指纹与状态码，不含 token、账号 ID、签名 URL）。
- 数据库：使用生产库 `%APPDATA%\com.bottlemusic.app\bottlemusic.db` 的**副本**
  （临时目录），避免就地迁移触碰真实数据。探针本身不回写会话，实测
  `session_fp_baseline == session_fp == session_fp_after == 0ffcd17b`、
  `device_fp` 稳定、`interfered=false`、`interference=[]`，与代码注释一致。
- 会话身份：`token_len=64`、`userid_len=10`、`dfid_len=24`、`mid_len=39`。
- 两轮顺序：`sigfam-20260929T121052Z`（forward，Standard 先）与
  `sigfam-20260929T121053Z`（reverse，Concept 先），各 4 探针、
  `usable_for_selection=true`、总耗时 535ms / 537ms（预算 45000ms）。
  原始 JSON 与红绿控制台记录留在本机
  `docs/validation/signature-family-ab-20260929/`：本仓库是公开仓库，取证原件
  （会话/设备指纹、长度、上游码全量响应）不入库，见 `.gitignore`。本文只保留
  结论与判定所需的摘要。

## 结果矩阵

| 族 | appid | clientver | vip 探针 | playlist 探针 | 上游码 |
|----|-------|-----------|----------|---------------|--------|
| Standard | 1005 | 20489 | business_rejection | business_rejection | `upstream_status=0`, `upstream_error_code=20017` |
| Concept | 3116 | 11440 | success | success | `upstream_status=0→1`, `upstream_error_code=0` |

Concept 侧的成功不是「解析得动」这种弱判定：`http_status=200`、
`json_parseable`、`normalized_status=1`、`normalized_authoritative=true`、
`response_shape_valid=true`、`response_contract_valid=true`、
`success_payload_valid=true` 全部为真。Standard 侧
`business_rejection_valid=true`，即它是一次结构完整的业务拒绝，不是超时或
格式问题。正反两轮结论相同，排除顺序与瞬时因素。

## 判定

1. **签名族错配成立（可修，已修）**。同一份会话凭据，Standard 族被上游完整
   拒绝，Concept 族被上游接受。生产代码此前的 v6 恰好用 Standard 族签
   Concept 铸造的会话，故 `080f977` 把 v6 的 profile 改为跟随
   `kProjectEdition`；`c6d5473` 的契约测试钉住「v6 URL 必须带 appid=3116」
   以防回退。
2. **历史错误码口径需要修正**：本机日志里 09-15 起的 v6 全败记为 `20018`，
   今日同族拒绝实测为 `20017`。两者同属「会话族与签名族不匹配 → 上游业务拒
   绝」这一类，但**码位不同**；后续排查不要只按 20018 匹配，否则会把这类拒
   绝当成另一件事。
3. **权益侧没有因为本实验而改变**。同一 Concept 成功探针给出的权益字段是
   `account_has_rights=false`、`account_has_rights_state="none"`（
   `busi_vip_count=2`、`busi_vip_kind="array"`），与本机会话三个月来
   `vip_token` 长度恒为 0 的观测一致。
4. **本仪器不能用来断言「完整音质 vs 试听」**。探针是脱敏仪器，不返回最终
   URL，也不产出 `delivery`/`is_preview` 判定。因此本轮只证明「族对了」，
   至于族对了以后回的是完整包还是 60s 试听包，仍需一次真实取链的 URL 分类
   才能定（见下）。把本轮读成「会员音质已恢复」是过度解读，明确禁止。

## 仍缺的验收（不阻塞本记录）

1. 一次走生产 `Resolve` 路径的真实 v6 取链，对最终 URL 做
   preview/full 分类（`/yp/p_` 形态或 `delivery`/`is_preview` 判定），
   确认修族之后落在哪一侧。
2. 候选应用冷启动 → 退出再登录 → 再次冷启动三轮（需要用户扫码，属破坏性
   操作，本轮不执行），每轮重跑两序探针。
3. 以上并入 7c 在线验收；在此之前不合 main。

## 明确不做

不领取扫码登录；不批量改 uuid；不伪造 is_vip；不把「族修好」与「权益下发」
合并成一条结论。

## 关联

- RED：`c6d5473`（Debug CTest 20/21，唯一失败即本断言）
- GREEN：`080f977`（Debug CTest 21/21、Release CTest 20/20）
- 上一轮双序反向 A/B：`docs/signature-family-experiment-2026-09-15.md`（本机留存，
  不入库；正文中对它的引用指的是本机文件）
- 原始证据与 RED/GREEN 控制台记录：`docs/validation/signature-family-ab-20260929/`
  （本机留存，不入库）
