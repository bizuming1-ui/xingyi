# ESP32-S3 按键计数开发中

`include/ButtonCounter.h` 是已单独测试的按键去抖与长按计数核心，不是完整固件，不能直接烧录使用。按键连接 GPIO 4 和 GND，最终固件将使用内部上拉。按下稳定 25 ms 后加 1；保持 500 ms 后每 100 ms 加 1，释放期间不会继续重复计数。网络通信、设备配对、断网队列、确认同步握手和网页集成尚未实现；请勿将这个文件作为完整生产代码使用。

主机环境测试：`g++ -std=c++17 -Wall -Wextra -Werror tests/button_counter.cpp -o /tmp/xingyi-button-test && /tmp/xingyi-button-test`。实际开发板接线、长按、断网与断电行为尚未验证。
