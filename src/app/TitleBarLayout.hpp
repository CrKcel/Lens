#pragma once

namespace lens {

// 自绘标题栏的窗口按钮方位：macOS 在左上，Windows 在右上，
// Linux 跟随桌面环境的窗口装饰按钮布局设定，无法识别时回退右侧
bool titleBarButtonsOnLeft();

} // namespace lens
