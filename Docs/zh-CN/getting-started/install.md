<a id="installation"></a>

# 安装

1. 把插件放到项目的 `Plugins/DreamNarrative` 目录中（UE 5.8；原版引擎即可）。必需的引擎插件为 EnhancedInput、Niagara 和 GameplayTags。GameplayAbilities 可选，只有 `DreamNarrativeGAS` 会用到它。
2. 打开项目设置 → Plugins → Dream Narrative：
   - `Source Directory`：`.dna` 文件相对于项目根目录的所在目录；默认 `DNarrative`。
   - `Asset Directory`：编译出的脚本资产存放位置；默认 `/Game/Narrative`。
   - 演出设置：按名称查找素材的资产根目录、用于遮罩的后处理材质、`ramp dark` 的默认亮度，以及看门狗的宽限秒数。
3. 在编辑器打开时保存 `.dna` 文件会自动重新构建。如果打开了多个编辑器，只有第一个会写入资产；编译失败则不会写入。编辑器关闭时，运行 `Tools/dnc.ps1 build`。使用 `dnc.ps1 lint` 可以在不写入资产的情况下进行检查。
