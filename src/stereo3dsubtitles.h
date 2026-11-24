// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef STEREO3DSUBTITLES_H
#define STEREO3DSUBTITLES_H

#include "paths.h"
#include "playerprocess.h"
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QString>
#include <QTimer>

class Stereo3DSubtitles {
public:
    static QString ensureScript() {
        const QString dirPath = Paths::configPath() + "/scripts";
        QDir().mkpath(dirPath);
        const QString fullPath = QDir::cleanPath(dirPath + "/stereo_subtitles.lua");

        static const char *kLua =
            "local mp = require 'mp'\n"
            "\n"
            "local overlay = mp.create_osd_overlay('ass-events')\n"
            "overlay.res_x = 1920\n"
            "overlay.res_y = 2205\n"
            "\n"
            "local state = {\n"
            "    enabled = false,\n"
            "    depth = 8,\n"
            "    font_size = 52,\n"
            "    margin_y = 54,\n"
            "    swap_eyes = false,\n"
            "    saved_vis = nil\n"
            "}\n"
            "\n"
            "local function escape_ass(text)\n"
            "    if not text then return '' end\n"
            "    text = text:gsub('\\\\', '\\\\\\\\')\n"
            "    text = text:gsub('{', '\\\\{')\n"
            "    text = text:gsub('}', '\\\\}')\n"
            "    text = text:gsub('\\r\\n', '\\\\N')\n"
            "    text = text:gsub('\\n', '\\\\N')\n"
            "    text = text:gsub('\\r', '\\\\N')\n"
            "    return text\n"
            "end\n"
            "\n"
            "local function render()\n"
            "    if not state.enabled then\n"
            "        overlay:remove()\n"
            "        return\n"
            "    end\n"
            "    local raw = mp.get_property('sub-text') or ''\n"
            "    if raw == '' then\n"
            "        overlay:remove()\n"
            "        return\n"
            "    end\n"
            "    local escaped = escape_ass(raw)\n"
            "    local shift = state.depth * 0.5\n"
            "    if state.swap_eyes then\n"
            "        shift = -shift\n"
            "    end\n"
            "    local top_x = math.floor(960 + shift + 0.5)\n"
            "    local bot_x = math.floor(960 - shift + 0.5)\n"
            "    local top_y = 1080 - state.margin_y\n"
            "    local bot_y = 2205 - state.margin_y\n"
            "    local style = string.format('{\\\\an2\\\\fs%d\\\\bord2.5\\\\shad1\\\\1c&HFFFFFF&\\\\3c&H000000&', state.font_size)\n"
            "    local line_top = string.format('%s\\\\pos(%d,%d)}%s', style, top_x, top_y, escaped)\n"
            "    local line_bot = string.format('%s\\\\pos(%d,%d)}%s', style, bot_x, bot_y, escaped)\n"
            "    overlay.data = line_top .. '\\n' .. line_bot\n"
            "    overlay:update()\n"
            "end\n"
            "\n"
            "mp.observe_property('sub-text', 'string', function()\n"
            "    render()\n"
            "end)\n"
            "\n"
            "mp.register_script_message('stereo-subs-config', function(enabled, depth, font_size, margin_y, swap_eyes)\n"
            "    local want = (enabled == '1')\n"
            "    state.depth = tonumber(depth) or 8\n"
            "    state.font_size = tonumber(font_size) or 52\n"
            "    state.margin_y = tonumber(margin_y) or 54\n"
            "    state.swap_eyes = (swap_eyes == '1')\n"
            "\n"
            "    if want and not state.enabled then\n"
            "        state.saved_vis = mp.get_property_bool('sub-visibility', true)\n"
            "        mp.set_property_bool('sub-visibility', false)\n"
            "        state.enabled = true\n"
            "    elseif not want and state.enabled then\n"
            "        state.enabled = false\n"
            "        overlay:remove()\n"
            "        if state.saved_vis ~= nil then\n"
            "            mp.set_property_bool('sub-visibility', state.saved_vis)\n"
            "            state.saved_vis = nil\n"
            "        else\n"
            "            mp.set_property_bool('sub-visibility', true)\n"
            "        end\n"
            "    end\n"
            "    render()\n"
            "end)\n";

        QFile file(fullPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            file.write(kLua);
            file.close();
        }
        return QDir::fromNativeSeparators(fullPath);
    }

    void apply(PlayerProcess *process,
               bool enabled,
               int depthPx,
               int fontSize,
               int marginY,
               bool swapEyes) {
        if (!process || !process->isMPV() || !process->isRunning()) {
            loadedPid = 0;
            return;
        }
        const qint64 pid = process->processId();
        const bool needLoad = (pid != 0 && pid != loadedPid);
        if (needLoad) {
            if (!enabled) return;
            const QString scriptPath = ensureScript();
            process->sendCommand(QString("load-script \"%1\"").arg(scriptPath));
            loadedPid = pid;
        }

        const QString msg = QString("script-message stereo-subs-config %1 %2 %3 %4 %5")
            .arg(enabled ? 1 : 0)
            .arg(depthPx)
            .arg(fontSize)
            .arg(marginY)
            .arg(swapEyes ? 1 : 0);
        process->sendCommand(msg);

        if (needLoad) {
            QPointer<PlayerProcess> guarded(process);
            QTimer::singleShot(250, guarded, [guarded, pid, msg]() {
                if (guarded && guarded->isRunning() && guarded->processId() == pid) {
                    guarded->sendCommand(msg);
                }
            });
        }
    }

    void reset() {
        loadedPid = 0;
    }

private:
    qint64 loadedPid = 0;
};

#endif
