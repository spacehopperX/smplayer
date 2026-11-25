// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef STEREO3DCONTROLLER_H
#define STEREO3DCONTROLLER_H

#include "displaymodeswitcher.h"
#include "mvhevcbridge.h"
#include "stereo3dshaders.h"
#include "basegui.h"
#include "mplayerwindow.h"
#include "global.h"
#include "preferences.h"
#include "paths.h"
#include "playerprocess.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QVBoxLayout>
#include <functional>

struct Stereo3DSettings {
    enum InputMode { InputAuto = 0, Input2D = 1, InputSBS = 2, InputTAB = 3 };
    enum OutputMode { OutputAutoFramePacked = 0, OutputFramePacked = 1, OutputMono2D = 2, OutputPassthrough = 3 };

    int inputMode = InputAuto;
    int outputMode = OutputAutoFramePacked;
    bool swapEyes = false;
    bool autoSwitchDisplayMode = true;
    bool restoreModeOnStop = true;
    int preferred3DRefreshHz = 0; // 0 = Auto (prefer 30 Hz)
    bool preferGpuNext = true;
    bool enableMvHevcBridge = true;
    QString ffmpegPath;

    static Stereo3DSettings load() {
        QSettings s(Paths::configPath() + "/stereo3d.ini", QSettings::IniFormat);
        Stereo3DSettings cfg;
        cfg.inputMode = s.value("inputMode", InputAuto).toInt();
        cfg.outputMode = s.value("outputMode", OutputAutoFramePacked).toInt();
        cfg.swapEyes = s.value("swapEyes", false).toBool();
        cfg.autoSwitchDisplayMode = s.value("autoSwitchDisplayMode", true).toBool();
        cfg.restoreModeOnStop = s.value("restoreModeOnStop", true).toBool();
        cfg.preferred3DRefreshHz = s.value("preferred3DRefreshHz", 0).toInt();
        cfg.preferGpuNext = s.value("preferGpuNext", true).toBool();
        cfg.enableMvHevcBridge = s.value("enableMvHevcBridge", true).toBool();
        cfg.ffmpegPath = s.value("ffmpegPath").toString();
        return cfg;
    }

    void save() const {
        QSettings s(Paths::configPath() + "/stereo3d.ini", QSettings::IniFormat);
        s.setValue("inputMode", inputMode);
        s.setValue("outputMode", outputMode);
        s.setValue("swapEyes", swapEyes);
        s.setValue("autoSwitchDisplayMode", autoSwitchDisplayMode);
        s.setValue("restoreModeOnStop", restoreModeOnStop);
        s.setValue("preferred3DRefreshHz", preferred3DRefreshHz);
        s.setValue("preferGpuNext", preferGpuNext);
        s.setValue("enableMvHevcBridge", enableMvHevcBridge);
        s.setValue("ffmpegPath", ffmpegPath);
        s.sync();
    }
};

struct Stereo3DDetection {
    int input = Stereo3DSettings::Input2D;
    bool swapped = false;
    QString reason = QStringLiteral("2D");
};

