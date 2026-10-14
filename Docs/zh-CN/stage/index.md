---
title: 演出语言
description: 轨道、时间基、机位、恢复。
---

```dna
stage 灯亮
  duration 5.0s
  shots
    灯室       local (0, 0, 900) of 灯塔 look 灯塔 fov 50
    海面       local (2500, 0, 300) of 灯塔 look 灯塔 fov 40
  camera
    0.0s     blend from game 0.5s
    0.5s     blend -> 灯室 1.5s
    2.5s     cut -> 海面
    5.0s     blend back 0.6s
  mask
    2.4s..2.6s   fade white 0.1s
  light
    0.0s     ramp dark 0.5s to 0.3
    2.5s     restore 0.3s
  ui
    0.0s..5.0s   hide hud
  game
    0.0s..5.0s   input lock
  restore
    ui       show hud
```

| 轨道 | 动词 |
|---|---|
| camera | `blend from game 时长` · `blend back 时长` · `cut -> 机位 [hard]` · `blend -> 机位 时长` · `whip -> 机位` · `push 8cm over 时长` |
| light | `ramp dark 时长 [to 比例]` · `restore 时长` · `key 角色 on/off` |
| mask | `distort strength n` · `exposure peak +n ev` · `streaks` · `comic` · `cut black` · `fade black 时长` · `fade white 时长` |
| vfx | `spawn /Game/路径 [at 角色] [persist]` · `stop` |
| ui | `hide hud` · `show hud` · `text 「…」 [style 名]` · `clear` |
| anim | `montage 名 [actor 角色] [warp 目标 角色]` |
| game | `input lock` · `hitstop 0.2x` · `time 0.5x` · `damage window` |
| sequence | `play /Game/关卡序列` |

- **时间基**:默认秒;`base: line` 的演出跟着挂上的台词走,时间写百分比,只能 `@演出名` 挂在台词上。
- **机位**:`local (x, y, z)` 在 `of 角色` 的本地空间(缺省是 look 的目标,再缺省是演出自己的 actor);`look 角色` 看着它,
  或 `rot (pitch, yaw, roll)` 给出朝向;没有 actor 应答的锚点就是世界坐标。角色名就是 actor 的 tag,或用导演的 `BindActor` 绑。
- **切镜头**必须落在遮罩最强的那一刻(DNS7001);过场里本来就该看见的硬切写 `hard`。
- **放回去**:改了世界的东西(拿走镜头、藏 HUD、文字、调暗灯、补光)要在轨道后面或 `restore` 块里写怎么放回(DNS7003)。
