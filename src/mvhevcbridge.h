// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef MVHEVCBRIDGE_H
#define MVHEVCBRIDGE_H

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QProcess>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

struct MvHevcProbe {
    bool isSpatial = false;
    int leftView = 0;
    int rightView = 1;
    bool hasSubtitles = false;
    double durationSec = 0.0;
    QString summary;
};

class MvHevcBridge {
public:
    MvHevcBridge() = default;
    ~MvHevcBridge() { stop(); }

    static QString findExecutable(const QString &configuredPath, const QString &name) {
        const QString trimmed = configuredPath.trimmed();
        if (!trimmed.isEmpty() && QFileInfo::exists(trimmed) && QFileInfo(trimmed).isFile()) {
            return QFileInfo(trimmed).absoluteFilePath();
        }
        const QString ext =
#ifdef Q_OS_WIN
            QStringLiteral(".exe");
#else
            QString();
#endif
        const QString baseName = name.endsWith(ext, Qt::CaseInsensitive) ? name : (name + ext);
        const QString appDir = QCoreApplication::applicationDirPath();
        for (const QString &dir : QStringList{appDir, appDir + "/ffmpeg", appDir + "/bin"}) {
            const QString candidate = QDir(dir).filePath(baseName);
            if (QFileInfo::exists(candidate)) return candidate;
        }
        return QStandardPaths::findExecutable(name);
    }

    static bool isCandidateFile(const QString &filePath) {
        const QFileInfo fi(filePath);
        if (!fi.exists() || !fi.isFile()) return false;
        const QString ext = fi.suffix().toLower();
        return ext == "mp4" || ext == "mov" || ext == "m4v" || ext == "mkv";
    }

    static MvHevcProbe probe(const QString &filePath, const QString &configuredFfmpeg) {
        MvHevcProbe res;
        if (!isCandidateFile(filePath)) return res;

        const QString ffprobe = findExecutable(configuredFfmpeg, "ffprobe");
        if (ffprobe.isEmpty()) return res;

        QProcess p;
        p.start(ffprobe, QStringList{
            "-v", "error",
            "-show_entries", "stream=codec_type,codec_name:stream_side_data=:format=duration",
            "-of", "default=nw=1",
            filePath
        });
        if (!p.waitForFinished(4000) || p.exitCode() != 0) return res;

        const QString out = QString::fromUtf8(p.readAllStandardOutput());
        QStringList viewIds;
        QStringList viewPos;

        for (const QString &rawLine : out.split('\n')) {
            const QString line = rawLine.trimmed();
            if (line == "codec_type=subtitle") {
                res.hasSubtitles = true;
            } else if (line.startsWith("view_ids_available=")) {
                viewIds = line.mid(QStringLiteral("view_ids_available=").size()).split(',', Qt::SkipEmptyParts);
            } else if (line.startsWith("view_pos_available=")) {
                viewPos = line.mid(QStringLiteral("view_pos_available=").size()).split(',', Qt::SkipEmptyParts);
            } else if (line.startsWith("duration=") && res.durationSec <= 0.0) {
                bool ok = false;
                const double dur = line.mid(QStringLiteral("duration=").size()).trimmed().toDouble(&ok);
                if (ok && dur > 0.0) res.durationSec = dur;
            }
        }

        if (viewIds.size() >= 2) {
            res.isSpatial = true;
            res.leftView = viewIds[0].toInt();
            res.rightView = viewIds[1].toInt();
            if (viewPos.size() >= 2) {
                for (int i = 0; i < viewIds.size() && i < viewPos.size(); ++i) {
                    const QString pos = viewPos[i].trimmed().toLower();
                    if (pos == "1" || pos == "left") res.leftView = viewIds[i].toInt();
                    else if (pos == "2" || pos == "right") res.rightView = viewIds[i].toInt();
                }
            }
            res.summary = QString("Apple MV-HEVC Spatial (L=%1, R=%2 -> Full-TAB)")
                .arg(res.leftView).arg(res.rightView);
        }
        return res;
    }

