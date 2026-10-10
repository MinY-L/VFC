#ifndef SELFTEST_H
#define SELFTEST_H

class MainWindow;

// 自测接口。只有工程里存在 selftest.cpp 时 CMake 才会定义 VFC_HAS_SELFTEST 并编译进来。
// 程序内置自测入口：VFC.exe --selftest，自动验证"正常文件 / 损坏文件 / 中文空格路径 / 快速连续切换"四类场景，输出 selftest_result.txt 与退出码。
class ISelfTest
{
public:
    virtual ~ISelfTest() = default;
    virtual int run(MainWindow *window) = 0;
};

ISelfTest *createSelfTest();

#endif // SELFTEST_H
