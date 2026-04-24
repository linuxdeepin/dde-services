// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <QCoreApplication>

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

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("dde-shortcut-tool");
    app.setApplicationVersion("1.0.0");

    CommandParser parser;
    
    // Register all controllers
    parser.registerController(new AudioController);
    parser.registerController(new DisplayController);
    parser.registerController(new TouchPadController);
    parser.registerController(new PowerController);
    parser.registerController(new KbdLightController);
    parser.registerController(new MediaPlayerController);
    parser.registerController(new LockKeyController); // NumLock, CapsLock
    parser.registerController(new LaunchController);
    parser.registerController(new NetworkController);

    // Execute command and return result
    return parser.run(argc, argv);
}
