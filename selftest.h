#ifndef SELFTEST_H
#define SELFTEST_H

class MainWindow;

// 自测接口。只有工程里存在 selftest.cpp 时 CMake 才会定义 VFC_HAS_SELFTEST 并编译进来。
// 入口：VFC.exe --selftest。素材一律用 ffmpeg 生成到临时目录，测完删除。
class ISelfTest
{
public:
    virtual ~ISelfTest() = default;
    virtual int run(MainWindow *window) = 0;
};

ISelfTest *createSelfTest();

#endif // SELFTEST_H
