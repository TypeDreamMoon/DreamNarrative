<a id="dns7xxx--stage"></a>

# DNS7xxx — 演出

<a id="dns7001"></a>

## DNS7001

切镜头时没有被 mask 盖住。

**解释：**`cut -> Shot` 会让画面瞬间切换。在那一刻，mask 轨上必须有一个条目正处于最强：`distort`、`exposure` 和 `streaks` 在各自范围的中点达到峰值；`fade black / white Duration` 在淡入之后达到峰值；`cut black` 和 `comic` 整段都保持最强。把切点移到峰值上，或者为 2.0s 的切点加一个类似 `1.9s..2.1s distort strength 0.8` 的 mask，或者改用看得见过程的过渡，例如 `blend -> Shot Duration` 或 `whip -> Shot`。有意保留的可见硬切可以使用 `cut -> Shot hard`，它不受此限制。

<a id="dns7002"></a>

## DNS7002

条目越出了演出的长度。

**解释：**在绝对时间的演出中，条目的两端都必须落在 `duration` 之内；在 `base: line` 演出中，两端必须落在 100% 之内。越出的条目永远不会执行。请修正时长或时间戳。条目自带的过程（例如 `blend back 0.4s` 中的 0.4s）可以越过演出结尾；演出会等它走完再交还控制权。

<a id="dns7003"></a>

## DNS7003

改了世界却没有放回去。

**解释：**会留下来的改动需要在同一条轨上、或在 `restore` 中有一个之后执行的逆向操作：拿走镜头控制需要 `blend back`；`hide hud` 需要 `show hud`；`text` 需要 `clear`；`ramp dark` 需要 `light restore`；`key Character on` 需要 `key Character off`。导演在演出完成或被打断时总会做紧急还原，但那是一下子弹回去的。请在轨道上或 `restore` 中声明一个体面的收尾，或者改用会在范围结束时释放自身状态的范围。

<a id="dns7004"></a>

## DNS7004

时间的单位和演出的时间基不合。

**解释：**绝对时间的演出中，条目和过程都用秒，例如 `0.3s`。`base: line` 演出从挂上的那一行台词取得长度，使用百分比，例如 `40%`；它不能声明 `duration`。请使用正确的单位。如果要固定长度的演出，去掉 `base: line`、加上 `duration`，并用 `play` 调用它。

<a id="dns7005"></a>

## DNS7005

范围倒着写了。

**解释：**在 `Start..End` 中，结束不能早于开始。多半是两个数字写反了。

<a id="dns7006"></a>

## DNS7006

这条轨上没有这个动词，或者参数不对。

**解释：**每条轨只接受自己的动词：

| Track | Verbs |
| --- | --- |
| camera | `blend from game / back / ->`, `cut ->`, `whip ->`, `push`, `shake` |
| light | `ramp dark`, `restore`, `key` |
| mask | `distort`, `exposure`, `streaks`, `comic`, `cut black`, `fade black / white` |
| vfx | `spawn`, `stop` |
| ui | `hide hud`, `show hud`, `text`, `clear` |
| anim | `montage` |
| game | `input lock`, `hitstop`, `time`, `damage window` |
| sequence | `play` |
| audio | `sound`, `music` |
| blocking | `move`、`face`、`look`；`back` 仅用于 `restore` |

消息中会给出期望的写法。这个码还覆盖无效的演出属性（只能是 `base` 和 `gameplay`）、缺少 `local (x, y, z)` 的机位或标记（后面可选跟 `rot (pitch, yaw, roll)`），以及无效的 `restore` 条目。还原只接受会释放世界状态的操作：`camera blend back`、`light restore`、`light key Character off`、`ui show hud`、`ui clear`、`vfx stop` 和 `blocking back`。只有 `text` 条目可以用 `#id` 结尾（它的文字翻译时所用的 key）；其他条目上出现 `#id` 会在这里报出。

<a id="dns7007"></a>

## DNS7007

该写时刻的写成了范围，或者反过来。

**解释：**镜头的过渡与切换、`ramp dark`、`restore`、`show hud`、`clear`、`stop`、`montage`、`sequence play` 和 `shake` 都发生在一个时刻，不能使用 `Start..End`。`distort`、`exposure` 和 `streaks` 需要一个范围，才能升到范围中点的峰值再落下来。请按动词选择对应的时间写法。`music` 搭配 `keep` 时同样需要一个时刻，因为它要在演出结束后继续存在；如果音乐只在一个范围内持续，去掉 `keep`。

<a id="dns7008"></a>

## DNS7008

切到了演出没有声明的机位。

**解释：**`cut ->`、`blend ->` 和 `whip ->` 的目标必须在本演出的 `shots` 块里声明过：`Name local (x, y, z) [of Character] [look Character] [fov Degrees]`。请检查拼写和归属；在别的演出里声明的机位在这里看不到。

<a id="dns7009"></a>

## DNS7009

绝对时间的演出没有写长度。

**解释：**以秒计时的演出需要 `duration`，这样导演才知道什么时候演完、对话什么时候可以接下去、看门狗什么时候该介入。请加上类似 `duration 6.0s` 的声明。如果这是跟着台词走的演出，请改用 `base: line`，并用 `@StageName` 挂到台词上。

<a id="dns7010"></a>

## DNS7010

`play` 用在了跟着台词走的演出上。

**解释：**带 `base: line` 的演出没有自己的时长；它的时间由挂上的那一行台词提供。请用 `@StageName` 把它挂到台词上，或者去掉 `base: line`、声明 `duration` 并把时间改成秒，把它转成绝对时间。