class Stereo3DControlDialog : public QDialog {
public:
    explicit Stereo3DControlDialog(const Stereo3DSettings &initial,
                                   const QString &screenName,
                                   const QString &statusText,
                                   std::function<void(const Stereo3DSettings &)> onApply,
                                   QWidget *parent = nullptr)
        : QDialog(parent), applyCallback(std::move(onApply)) {
        setWindowTitle(tr("3D Output Settings"));
        setMinimumWidth(540);

        auto *root = new QVBoxLayout(this);
        auto *form = new QFormLayout();

        inputCombo = new QComboBox(this);
        inputCombo->addItem(tr("Auto-detect (filename / resolution / MV-HEVC)"), Stereo3DSettings::InputAuto);
        inputCombo->addItem(tr("2D (Off)"), Stereo3DSettings::Input2D);
        inputCombo->addItem(tr("Side-by-Side (SBS)"), Stereo3DSettings::InputSBS);
        inputCombo->addItem(tr("Top-and-Bottom (TAB / OU)"), Stereo3DSettings::InputTAB);
        setComboByData(inputCombo, initial.inputMode);
        form->addRow(tr("Input format:"), inputCombo);

        outputCombo = new QComboBox(this);
        outputCombo->addItem(tr("Auto (Frame-Packed 1920x2205 when 3D detected)"), Stereo3DSettings::OutputAutoFramePacked);
        outputCombo->addItem(tr("Frame-Packed 1920x2205 (HDMI 3D)"), Stereo3DSettings::OutputFramePacked);
        outputCombo->addItem(tr("2D Fallback (Single Eye)"), Stereo3DSettings::OutputMono2D);
        outputCombo->addItem(tr("Passthrough (No Shader)"), Stereo3DSettings::OutputPassthrough);
        setComboByData(outputCombo, initial.outputMode);
        form->addRow(tr("Output mode:"), outputCombo);

        refreshCombo = new QComboBox(this);
        refreshCombo->addItem(tr("Auto (prefer 30 Hz)"), 0);
        const QList<int> rates = DisplayModeSwitcher::enumFramePackingRefreshRates(screenName);
        for (int hz : rates) {
            refreshCombo->addItem(tr("%1 Hz").arg(hz), hz);
        }
        if (initial.preferred3DRefreshHz > 0 && !rates.contains(initial.preferred3DRefreshHz)) {
            refreshCombo->addItem(tr("%1 Hz (not currently reported)").arg(initial.preferred3DRefreshHz), initial.preferred3DRefreshHz);
        }
        setComboByData(refreshCombo, initial.preferred3DRefreshHz);
        form->addRow(tr("1920x2205 refresh rate:"), refreshCombo);

        auto *ffmpegRow = new QHBoxLayout();
        ffmpegEdit = new QLineEdit(initial.ffmpegPath, this);
        ffmpegEdit->setPlaceholderText(tr("Auto-detect (PATH / app folder)"));
        auto *browseBtn = new QPushButton(tr("Browse..."), this);
        ffmpegRow->addWidget(ffmpegEdit, 1);
        ffmpegRow->addWidget(browseBtn);
        form->addRow(tr("FFmpeg executable:"), ffmpegRow);
        QObject::connect(browseBtn, &QPushButton::clicked, this, [this]() {
            const QString chosen = QFileDialog::getOpenFileName(
                this,
                tr("Select FFmpeg Executable"),
                ffmpegEdit->text(),
                tr("Executables (*.exe);;All Files (*)"));
            if (!chosen.isEmpty()) ffmpegEdit->setText(chosen);
        });

        root->addLayout(form);

        swapCheck = new QCheckBox(tr("Swap Left / Right eyes"), this);
        swapCheck->setChecked(initial.swapEyes);
        root->addWidget(swapCheck);

        mvHevcCheck = new QCheckBox(tr("Auto-decode Apple MV-HEVC (Spatial Video) via real-time FFmpeg bridge (no disk re-encoding)"), this);
        mvHevcCheck->setChecked(initial.enableMvHevcBridge);
        root->addWidget(mvHevcCheck);

        autoSwitchCheck = new QCheckBox(tr("Auto-switch playback display to 1920x2205 for 3D output (temporary; preserves CRU/EDID)"), this);
        autoSwitchCheck->setChecked(initial.autoSwitchDisplayMode);
        root->addWidget(autoSwitchCheck);

        restoreStopCheck = new QCheckBox(tr("Restore saved 2D display mode when playback stops"), this);
        restoreStopCheck->setChecked(initial.restoreModeOnStop);
        root->addWidget(restoreStopCheck);

        gpuNextCheck = new QCheckBox(tr("Use mpv gpu-next video output when VO is default (required for 3D shaders)"), this);
        gpuNextCheck->setChecked(initial.preferGpuNext);
        root->addWidget(gpuNextCheck);

        auto *statusLabel = new QLabel(statusText, this);
        statusLabel->setWordWrap(true);
        statusLabel->setStyleSheet("QLabel { color: gray; padding-top: 6px; }");
        root->addWidget(statusLabel);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
        root->addWidget(buttons);

        QObject::connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this]() {
            if (applyCallback) applyCallback(settings());
        });
        QObject::connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
            if (applyCallback) applyCallback(settings());
            accept();
        });
        QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    Stereo3DSettings settings() const {
        Stereo3DSettings cfg;
        cfg.inputMode = inputCombo->currentData().toInt();
        cfg.outputMode = outputCombo->currentData().toInt();
        cfg.preferred3DRefreshHz = refreshCombo->currentData().toInt();
        cfg.swapEyes = swapCheck->isChecked();
        cfg.autoSwitchDisplayMode = autoSwitchCheck->isChecked();
        cfg.restoreModeOnStop = restoreStopCheck->isChecked();
        cfg.preferGpuNext = gpuNextCheck->isChecked();
        cfg.enableMvHevcBridge = mvHevcCheck->isChecked();
        cfg.ffmpegPath = ffmpegEdit->text().trimmed();
        return cfg;
    }

