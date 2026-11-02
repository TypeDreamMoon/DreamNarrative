---
title: "DNS7xxx · 演出"
description: "时序、遮罩、恢复、单位与时间基"
---

<a id="dns7001"></a>

## DNS7001

切镜头时没有遮罩盖住。

`cut -> 机位` 让画面一下子跳到另一个机位,观众会看到这一跳。演出的规矩是:跳的那一刻,mask 轨上必须有一个条目正处在最强——`distort`、`exposure`、`streaks` 在它范围的正中间最强,`fade black / white 时长` 淡进去之后就是最强,`cut black`、`comic` 整段都是最强。把切点挪到遮罩的峰值上,或者给切点配一个遮罩(比如 `1.9s..2.1s distort strength 0.8` 盖住 2.0s 的切),或者改用 `blend -> 机位 时长` / `whip -> 机位` 这种看得见过程的移动。过场里镜头之间本来就该看得见的硬切写 `cut -> 机位 hard`,这条就不查它。

<a id="dns7002"></a>

## DNS7002

条目越出了演出的长度。

绝对时间的演出写了 `duration`,条目的时间(范围的两头都算)不能比它晚;跟着台词的演出(`base: line`)以整行为 100%,条目不能超过 100%。越出去的条目永远轮不到,多半是长度写短了或者时间多打了一位。条目自带的过程(`blend back 0.4s` 的那 0.4 秒)可以越过结尾,演出会等它走完再交还。

<a id="dns7003"></a>

## DNS7003

改了世界却没有放回去。

演出在某一刻改了会留下来的东西,之后既没有在同一条轨上改回来,`restore` 块里也没有写怎么放回去:拿走了镜头(`blend from game`、`cut`、`blend ->`、`whip`)却没有 `blend back`;`hide hud` 之后没有 `show hud`;`text` 之后没有 `clear`;`ramp dark` 之后没有 `light restore`;`key 角色 on` 之后没有 `key 角色 off`。导演在演出结束、被打断时都会兜底把世界放回原样,但那是一下子弹回去的;这条要你写出体面的收尾。写在轨道后面、写进 `restore`,或者把这个条目改成范围(范围结束时自己就放回去了)。

<a id="dns7004"></a>

## DNS7004

时间的单位和演出的时间基不合。

演出有两种时间基。默认是绝对时间,条目和过程都用秒(`0.3s`);写了 `base: line` 的演出跟着它挂上的那一行台词走,长短随那一行而定,所以条目和过程都用这一行的百分比(`40%`),也不能写 `duration`。把单位换成这个演出用的那一种;如果其实想要固定的秒数,就去掉 `base: line`、写上 `duration`,用 `play` 播它。

<a id="dns7005"></a>

## DNS7005

范围倒着写了。

`起..止` 的范围,止不能比起早。多半是两个数写反了。

<a id="dns7006"></a>

## DNS7006

这条轨上没有这个动词,或者参数不对。

每条轨只认自己的动词:camera 有 `blend from game / back / ->`、`cut ->`、`whip ->`、`push`;light 有 `ramp dark`、`restore`、`key`;mask 有 `distort`、`exposure`、`streaks`、`comic`、`cut black`、`fade black / white`;vfx 有 `spawn`、`stop`;ui 有 `hide hud`、`show hud`、`text`、`clear`;anim 有 `montage`;game 有 `input lock`、`hitstop`、`time`、`damage window`;sequence 有 `play`。消息里给出了这个动词该怎么写。同一个码也报这几种情况:演出属性只有 `base` 和 `gameplay`;机位要写 `local (x, y, z)`,可以再写 `rot (pitch, yaw, roll)`;`restore` 里只能放把世界放回去的条目(`camera blend back`、`light restore`、`light key 角色 off`、`ui show hud`、`ui clear`、`vfx stop`)。只有 `text` 条目能在行尾写 `#id`(它的文字翻译时用的 key),别的条目写了也报这个码。

<a id="dns7007"></a>

## DNS7007

该写范围的写成了时刻,或者反过来。

有的动词发生在一个时刻:镜头的移动和切换、`ramp dark` 与 `restore`、`show hud`、`clear`、`stop`、`montage`、`sequence play`,它们不能写成 `起..止`。有的动词是一段起落:`distort`、`exposure`、`streaks` 从范围开头升到正中最强再落下去,只写一个时刻就没有起落可言。按动词改成一个时刻或一个范围。

<a id="dns7008"></a>

## DNS7008

切到了演出没有声明的机位。

`cut ->`、`blend ->`、`whip ->` 后面的名字要在这个演出的 `shots` 块里声明过:`名 local (x, y, z) [of 角色] [look 角色] [fov 度数]`。检查是不是名字拼错了,或者机位写在了别的演出里——机位只属于声明它的演出。

<a id="dns7009"></a>

## DNS7009

绝对时间的演出没有写长度。

按秒走的演出要有 `duration`,导演才知道什么时候演完、对话什么时候接着说、看门狗什么时候该介入。加上 `duration 6.0s` 这样的一行;如果这个演出其实是跟着台词走的,写 `base: line`,再用 `@演出名` 挂到台词上。

<a id="dns7010"></a>

## DNS7010

`play` 了一个跟着台词走的演出。

写了 `base: line` 的演出没有自己的长度,它的时间是挂上的那一行台词给的,所以只能用 `@演出名` 挂在台词行上,不能单独 `play`。把它挂到台词上;如果要单独播,就把它改成绝对时间(去掉 `base: line`,写上 `duration`,时间改成秒)。
