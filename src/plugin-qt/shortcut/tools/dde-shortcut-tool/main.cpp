// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <QGuiApplication>

#include "commandparser.h"
#include "audiocontroller.h"
#include "displaycontroller.h"
#include "touchpadcontroller.h"
#include "powercontroller.h"
#include "kbdlightcontroller.h"
#include "mediaplayercontroller.h"
#include "lockkeycontroller.h"
#include "launchcontroller.h"
#include "networkcontroller.h"
#include "wmcontroller.h"

namespace {

template <typename Controller>
void registerControllerFactory(CommandParser &parser)
{
    parser.registerControllerFactory(Controller::commandName(),
                                     Controller::commandActions(),
                                     Controller::commandActionHelp(),
                                     [] { return new Controller; });
}

}

int main(int argc, char *argv[])
{
    // QGuiApplication (not QCoreApplication) so PowerController can talk to
    // Treeland's dde-shell protocol for the Wayland shutdown UI; harmless on X11.
    QGuiApplication app(argc, argv);
    app.setApplicationName("dde-shortcut-tool");
    app.setApplicationVersion("1.0.0");

    CommandParser parser;
    
    // Create controllers on demand so each shortcut action only initializes the module it actually needs.
    registerControllerFactory<AudioController>(parser);
    registerControllerFactory<DisplayController>(parser);
    registerControllerFactory<TouchPadController>(parser);
    registerControllerFactory<PowerController>(parser);
    registerControllerFactory<KbdLightController>(parser);
    registerControllerFactory<MediaPlayerController>(parser);
    registerControllerFactory<LockKeyController>(parser);
    registerControllerFactory<LaunchController>(parser);
    registerControllerFactory<NetworkController>(parser);
    registerControllerFactory<WmController>(parser);

    // Execute command and return result
    return parser.run(argc, argv);
}