private:
    static void setComboByData(QComboBox *combo, int value) {
        const int idx = combo->findData(value);
        if (idx >= 0) combo->setCurrentIndex(idx);
    }

    QComboBox *inputCombo;
    QComboBox *outputCombo;
    QComboBox *refreshCombo;
    QLineEdit *ffmpegEdit;
    QCheckBox *swapCheck;
    QCheckBox *mvHevcCheck;
    QCheckBox *autoSwitchCheck;
    QCheckBox *restoreStopCheck;
    QCheckBox *gpuNextCheck;
    std::function<void(const Stereo3DSettings &)> applyCallback;
};

class Stereo3DController {
public:
    Stereo3DController() {
        cfg = Stereo3DSettings::load();
        ensureGpuNextIfConfigured();
    }

    ~Stereo3DController() {
        modeSwitcher.restore();
    }

    const Stereo3DSettings &settings() const { return cfg; }
    bool isFramePackedActive() const { return framePackedActive; }
    void setBridgeNote(const QString &note) { lastBridgeNote = note; }

    void ensureGpuNextIfConfigured() {
        if (!cfg.preferGpuNext || !Global::pref) return;
        const QString vo = Global::pref->vo.trimmed().toLower();
        if (vo.isEmpty() || vo == "direct3d" || vo == "default") {
            Global::pref->vo = "gpu-next";
        }
    }

    bool prepareFramePackingDisplay(BaseGui *gui,
                                    const QString &playbackScreenName,
                                    const std::function<void()> &onDisplayModeChanged) {
        if (!cfg.autoSwitchDisplayMode || playbackScreenName.isEmpty()) return false;
        if (cfg.outputMode != Stereo3DSettings::OutputAutoFramePacked &&
            cfg.outputMode != Stereo3DSettings::OutputFramePacked) {
            return false;
        }
        framePackedActive = true;
        if (modeSwitcher.isSwitched()) {
            syncVideoWindowAspect(gui, true);
            return false;
        }
        lastModeError.clear();
        if (modeSwitcher.switchToFramePacking(playbackScreenName, cfg.preferred3DRefreshHz, &lastModeError)) {
            syncVideoWindowAspect(gui, true);
            if (onDisplayModeChanged) onDisplayModeChanged();
            return true;
        }
        return false;
    }

