# 硬币识别计数 — Visual Studio C++

本工程独立存放在桌面 `CoinCounter` 文件夹。使用 Visual Studio 2022、C++17、Windows SDK 和系统自带 GDI+；无需 OpenCV、Python 或模型权重。

## 直接运行

双击 `运行示例.cmd`，自动处理所给硬币照片并打开编号结果图。已附编译好的 `bin/Release/CoinCounter.exe`。

## 在 Visual Studio 运行

1. 打开 `CoinCounter.sln`。
2. 选择 `Release | x64`，按 `Ctrl+F5`。
3. 自动读取 `images/sample.jpg`，结果写入 `output`。

编译环境为 VS 2022 的“使用 C++ 的桌面开发”工作负载和 Windows 10/11 SDK，工具集 v143。源码位于 `src/main.cpp`。也可运行 `powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1` 编译。

## 识别结果

所给照片尺寸为 691×572 像素，程序检测到 **10 枚硬币**。已逐个检查圆形标注，与目视计数一致。

| 输出 | 内容 |
| --- | --- |
| `output/04_result.png` | 圆形框、编号、总数 |
| `output/01_gray.png` | 中值滤波灰度图 |
| `output/02_binary.png` | 阈值二值图，亮硬币为白色 |
| `output/02b_filled.png` | 闭运算并填充孔洞后的前景 |
| `output/03_mask.png` | 筛选后保留的硬币区域 |
| `output/objects.csv` | 每枚硬币的边界框、填充面积、质心和像素直径 |
| `output/summary.json` | 总数、图像尺寸、实际阈值等参数 |

CSV 坐标从 0 开始；面积是填充后的前景像素数，直径为轴向边界框宽高的平均值，单位为像素，不是物理尺寸。编号按水平带从上到下、带内从左到右分配。圆形轮廓按轴向边界框绘制，可随照片透视或缩放略呈椭圆。

## 算法

灰度化 → 3×3 中值滤波 → Otsu 自动阈值 × 0.85 → 亮前景二值化 → 7×7 闭运算 → 四邻域背景泛洪填孔 → 八邻域连通域 → 面积及近圆形筛选 → 编号计数。

阈值系数 0.85 是针对当前暗背景、包含较暗硬币的图片所用的启发式设置；同时将阈值下限设为背景估计（灰度的第 20 百分位）加 5，上限 255，避免均匀背景被错误纳入前景。本图实际阈值为 114；这些设置并非通用最优值，可手动覆盖。筛选条件：面积至少为图像面积的 0.003（且不低于 20 像素）、最多为图像面积的 15%；宽高比 0.75～1.33；二阶矩主轴比不超过 1.35；填充面积/边界框面积为 0.65～0.90。距边界不超过 2 像素的区域排除。

## 更换图片和调参

在 VS 项目属性 → 调试 → 命令参数中修改输入图片路径和输出目录。或在当前文件夹的 PowerShell 执行：

```powershell
.\bin\Release\CoinCounter.exe "D:\图片\硬币.jpg" "output_other" --show
.\bin\Release\CoinCounter.exe "images\sample.jpg" "output_manual" --threshold 114 --min-area-ratio 0.003 --show
```

支持 PNG/JPEG/BMP 等 GDI+ 可读格式，中文和空格路径加双引号。无参数时相对于当前工作目录读取 `images/sample.jpg`。`--threshold` 为 0～255；`--min-area-ratio` 大于 0、小于 0.15；省略参数则采用默认值。`--show` 调用系统看图程序。重复运行覆盖输出目录中的同名结果，程序拒绝结果覆盖输入图片。

## 验证与范围

本机已用 VS 2022 / MSVC 编译并运行。执行 `powershell -NoProfile -ExecutionPolicy Bypass -File .\verify.ps1` 可复核样例 10 枚、空白图 0、三枚合成圆形含暗币及内部孔洞为 3、方形/细长杂物排除、错误输入和覆盖保护。合成测试验证处理逻辑，不用于估计真实照片准确率。

当前实现是暗背景中彼此分离的亮色近圆物体分割计数，不区分面额、币种或真伪，也不计算金额。相连、重叠、背景较亮、强反光、透视严重或相似圆形杂物可能导致误检或漏检；只有本张真实照片经过逐枚核对。