    bool start(const QString &sourceFile,
               const MvHevcProbe &info,
               const QString &configuredFfmpeg,
               bool burnStereoPgs = true,
               int pgsDepthPx = 8,
               bool swapEyes = false,
               QString *errorOut = nullptr,
               std::function<void(const QString &)> onBridgeNote = nullptr,
               double startSec = 0.0) {
        stop();

        const QString ffmpeg = findExecutable(configuredFfmpeg, "ffmpeg");
        if (ffmpeg.isEmpty()) {
            if (errorOut) *errorOut = QStringLiteral("FFmpeg executable not found (required for MV-HEVC 3D bridge).");
            return false;
        }

        const quint32 pipeId = QRandomGenerator::global()->generate();
#ifdef Q_OS_WIN
        const QString pipeName = QString("\\\\.\\pipe\\smplayer_mvhevc_%1").arg(pipeId);
        const std::wstring wname = pipeName.toStdWString();
        pipeHandle = CreateNamedPipeW(
            wname.c_str(),
            PIPE_ACCESS_INBOUND,
            PIPE_TYPE_BYTE | PIPE_WAIT,
            1,
            8 * 1024 * 1024,
            8 * 1024 * 1024,
            0,
            nullptr);
        const QString outTarget = pipeName;
        activeUrl = pipeName;
#else
        const QString outTarget = QString("tcp://127.0.0.1:%1?listen=1").arg(10000 + (pipeId % 50000));
        activeUrl = QString("tcp://127.0.0.1:%1").arg(10000 + (pipeId % 50000));
#endif

        const bool overlayPgs = (burnStereoPgs && info.hasSubtitles);
        const int halfShift = (swapEyes ? -pgsDepthPx : pgsDepthPx) / 2;

        QString filter;
        if (overlayPgs) {
            filter = QString(
                "[0:s:0]split=2[subL][subR];"
                "[0:v:view:%1][subL]overlay=x=%3:y=0:shortest=0[L];"
                "[0:v:view:%2][subR]overlay=x=%4:y=0:shortest=0[R];"
                "[L][R]vstack=inputs=2[v]")
                .arg(info.leftView).arg(info.rightView)
                .arg(halfShift).arg(-halfShift);
        } else {
            filter = QString("[0:v:view:%1][0:v:view:%2]vstack=inputs=2[v]")
                .arg(info.leftView).arg(info.rightView);
        }

        QStringList args;
        args << "-hide_banner" << "-loglevel" << "error" << "-nostdin";
        if (startSec > 0.0) {
            args << "-ss" << QString::number(startSec, 'f', 3);
        }
        args << "-i" << sourceFile
             << "-filter_complex" << filter
             << "-map" << "[v]"
             << "-map" << "0:a:0?"
             << "-c:v" << "rawvideo" << "-pix_fmt" << "yuv420p"
             << "-c:a" << "pcm_s16le"
             << "-f" << "nut"
             << "-y" << outTarget;

        auto proc = std::make_unique<QProcess>();
        stderrLog.clear();
        noteCallback = std::move(onBridgeNote);
        intentionalStop = false;

        QObject::connect(proc.get(), &QProcess::readyReadStandardError, [this, p = proc.get()]() {
            const QString chunk = QString::fromUtf8(p->readAllStandardError()).trimmed();
            if (!chunk.isEmpty()) {
                if (!stderrLog.isEmpty()) stderrLog += '\n';
                stderrLog += QString("[FFmpeg MV-HEVC] %1").arg(chunk);
                if (stderrLog.size() > 1200) stderrLog = stderrLog.right(1200);
                if (noteCallback && !intentionalStop) noteCallback(stderrLog);
            }
        });
        QObject::connect(proc.get(), QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                         [this, p = proc.get()](int exitCode, QProcess::ExitStatus status) {
            if (intentionalStop) return;
            const QString tail = QString::fromUtf8(p->readAllStandardError()).trimmed();
            if (!tail.isEmpty()) {
                if (!stderrLog.isEmpty()) stderrLog += '\n';
                stderrLog += QString("[FFmpeg MV-HEVC] %1").arg(tail);
            }
            if ((exitCode != 0 || status != QProcess::NormalExit) && noteCallback) {
                const QString msg = stderrLog.isEmpty()
                    ? QString("FFmpeg MV-HEVC exited early (code %1)").arg(exitCode)
                    : QString("FFmpeg MV-HEVC exited (%1): %2").arg(exitCode).arg(stderrLog);
                noteCallback(msg);
            }
        });

        proc->start(ffmpeg, args);
        if (!proc->waitForStarted(3000)) {
            if (errorOut) *errorOut = QString("Failed to launch FFmpeg MV-HEVC bridge: %1").arg(proc->errorString());
            stop();
            return false;
        }

        activeProc = std::move(proc);
        activeSummary = overlayPgs
            ? (info.summary + QStringLiteral(" + 3D PGS subs"))
            : info.summary;
        activeProbe = info;
        activeSource = sourceFile;
        activeSeekOffsetSec = startSec;
        return true;
    }

    void stop() {
        intentionalStop = true;
        if (activeProc) {
            if (activeProc->state() != QProcess::NotRunning) {
                activeProc->terminate();
                if (!activeProc->waitForFinished(700)) {
                    activeProc->kill();
                    activeProc->waitForFinished(500);
                }
            }
            activeProc.reset();
        }
#ifdef Q_OS_WIN
        if (pipeHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(pipeHandle);
            pipeHandle = INVALID_HANDLE_VALUE;
        }
#endif
        activeUrl.clear();
        activeSummary.clear();
        activeProbe = MvHevcProbe();
        activeSource.clear();
        activeSeekOffsetSec = 0.0;
    }

    bool isActive() const { return !activeUrl.isEmpty(); }
    bool isActiveUrl(const QString &url) const { return !activeUrl.isEmpty() && url == activeUrl; }
    QString url() const { return activeUrl; }
    QString summary() const { return activeSummary; }
    double durationSec() const { return activeProbe.durationSec; }
    double seekOffsetSec() const { return activeSeekOffsetSec; }
    const MvHevcProbe &probeInfo() const { return activeProbe; }
    QString sourceFile() const { return activeSource; }

private:
    std::unique_ptr<QProcess> activeProc;
#ifdef Q_OS_WIN
    HANDLE pipeHandle = INVALID_HANDLE_VALUE;
#endif
    QString activeUrl;
    QString activeSummary;
    MvHevcProbe activeProbe;
    QString activeSource;
    double activeSeekOffsetSec = 0.0;
    QString stderrLog;
    std::function<void(const QString &)> noteCallback;
    bool intentionalStop = false;
};

#endif
