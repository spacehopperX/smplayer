// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef DISPLAYMODESWITCHER_H
#define DISPLAYMODESWITCHER_H

#include <QList>
#include <QString>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

class DisplayModeSwitcher {
public:
    DisplayModeSwitcher() = default;
    ~DisplayModeSwitcher() { restore(); }

    static QList<int> enumFramePackingRefreshRates(const QString &deviceName) {
        QList<int> rates;
#ifdef Q_OS_WIN
        if (deviceName.isEmpty()) return rates;
        const std::wstring dev = deviceName.toStdWString();
        DEVMODEW dm = {};
        dm.dmSize = sizeof(DEVMODEW);
        for (DWORD idx = 0; EnumDisplaySettingsW(dev.c_str(), idx, &dm); ++idx) {
            if (dm.dmPelsWidth == 1920 && dm.dmPelsHeight == 2205 && dm.dmDisplayFrequency > 0) {
                const int hz = static_cast<int>(dm.dmDisplayFrequency);
                if (!rates.contains(hz)) rates.append(hz);
            }
            dm = {};
            dm.dmSize = sizeof(DEVMODEW);
        }
        std::sort(rates.begin(), rates.end());
#else
        Q_UNUSED(deviceName);
#endif
        return rates;
    }

    static QString currentModeSummary(const QString &deviceName) {
#ifdef Q_OS_WIN
        if (deviceName.isEmpty()) return QStringLiteral("No display selected");
        const std::wstring dev = deviceName.toStdWString();
        DEVMODEW dm = {};
        dm.dmSize = sizeof(DEVMODEW);
        if (EnumDisplaySettingsW(dev.c_str(), ENUM_CURRENT_SETTINGS, &dm)) {
            return QString("%1x%2 @ %3 Hz")
                .arg(dm.dmPelsWidth)
                .arg(dm.dmPelsHeight)
                .arg(dm.dmDisplayFrequency);
        }
#else
        Q_UNUSED(deviceName);
#endif
        return QStringLiteral("Unknown");
    }

    bool switchToFramePacking(const QString &deviceName, int preferredHz = 0, QString *errorOut = nullptr) {
#ifdef Q_OS_WIN
        if (deviceName.isEmpty()) {
            if (errorOut) *errorOut = QStringLiteral("No dedicated playback display is active.");
            return false;
        }
        if (hasSavedMode && savedDevice != deviceName) restore();

        const std::wstring dev = deviceName.toStdWString();
        DEVMODEW currentDm = {};
        currentDm.dmSize = sizeof(DEVMODEW);
        if (!EnumDisplaySettingsW(dev.c_str(), ENUM_CURRENT_SETTINGS, &currentDm)) {
            if (errorOut) *errorOut = QStringLiteral("Failed to query current display mode.");
            return false;
        }

        if (currentDm.dmPelsWidth == 1920 && currentDm.dmPelsHeight == 2205 &&
            (preferredHz <= 0 || static_cast<int>(currentDm.dmDisplayFrequency) == preferredHz)) {
            return true;
        }

        DEVMODEW bestDm = {};
        bool found = false;
        DEVMODEW dm = {};
        dm.dmSize = sizeof(DEVMODEW);
        for (DWORD idx = 0; EnumDisplaySettingsW(dev.c_str(), idx, &dm); ++idx) {
            if (dm.dmPelsWidth == 1920 && dm.dmPelsHeight == 2205) {
                const int hz = static_cast<int>(dm.dmDisplayFrequency);
                if (preferredHz > 0) {
                    if (hz == preferredHz) {
                        bestDm = dm;
                        found = true;
                        break;
                    }
                } else {
                    if (!found) {
                        bestDm = dm;
                        found = true;
                    } else if (hz == 30) {
                        bestDm = dm;
                    } else if (bestDm.dmDisplayFrequency != 30 && hz <= 60 && hz > static_cast<int>(bestDm.dmDisplayFrequency)) {
                        bestDm = dm;
                    }
                }
            }
            dm = {};
            dm.dmSize = sizeof(DEVMODEW);
        }

        if (!found) {
            if (errorOut) {
                *errorOut = preferredHz > 0
                    ? QString("1920x2205 @ %1 Hz is not reported by Windows on %2.").arg(preferredHz).arg(deviceName)
                    : QString("No 1920x2205 mode is reported by Windows on %1.").arg(deviceName);
            }
            return false;
        }

        if (!hasSavedMode) {
            savedMode = currentDm;
            savedDevice = deviceName;
            hasSavedMode = true;
        }

        bestDm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY | DM_BITSPERPEL;
        const LONG res = ChangeDisplaySettingsExW(dev.c_str(), &bestDm, nullptr, CDS_FULLSCREEN, nullptr);
        if (res != DISP_CHANGE_SUCCESSFUL) {
            if (errorOut) *errorOut = QString("ChangeDisplaySettingsExW failed (code %1).").arg(res);
            hasSavedMode = false;
            savedDevice.clear();
            return false;
        }
        return true;
#else
        Q_UNUSED(deviceName);
        Q_UNUSED(preferredHz);
        Q_UNUSED(errorOut);
        return false;
#endif
    }

    void restore() {
#ifdef Q_OS_WIN
        if (!hasSavedMode || savedDevice.isEmpty()) return;
        const std::wstring dev = savedDevice.toStdWString();
        savedMode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY | DM_BITSPERPEL;
        LONG res = ChangeDisplaySettingsExW(dev.c_str(), &savedMode, nullptr, 0, nullptr);
        if (res != DISP_CHANGE_SUCCESSFUL) {
            ChangeDisplaySettingsExW(dev.c_str(), nullptr, nullptr, 0, nullptr);
        }
        hasSavedMode = false;
        savedDevice.clear();
#endif
    }

    void clearForDisconnected(const QString &deviceName) {
        if (hasSavedMode && savedDevice == deviceName) {
            hasSavedMode = false;
            savedDevice.clear();
        }
    }

    bool isSwitched() const { return hasSavedMode; }
    QString switchedDevice() const { return savedDevice; }

private:
#ifdef Q_OS_WIN
    DEVMODEW savedMode = {};
#endif
    QString savedDevice;
    bool hasSavedMode = false;
};

#endif
