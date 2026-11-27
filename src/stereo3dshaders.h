// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef STEREO3DSHADERS_H
#define STEREO3DSHADERS_H

#include "paths.h"
#include <QDir>
#include <QFile>
#include <QString>

class Stereo3DShaders {
public:
    enum ShaderKind {
        None = 0,
        FramePackedTAB,
        FramePackedTABSwap,
        FramePackedSBS,
        FramePackedSBSSwap,
        MonoTAB,
        MonoTABSwap,
        MonoSBS,
        MonoSBSSwap
    };

    static QString ensureShader(ShaderKind kind) {
        if (kind == None) return QString();
        const QString dirPath = Paths::configPath() + "/shaders";
        QDir().mkpath(dirPath);

        QString filename;
        QString body;
        switch (kind) {
            case FramePackedTAB:
                filename = "fp_tab.glsl";
                body = framePackedTabShader(false);
                break;
            case FramePackedTABSwap:
                filename = "fp_tab_swap.glsl";
                body = framePackedTabShader(true);
                break;
            case FramePackedSBS:
                filename = "fp_sbs.glsl";
                body = framePackedSbsShader(false);
                break;
            case FramePackedSBSSwap:
                filename = "fp_sbs_swap.glsl";
                body = framePackedSbsShader(true);
                break;
            case MonoTAB:
                filename = "mono_tab.glsl";
                body = monoTabShader(false);
                break;
            case MonoTABSwap:
                filename = "mono_tab_swap.glsl";
                body = monoTabShader(true);
                break;
            case MonoSBS:
                filename = "mono_sbs.glsl";
                body = monoSbsShader(false);
                break;
            case MonoSBSSwap:
                filename = "mono_sbs_swap.glsl";
                body = monoSbsShader(true);
                break;
            default:
                return QString();
        }

        const QString fullPath = QDir::cleanPath(dirPath + "/" + filename);
        QFile file(fullPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            file.write(body.toUtf8());
            file.close();
        }
        return QDir::fromNativeSeparators(fullPath);
    }

private:
    static QString framePackedTabShader(bool swapEyes) {
        const QString topOffset = swapEyes ? "0.5" : "0.0";
        const QString bottomOffset = swapEyes ? "0.0" : "0.5";
        return QString(
            "//!HOOK OUTPUT\n"
            "//!BIND HOOKED\n"
            "//!DESC Frame-Packed 1920x2205 from Top-and-Bottom\n"
            "\n"
            "vec4 hook() {\n"
            "    float py = HOOKED_pos.y * 2205.0;\n"
            "    if (py < 1080.0) {\n"
            "        float v = %1 + (py / 1080.0) * 0.5;\n"
            "        return HOOKED_tex(vec2(HOOKED_pos.x, v));\n"
            "    } else if (py < 1125.0) {\n"
            "        return vec4(0.0, 0.0, 0.0, 1.0);\n"
            "    } else {\n"
            "        float v = %2 + ((py - 1125.0) / 1080.0) * 0.5;\n"
            "        return HOOKED_tex(vec2(HOOKED_pos.x, v));\n"
            "    }\n"
            "}\n").arg(topOffset, bottomOffset);
    }

    static QString framePackedSbsShader(bool swapEyes) {
        const QString topOffset = swapEyes ? "0.5" : "0.0";
        const QString bottomOffset = swapEyes ? "0.0" : "0.5";
        return QString(
            "//!HOOK OUTPUT\n"
            "//!BIND HOOKED\n"
            "//!DESC Frame-Packed 1920x2205 from Side-by-Side\n"
            "\n"
            "vec4 hook() {\n"
            "    float py = HOOKED_pos.y * 2205.0;\n"
            "    if (py < 1080.0) {\n"
            "        float u = %1 + HOOKED_pos.x * 0.5;\n"
            "        return HOOKED_tex(vec2(u, py / 1080.0));\n"
            "    } else if (py < 1125.0) {\n"
            "        return vec4(0.0, 0.0, 0.0, 1.0);\n"
            "    } else {\n"
            "        float u = %2 + HOOKED_pos.x * 0.5;\n"
            "        return HOOKED_tex(vec2(u, (py - 1125.0) / 1080.0));\n"
            "    }\n"
            "}\n").arg(topOffset, bottomOffset);
    }

    static QString monoTabShader(bool swapEyes) {
        const QString offset = swapEyes ? "0.5" : "0.0";
        return QString(
            "//!HOOK OUTPUT\n"
            "//!BIND HOOKED\n"
            "//!DESC 2D Single-Eye from Top-and-Bottom\n"
            "\n"
            "vec4 hook() {\n"
            "    return HOOKED_tex(vec2(HOOKED_pos.x, %1 + HOOKED_pos.y * 0.5));\n"
            "}\n").arg(offset);
    }

    static QString monoSbsShader(bool swapEyes) {
        const QString offset = swapEyes ? "0.5" : "0.0";
        return QString(
            "//!HOOK OUTPUT\n"
            "//!BIND HOOKED\n"
            "//!DESC 2D Single-Eye from Side-by-Side\n"
            "\n"
            "vec4 hook() {\n"
            "    return HOOKED_tex(vec2(%1 + HOOKED_pos.x * 0.5, HOOKED_pos.y));\n"
            "}\n").arg(offset);
    }
};

#endif
