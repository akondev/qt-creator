# This file should not be edited.
# Following versions of Qt Creator might offer new version.

QT += declarative

SOURCES += $$PWD/qmlapplicationviewer.cpp
HEADERS += $$PWD/qmlapplicationviewer.h
INCLUDEPATH += $$PWD

contains(DEFINES, QMLINSPECTOR) {
    CONFIG(debug, debug|release) {
        isEmpty(QMLINSPECTOR_PATH) {
            warning(QMLINSPECTOR_PATH was not set. QMLINSPECTOR not activated.)
            DEFINES -= QMLINSPECTOR
        } else {
            include($$QMLINSPECTOR_PATH/qmljsdebugger-lib.pri)
        }
    } else {
        DEFINES -= QMLINSPECTOR
    }
}

for(deploymentfolder, DEPLOYMENTFOLDERS) {
    item = item$${deploymentfolder}
    itemsources = $${item}.sources
    $$itemsources = $$eval($${deploymentfolder}.source)
    itempath = $${item}.path
    $$itempath= $$eval($${deploymentfolder}.target)
    DEPLOYMENT += $$item
}

MAINPROFILEPWD = $$PWD/..

symbian {
    TARGET.EPOCHEAPSIZE = 0x20000 0x2000000
    contains(DEFINES, ORIENTATIONLOCK):LIBS += -lavkon -leikcore -leiksrv -lcone
    contains(DEFINES, NETWORKACCESS):TARGET.CAPABILITY += NetworkServices
} else:win32 {
    !isEqual(PWD,$$OUT_PWD) {
        copyCommand = @echo Copying application data...
        for(deploymentfolder, DEPLOYMENTFOLDERS) {
            source = $$eval($${deploymentfolder}.source)
            pathSegments = $$split(source, /)
            sourceAndTarget = $$MAINPROFILEPWD/$$source $$OUT_PWD/$$eval($${deploymentfolder}.target)/$$last(pathSegments)
            copyCommand += && $(COPY_DIR) $$replace(sourceAndTarget, /, \\)
        }
        copydeploymentfolders.commands = $$copyCommand
        first.depends = $(first) copydeploymentfolders
        QMAKE_EXTRA_TARGETS += first copydeploymentfolders
    }
} else:unix {
    maemo5 {
        desktopfile.path = /usr/share/applications/hildon       
    } else {
        desktopfile.path = /usr/share/applications
        !isEqual(PWD,$$OUT_PWD) {
            copyCommand = @echo Copying application data...
            for(deploymentfolder, DEPLOYMENTFOLDERS) {
                macx {
                    target = $$OUT_PWD/$${TARGET}.app/Contents/Resources/$$eval($${deploymentfolder}.target)
                } else {
                    target = $$OUT_PWD/$$eval($${deploymentfolder}.target)
                }
                copyCommand += && $(MKDIR) $$target
                copyCommand += && $(COPY_DIR) $$MAINPROFILEPWD/$$eval($${deploymentfolder}.source) $$target
            }
            copydeploymentfolders.commands = $$copyCommand
            first.depends = $(first) copydeploymentfolders
            QMAKE_EXTRA_TARGETS += first copydeploymentfolders
        }
    }
    for(deploymentfolder, DEPLOYMENTFOLDERS) {
        item = item$${deploymentfolder}
        itemfiles = $${item}.files
        $$itemfiles = $$eval($${deploymentfolder}.source)
        itempath = $${item}.path
        $$itempath = /opt/share/$$eval($${deploymentfolder}.target)
        INSTALLS += $$item
    }
    icon.files = $${TARGET}.png
    icon.path = /usr/share/icons/hicolor/64x64
    desktopfile.files = $${TARGET}.desktop
    target.path = /opt/bin
    INSTALLS += desktopfile icon target
}
