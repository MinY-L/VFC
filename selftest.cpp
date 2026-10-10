#include "selftest.h"
#include "mainwindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QStringConverter>
#include <QTextStream>
#include <QTimer>

#include <cstdio>

namespace {

const QString kTestMp4 =
    QStringLiteral("C:/Users/28296/Desktop/Video Format Conversion/test.mp4");
const QString kBrokenMp4 =
    QStringLiteral("C:/Users/28296/Desktop/Video Format Conversion/损坏视频.mp4");
const QString kChineseMp4 =
    QStringLiteral("C:/Users/28296/Desktop/Video Format Conversion/中文 测试 视频.mp4");

bool sameFile(const QString &lhs, const QString &rhs)
{
    return QFileInfo(lhs) == QFileInfo(rhs);
}

bool waitUntilIdle(MainWindow *window, int timeoutMs)
{
    if (!window->isBusy())
        return true;

    QElapsedTimer clock;
    clock.start();

    QEventLoop loop;
    QTimer ticker;
    ticker.setInterval(20);
    QObject::connect(&ticker, &QTimer::timeout, &loop, [&]() {
        if (!window->isBusy() || clock.elapsed() >= timeoutMs)
            loop.quit();
    });
    ticker.start();
    loop.exec();

    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return !window->isBusy();
}

} // namespace

class SelfTest final : public ISelfTest
{
public:
    int run(MainWindow *window) override
    {
        m_window = window;
        log(QStringLiteral("======== VFC 自测开始 ========"));

        testOneValidVideo(QStringLiteral("单独打开 test.mp4"), kTestMp4);
        testBrokenVideo();
        testOneValidVideo(QStringLiteral("单独打开 中文 测试 视频.mp4"), kChineseMp4);
        testRapidSwitch();

        const QString reportPath =
            QCoreApplication::applicationDirPath() + QStringLiteral("/selftest_result.txt");
        QFile file(reportPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out.setEncoding(QStringConverter::Utf8);
            out << (m_failed == 0 ? QStringLiteral("结果：通过\n") : QStringLiteral("结果：失败\n"))
                << QStringLiteral("通过项数=") << m_passed << "\n"
                << QStringLiteral("失败项数=") << m_failed << "\n";
            for (const QString &line : m_lines)
                out << line << "\n";
        }

        log(QStringLiteral("结果已写入：%1").arg(QDir::toNativeSeparators(reportPath)));
        log(QStringLiteral("======== 自测结束：通过 %1 项，失败 %2 项 ========")
                .arg(m_passed)
                .arg(m_failed));
        return m_failed == 0 ? 0 : 1;
    }

private:
    void log(const QString &line)
    {
        m_lines.append(line);
        const QString out = QStringLiteral("[自测] %1").arg(line);
        // 直接写 stderr：Qt Creator「应用程序输出」会实时捕获，无需等自测结束
        fprintf(stderr, "%s\n", qUtf8Printable(out));
        fflush(stderr);
    }

    void pass(const QString &name, const QString &detail = QString())
    {
        ++m_passed;
        log(QStringLiteral("[通过] %1%2")
                .arg(name, detail.isEmpty() ? QString() : QStringLiteral(" | ") + detail));
    }

    void fail(const QString &name, const QString &detail)
    {
        ++m_failed;
        log(QStringLiteral("[失败] %1 | %2").arg(name, detail));
    }

    bool requireExists(const QString &name, const QString &path)
    {
        if (QFileInfo::exists(path))
            return true;
        fail(name, QStringLiteral("找不到测试文件：%1").arg(QDir::toNativeSeparators(path)));
        return false;
    }

    void testOneValidVideo(const QString &name, const QString &path)
    {
        if (!requireExists(name, path))
            return;

        log(QStringLiteral("开始：%1").arg(name));
        m_window->loadVideo(path);
        log(QStringLiteral("正在等待解析与抽帧…"));
        if (!waitUntilIdle(m_window, 15000)) {
            fail(name, QStringLiteral("等待超时，仍在处理"));
            return;
        }

        QStringList errors;
        if (!sameFile(m_window->currentFilePath(), path)) {
            errors << QStringLiteral("当前文件不匹配，实际为：%1")
                          .arg(QDir::toNativeSeparators(m_window->currentFilePath()));
        }
        if (!m_window->lastProbeOk())
            errors << QStringLiteral("读取视频信息未成功");
        if (!m_window->lastThumbOk())
            errors << QStringLiteral("缩略图未成功");

        if (errors.isEmpty())
            pass(name, QDir::toNativeSeparators(path));
        else
            fail(name, errors.join(QStringLiteral("；")));
    }

    void testBrokenVideo()
    {
        const QString name = QStringLiteral("单独打开 损坏视频.mp4");
        if (!requireExists(name, kBrokenMp4))
            return;

        log(QStringLiteral("开始：%1").arg(name));
        m_window->loadVideo(kBrokenMp4);
        log(QStringLiteral("正在等待损坏文件被拒绝…"));
        if (!waitUntilIdle(m_window, 15000)) {
            fail(name, QStringLiteral("等待超时，仍在处理（程序可能卡住）"));
            return;
        }

        QStringList errors;
        if (!sameFile(m_window->currentFilePath(), kBrokenMp4)) {
            errors << QStringLiteral("当前文件不匹配，实际为：%1")
                          .arg(QDir::toNativeSeparators(m_window->currentFilePath()));
        }
        if (m_window->lastProbeOk())
            errors << QStringLiteral("损坏视频不应解析成功");
        if (m_window->lastThumbOk())
            errors << QStringLiteral("损坏视频不应抽出缩略图");

        if (errors.isEmpty())
            pass(name, QStringLiteral("已拒绝损坏文件且未崩溃"));
        else
            fail(name, errors.join(QStringLiteral("；")));
    }

    void testRapidSwitch()
    {
        const QString name = QStringLiteral("快速连续打开 test.mp4 与 中文 测试 视频.mp4");
        if (!requireExists(name, kTestMp4) || !requireExists(name, kChineseMp4))
            return;

        log(QStringLiteral("开始：%1").arg(name));
        m_window->loadVideo(kTestMp4);
        m_window->loadVideo(kChineseMp4);

        QEventLoop delay;
        QTimer::singleShot(40, &delay, &QEventLoop::quit);
        delay.exec();
        m_window->loadVideo(kTestMp4);
        m_window->loadVideo(kChineseMp4);
        log(QStringLiteral("正在等待最终结果稳定…"));

        if (!waitUntilIdle(m_window, 15000)) {
            fail(name, QStringLiteral("等待超时，仍在处理"));
            return;
        }

        QStringList errors;
        if (!sameFile(m_window->currentFilePath(), kChineseMp4)) {
            errors << QStringLiteral("最终文件应为中文测试视频，实际为：%1")
                          .arg(QDir::toNativeSeparators(m_window->currentFilePath()));
        }
        if (!m_window->lastProbeOk())
            errors << QStringLiteral("最后一次读取视频信息未成功");
        if (!m_window->lastThumbOk())
            errors << QStringLiteral("最后一次缩略图未成功");

        if (errors.isEmpty())
            pass(name, QStringLiteral("最终稳定在中文测试视频"));
        else
            fail(name, errors.join(QStringLiteral("；")));
    }

    MainWindow *m_window = nullptr;
    int m_passed = 0;
    int m_failed = 0;
    QStringList m_lines;
};

ISelfTest *createSelfTest()
{
    return new SelfTest;
}
