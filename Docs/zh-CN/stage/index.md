<a id="stage-language"></a>

# 演出语言

```dna
stage 灯亮
  duration 5.0s
  shots
    灯室       local (0, 0, 900) of 灯塔 look 灯塔 fov 50
  camera
    0.0s     blend from game 0.5s
    0.5s     blend -> 灯室 1.5s
    2.5s     cut -> 海面
    5.0s     blend back 0.6s
  mask
    2.4s..2.6s   fade white 0.1s      // The cut must coincide with maximum mask strength (DNS7001)
  light
    0.0s     ramp dark 0.5s to 0.3
    2.5s     restore 0.3s
  ui
    0.0s..5.0s   hide hud
  game
    0.0s..5.0s   input lock
    2.5s..2.7s   hitstop 0.2x
```

- 轨道包括 camera、light、mask、vfx、ui、anim、game、sequence、audio 和 blocking。动词列表见诊断参考中的 DNS7006。
- 屏幕上的 `text` 在 `stage.<Stage>.text<n>` 下翻译，按时间顺序从 0 开始编号；`0.5s text 「…」 #id` 则将其命名为 `stage.<Stage>.<id>`，并且 `dnc freeze` 会把编号写进去。
- 默认时间基是秒。带有 `base: line` 的演出会跟随其附加的对话行，使用百分比，并且只能通过 `@StageName` 附加。
- 镜头按角色名定位：该名称是一个 actor 标签，也可以通过导演的 `BindActor` 绑定。`rot (pitch, yaw, roll)` 指定显式朝向；未绑定的锚点使用世界坐标。
- 声明更改后的世界状态将如何恢复，可以稍后在同一轨道上，或在 `restore` 块中（DNS7003）。导演是事务性的：完成、停止、替换、玩家死亡以及离开关卡都会恢复它触碰过的状态。
- Stage 子系统会向最近启动的导演发送对话 `play`、行 `@` 和任务 `play` 请求，或自动将其添加到第一个本地玩家控制器。没有玩家控制器时，演出仍会在账本时钟上运行其时长，故事也仍会等待。
- 有多个导演时，时间和阳光/天光倍率会相乘；任何演出都可以保持 HUD 隐藏；最近获取的摄像机处于活动状态。完成一个演出会保留其他演出的声明，最后一次完成会恢复原始状态。在更改之间，这些状态不会被处理，以便游戏可以修改它们。对于 AHUD 以外的 HUD，请使用世界子系统 `UDreamNarrativeStageArbiter::OnHudVisibility`，它表示所有演出的组合。在导演的完成回调中再次调用 `Play` 会让新请求获得优先权；被取代的外层请求返回 0。
- 由 `montage`、`spawn` 和 `sequence play` 命名的资产在编译时解析。名称会在 Stage Asset Roots 下搜索；完整路径可直接接受。解析后的路径和引用存储在脚本资产中，并包含在打包中。如果查找失败，DNS6001 会发出警告：编辑器播放可能仍然有效，但打包后的游戏很可能缺少该资产。
- `audio` 轨道会发出事件；游戏负责播放音频：
  - `0.5s sound /Game/SFX/打击 [at Character] [volume 0.8]` 发出一个导演 `OnSound` 事件。诸如 `0.0s..3.0s sound 风声` 这样的范围是持续声音；`OnSoundStopped` 会在范围或演出结束时请求游戏停止它。
  - `0.0s music /Game/BGM/回忆 [fade 1.0s]` 使用世界范围的音乐栈。仲裁者的 `OnMusic(Music, Fade)` 会命名当前轨道。较晚的演出会覆盖较早的演出，然后在完成时再次显露它。当最后一个演出结束时，空音乐名称表示返回游戏自身的音乐。带范围的音乐条目仅在其范围内有效。带有 `keep` 的点条目（例如 `0.0s music /Game/BGM/首领 keep`）会在其演出结束后持续存在。一旦没有其他演出覆盖它，它就成为游戏的音乐，仲裁者会停止为其发出 `OnMusic`。`keep` 仅在点条目上有效。
- `blocking` 轨道会将角色移动到 `marks`。标记使用与镜头相同的语法；`look Character` 指定到达时面向谁：

  ```dna
  marks
    门口     local (500, 0, 0) of 灯塔 look 老周
  blocking
    0.0s..2.0s   move 守夜人 -> 门口 run        // Move over a range; a point teleports; navigate uses the navmesh
    2.0s         face 守夜人 -> 老周 over 0.3s  // Turn toward a mark or character
    0.0s..3.0s   look 老周 at 守夜人           // Look at the target until the range ends
  restore
    blocking back                              // Restore moved actors; omit this to leave them at their destinations
  ```

  导演会随时间插值位置和旋转。actor 负责脚部和头部动画：在 C++ 或蓝图中实现 `IDnaStageActor` 以接收 `StageMoveStarted`、`StageMoveFinished` 和 `StageLookAt`。不是标记的目标会作为角色 actor 解析。blocking 位置是演出和时间的纯函数（`SampleDnaStageBlocking`）。启用 **Preview in viewport** 并拖动时间轴，可移动标记了角色名称的 actor；禁用预览或更改演出会恢复它们。`navigate` 移动使用导航，不包含在预览中。
- 镜头震动：摄像机轨道上的 `0.5s shake /Game/镜头/BP_震动 [scale 1.5]` 使用 CameraShakeBase 蓝图，并在演出结束时停止。
- 要从技能播放，请使用 `PlayNarrativeStage` 技能任务。或者，在演出上声明 `gameplay: Tag`；当该事件到达时，GAS 桥会在 avatar 上播放它。