    static Stereo3DDetection detectInput(const QString &filename, int width, int height) {
        Stereo3DDetection det;
        const QString base = QFileInfo(filename).completeBaseName();

        static const QRegularExpression reSbsSwap(
            QStringLiteral("(^|[^A-Za-z0-9])(H?SBSR|3D[._-]?RL)([^A-Za-z0-9]|$)"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression reSbs(
            QStringLiteral("(^|[^A-Za-z0-9])(H-?SBS|HALF[._-]?SBS|3D[._-]?SBS|SBS|SIDE[._-]?BY[._-]?SIDE|3D[._-]?LR)([^A-Za-z0-9]|$)"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression reTabSwap(
            QStringLiteral("(^|[^A-Za-z0-9])(H?TABR|3D[._-]?BT)([^A-Za-z0-9]|$)"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression reTab(
            QStringLiteral("(^|[^A-Za-z0-9])(H-?TAB|HALF[._-]?TAB|3D[._-]?TAB|TAB|H-?OU|HALF[._-]?OU|3D[._-]?OU|OVER[._-]?UNDER|TOP[._-]?BOTTOM|3D[._-]?TB)([^A-Za-z0-9]|$)"),
            QRegularExpression::CaseInsensitiveOption);

        if (reSbsSwap.match(base).hasMatch()) {
            det.input = Stereo3DSettings::InputSBS;
            det.swapped = true;
            det.reason = QStringLiteral("Filename tag (SBS swapped)");
            return det;
        }
        if (reSbs.match(base).hasMatch()) {
            det.input = Stereo3DSettings::InputSBS;
            det.reason = QStringLiteral("Filename tag (SBS)");
            return det;
        }
        if (reTabSwap.match(base).hasMatch()) {
            det.input = Stereo3DSettings::InputTAB;
            det.swapped = true;
            det.reason = QStringLiteral("Filename tag (TAB/OU swapped)");
            return det;
        }
        if (reTab.match(base).hasMatch()) {
            det.input = Stereo3DSettings::InputTAB;
            det.reason = QStringLiteral("Filename tag (TAB/OU)");
            return det;
        }
        if (width >= 3200 && height >= 720 && width >= height * 3) {
            det.input = Stereo3DSettings::InputSBS;
            det.reason = QString("Resolution heuristic (%1x%2 Full-SBS)").arg(width).arg(height);
            return det;
        }
        if (height >= 2000 && width > 0 && width <= 2048) {
            det.input = Stereo3DSettings::InputTAB;
            det.reason = QString("Resolution heuristic (%1x%2 Full-TAB)").arg(width).arg(height);
            return det;
        }
        return det;
    }

    void applyForActiveMedia(BaseGui *gui,
                             const QString &playbackScreenName,
                             const std::function<void()> &onDisplayModeChanged,
                             bool mvHevcActive = false,
                             const QString &mvHevcSummary = QString()) {
        if (!gui) return;
        Core *core = gui->getCore();
        if (!core) return;

        if (mvHevcActive) {
            lastDetection.input = Stereo3DSettings::InputTAB;
            lastDetection.swapped = false;
            lastDetection.reason = mvHevcSummary.isEmpty()
                ? QStringLiteral("Apple MV-HEVC (FFmpeg real-time bridge -> Full-TAB)")
                : mvHevcSummary;
        } else {
            lastDetection = detectInput(core->mdat.filename, core->mdat.video_width, core->mdat.video_height);
        }

        int effectiveInput = cfg.inputMode;
        bool effectiveSwap = cfg.swapEyes;
        if (effectiveInput == Stereo3DSettings::InputAuto) {
            effectiveInput = lastDetection.input;
            if (lastDetection.swapped) effectiveSwap = !effectiveSwap;
        }

        Stereo3DShaders::ShaderKind shader = Stereo3DShaders::None;
        bool needFramePackingMode = false;

        if (effectiveInput == Stereo3DSettings::InputSBS || effectiveInput == Stereo3DSettings::InputTAB) {
            const bool isTab = (effectiveInput == Stereo3DSettings::InputTAB);
            if (cfg.outputMode == Stereo3DSettings::OutputAutoFramePacked ||
                cfg.outputMode == Stereo3DSettings::OutputFramePacked) {
                needFramePackingMode = true;
                if (isTab) {
                    shader = effectiveSwap ? Stereo3DShaders::FramePackedTABSwap : Stereo3DShaders::FramePackedTAB;
                } else {
                    shader = effectiveSwap ? Stereo3DShaders::FramePackedSBSSwap : Stereo3DShaders::FramePackedSBS;
                }
            } else if (cfg.outputMode == Stereo3DSettings::OutputMono2D) {
                if (isTab) {
                    shader = effectiveSwap ? Stereo3DShaders::MonoTABSwap : Stereo3DShaders::MonoTAB;
                } else {
                    shader = effectiveSwap ? Stereo3DShaders::MonoSBSSwap : Stereo3DShaders::MonoSBS;
                }
            }
        }

        framePackedActive = needFramePackingMode;

        lastModeError.clear();
        if (needFramePackingMode && cfg.autoSwitchDisplayMode && !playbackScreenName.isEmpty()) {
            const bool switchedBefore = modeSwitcher.isSwitched();
            if (modeSwitcher.switchToFramePacking(playbackScreenName, cfg.preferred3DRefreshHz, &lastModeError)) {
                if (!switchedBefore && onDisplayModeChanged) onDisplayModeChanged();
            }
        } else if (!needFramePackingMode && modeSwitcher.isSwitched()) {
            modeSwitcher.restore();
            if (onDisplayModeChanged) onDisplayModeChanged();
        }

        syncVideoWindowAspect(gui, needFramePackingMode);
        applyShaderToProcess(core->playerProcess(), shader, needFramePackingMode);
    }

    void onPlaybackStopped(BaseGui *gui, const std::function<void()> &onDisplayModeChanged) {
        framePackedActive = false;
        if (gui && gui->getCore()) {
            applyShaderToProcess(gui->getCore()->playerProcess(), Stereo3DShaders::None, false);
        }
        syncVideoWindowAspect(gui, false);
        if (cfg.restoreModeOnStop && modeSwitcher.isSwitched()) {
            modeSwitcher.restore();
            if (onDisplayModeChanged) onDisplayModeChanged();
        }
    }

    void restoreDisplayMode(const std::function<void()> &onDisplayModeChanged = nullptr) {
        framePackedActive = false;
        if (modeSwitcher.isSwitched()) {
            modeSwitcher.restore();
            if (onDisplayModeChanged) onDisplayModeChanged();
        }
    }

    void onScreenRemoved(const QString &screenName) {
        modeSwitcher.clearForDisconnected(screenName);
    }

    void showDialog(BaseGui *gui,
                    const QString &playbackScreenName,
                    const std::function<void()> &onDisplayModeChanged,
                    bool mvHevcActive = false,
                    const QString &mvHevcSummary = QString()) {
        if (!gui) return;
        const QString status = buildStatusSummary(playbackScreenName);
        Stereo3DControlDialog dialog(
            cfg,
            playbackScreenName,
            status,
            [this, gui, playbackScreenName, onDisplayModeChanged, mvHevcActive, mvHevcSummary](const Stereo3DSettings &next) {
                cfg = next;
                cfg.save();
                ensureGpuNextIfConfigured();
                if (gui->getCore()->state() != Core::Stopped) {
                    applyForActiveMedia(gui, playbackScreenName, onDisplayModeChanged, mvHevcActive, mvHevcSummary);
                } else if (!cfg.autoSwitchDisplayMode) {
                    restoreDisplayMode(onDisplayModeChanged);
                }
            },
            gui);
        dialog.exec();
    }

private:
    void syncVideoWindowAspect(BaseGui *gui, bool framePacked) {
        if (!gui) return;
        MplayerWindow *mw = gui->findChild<MplayerWindow *>("mplayerwindow");
        if (!mw) return;
        if (framePacked) {
            mw->setAspect(0);
            if (QWidget *vl = mw->videoLayer()) {
                if (vl->geometry() != mw->rect()) vl->setGeometry(mw->rect());
            }
        } else if (gui->getCore() && gui->getCore()->mdat.video_aspect > 0) {
            mw->setAspect(gui->getCore()->mdat.video_aspect);
        }
    }

    void applyShaderToProcess(PlayerProcess *process, Stereo3DShaders::ShaderKind kind, bool framePacked) {
        activeShaderPath = Stereo3DShaders::ensureShader(kind);
        if (!process || !process->isMPV() || !process->isRunning()) return;
        if (activeShaderPath.isEmpty()) {
            process->sendCommand("no-osd set keepaspect yes");
            process->sendCommand("no-osd change-list glsl-shaders clr \"\"");
        } else {
            process->sendCommand(framePacked ? "no-osd set keepaspect no" : "no-osd set keepaspect yes");
            process->sendCommand(QString("no-osd change-list glsl-shaders set \"%1\"").arg(activeShaderPath));
        }
    }

    QString buildStatusSummary(const QString &screenName) const {
        const QString resolvedFfmpeg = MvHevcBridge::findExecutable(cfg.ffmpegPath, "ffmpeg");
        QString s = QString("Detected input: %1\nActive shader: %2\nPlayback display mode: %3\nFFmpeg (MV-HEVC): %4")
            .arg(lastDetection.reason)
            .arg(activeShaderPath.isEmpty() ? QStringLiteral("None") : QFileInfo(activeShaderPath).fileName())
            .arg(DisplayModeSwitcher::currentModeSummary(screenName))
            .arg(resolvedFfmpeg.isEmpty() ? QStringLiteral("Not found") : resolvedFfmpeg);
        if (!lastModeError.isEmpty()) {
            s += QString("\nLast mode-switch note: %1").arg(lastModeError);
        }
        if (!lastBridgeNote.isEmpty()) {
            s += QString("\nMV-HEVC bridge note: %1").arg(lastBridgeNote);
        }
        return s;
    }

    Stereo3DSettings cfg;
    DisplayModeSwitcher modeSwitcher;
    Stereo3DDetection lastDetection;
    QString activeShaderPath;
    QString lastModeError;
    QString lastBridgeNote;
    bool framePackedActive = false;
};

#endif
