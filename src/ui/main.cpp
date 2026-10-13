// VGM Music Maker 1.1 - Qt port.
// Original by Shiru 2006-2011
// Reverse engineered using Ghidra
// Comments and code cleaned up using Opus
// I also made some small features and UI fixes
// -Drozerix 2026

#include <QApplication>
#include <QStyleFactory>
#include <cstdio>
#include <cstring>

#include "dialogs.h"
#include "mainwindow.h"

// Batch conversion as offered by the original executable:
//   vgmmm -vgm|-sms|-gg|-vgz|-smz|-ggz|-tfd|-wav|-txt file.vge
// The output goes next to the module, with the matching extension.
static int convert(const QStringList &args)
{
    QString mode, file;
    for (int i = 1; i < args.size(); i++) {
        if (args[i].startsWith('-'))
            mode = args[i].mid(1).toLower();
        else
            file = args[i];
    }
    if (file.isEmpty())
        return 1;
    sample_data_init();
    song_new();
    if (song_load(file.toLocal8Bit().constData()) != 0) {
        fprintf(stderr, "Can't load module '%s'\n", qPrintable(file));
        return 1;
    }
    tables_init();
    song_calc_timing();
    chips_init((int)g_song.fmClock, (int)g_song.psgClock, 44100);
    chips_reset();
    QFileInfo fi(file);
    QString base = fi.absolutePath() + "/" + fi.completeBaseName();
    auto name = [&](const char *ext) { return (base + ext).toLocal8Bit(); };
    bool ok = false;
    if (mode == "vgm")
        ok = export_vgm(name(".vgm").constData(), 0, false, true);
    else if (mode == "sms")
        ok = export_vgm(name(".vgm").constData(), 1, false, true);
    else if (mode == "gg")
        ok = export_vgm(name(".vgm").constData(), 2, false, true);
    else if (mode == "vgz")
        ok = export_vgm(name(".vgz").constData(), 0, true, true);
    else if (mode == "smz")
        ok = export_vgm(name(".vgz").constData(), 1, true, true);
    else if (mode == "ggz")
        ok = export_vgm(name(".vgz").constData(), 2, true, true);
    else if (mode == "tfd" || mode == "smd")
        ok = export_tfd(name(".tfd").constData());
    else if (mode == "txt")
        ok = export_text(name(".txt").constData());
    else if (mode == "wav")
        ok = export_wav(name(".wav").constData(), true, nullptr, nullptr);
    else
        fprintf(stderr, "Unknown conversion '-%s'\n", qPrintable(mode));
    chips_shutdown();
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    QCoreApplication::setOrganizationName("Shiru");
    QCoreApplication::setApplicationName("VGM Music Maker");
    QApplication app(argc, argv);
    QStringList args = app.arguments();
    for (int i = 1; i < args.size(); i++) {
        static const char *const modes[] = {"-vgm", "-sms", "-gg", "-vgz", "-smz", "-ggz", "-tfd", "-smd", "-wav", "-txt"};
        for (const char *m : modes)
            if (args[i].compare(m, Qt::CaseInsensitive) == 0)
                return convert(args);
    }

    // The original is a classic VCL application: grey 3D controls, 13 pixel sans-serif font.
    bool styleGiven = false;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-style"))
            styleGiven = true;
    if (!styleGiven && !qEnvironmentVariableIsSet("QT_STYLE_OVERRIDE")) {
        QApplication::setStyle(QStyleFactory::create("Fusion"));
        QPalette pal;
        pal.setColor(QPalette::Window, QColor(0xf0, 0xf0, 0xf0));
        pal.setColor(QPalette::Button, QColor(0xf0, 0xf0, 0xf0));
        pal.setColor(QPalette::Base, Qt::white);
        pal.setColor(QPalette::AlternateBase, QColor(0xf7, 0xf7, 0xf7));
        pal.setColor(QPalette::WindowText, Qt::black);
        pal.setColor(QPalette::Text, Qt::black);
        pal.setColor(QPalette::ButtonText, Qt::black);
        pal.setColor(QPalette::Highlight, QColor(0x33, 0x99, 0xff));
        pal.setColor(QPalette::HighlightedText, Qt::white);
        pal.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xe1));
        pal.setColor(QPalette::ToolTipText, Qt::black);
        pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x80, 0x80, 0x80));
        pal.setColor(QPalette::Disabled, QPalette::Text, QColor(0x80, 0x80, 0x80));
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x80, 0x80, 0x80));
        QApplication::setPalette(pal);
    }
    QFont f;
    for (const char *family : {"Tahoma", "Liberation Sans", "DejaVu Sans"}) {
        if (QFontDatabase::hasFamily(family)) {
            f = QFont(family);
            break;
        }
    }
    f.setPixelSize(13);
    QApplication::setFont(f);

    // classic group boxes: caption on the frame line
    app.setStyleSheet("QToolButton { padding: 0px; }"
                      "QGroupBox { border: 1px solid #b4b4b4; border-radius: 2px; margin-top: 7px; }"
                      "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 7px;"
                      " padding: 0px 2px; }");

    MainWindow w;
    w.show();
    for (int i = 1; i < args.size(); i++) {
        if (!args[i].startsWith('-') && QFile::exists(args[i])) {
            w.loadModule(args[i]);
            break;
        }
    }
    // test hook: VGMMM_SCREENSHOT=file.png [VGMMM_TAB=n] [VGMMM_KEYS="Z Ctrl+C Down ..."] [VGMMM_CLICK=...] sends
    // the clicks, then the keys, to the window, saves a picture of it and exits
    if (qEnvironmentVariableIsSet("VGMMM_SCREENSHOT")) {
        if (qEnvironmentVariableIsSet("VGMMM_TAB"))
            w.PageControlMain->setCurrentIndex(qEnvironmentVariableIntValue("VGMMM_TAB"));
        QTimer::singleShot(900, [&w] {
            w.activateWindow();
            const QStringList keys = qEnvironmentVariable("VGMMM_KEYS").split(' ', Qt::SkipEmptyParts);
            for (const QString &k : keys) {
                QKeyCombination kc = QKeySequence::fromString(k)[0];
                QString text = kc.keyboardModifiers() == Qt::NoModifier && k.size() == 1 ? k.toLower() : QString();
                const struct { Qt::KeyboardModifier mod; Qt::Key key; } mods[3] = {
                    {Qt::ShiftModifier, Qt::Key_Shift}, {Qt::ControlModifier, Qt::Key_Control}, {Qt::AltModifier, Qt::Key_Alt}};
                Qt::KeyboardModifiers held;
                for (const auto &m : mods) {
                    if (!(kc.keyboardModifiers() & m.mod))
                        continue;
                    QKeyEvent e(QEvent::KeyPress, m.key, held);
                    QCoreApplication::sendEvent(w.windowHandle(), &e);
                    held |= m.mod;
                }
                QKeyEvent press(QEvent::KeyPress, kc.key(), held, text);
                QCoreApplication::sendEvent(w.windowHandle(), &press);
                QKeyEvent release(QEvent::KeyRelease, kc.key(), held, text);
                QCoreApplication::sendEvent(w.windowHandle(), &release);
                for (const auto &m : mods) {
                    if (!(held & m.mod))
                        continue;
                    held &= ~Qt::KeyboardModifiers(m.mod);
                    QKeyEvent e(QEvent::KeyRelease, m.key, held);
                    QCoreApplication::sendEvent(w.windowHandle(), &e);
                }
                QCoreApplication::processEvents();
            }
        });
        QTimer::singleShot(500, [&w] {
            // VGMMM_CLICK="x,y x,y,r x,y,d ...": left clicks (r = right button, d = press and drag 40 px)
            const QStringList clicks = qEnvironmentVariable("VGMMM_CLICK").split(' ', Qt::SkipEmptyParts);
            for (const QString &c : clicks) {
                QStringList f = c.split(',');
                QPoint pos(f.value(0).toInt(), f.value(1).toInt());
                QWidget *t = w.childAt(pos);
                if (!t)
                    continue;
                Qt::MouseButton b = f.value(2) == "r" ? Qt::RightButton : Qt::LeftButton;
                QPoint local = t->mapFrom(&w, pos);
                QMouseEvent press(QEvent::MouseButtonPress, local, t->mapToGlobal(local), b, b, Qt::NoModifier);
                QCoreApplication::sendEvent(t, &press);
                if (f.value(2) == "d") {
                    QPoint to = local + QPoint(40, 40);
                    QMouseEvent move(QEvent::MouseMove, to, t->mapToGlobal(to), Qt::NoButton, b, Qt::NoModifier);
                    QCoreApplication::sendEvent(t, &move);
                    local = to;
                }
                QMouseEvent release(QEvent::MouseButtonRelease, local, t->mapToGlobal(local), b, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(t, &release);
                QCoreApplication::processEvents();
            }
        });
        QTimer::singleShot(1200, [&w] {
            // VGMMM_DIALOGS=dir: pictures of all dialogs
            if (qEnvironmentVariableIsSet("VGMMM_DIALOGS")) {
                QString dir = qEnvironmentVariable("VGMMM_DIALOGS");
                QList<QPair<QString, QDialog *>> dlgs = {
                    {"about", new AboutDialog(&w, "VGM Music Maker v1.1-qt 15.11.11\n\nEditor code by Shiru, (C)2006-2011\n\nReverse engineered and ported to QT by Drozerix using Ghidra")},
                    {"copyexop", OpCopyDialog::instance(&w)},   {"copyexins", InsCopyDialog::instance(&w)},
                    {"duplicate", DuplicateDialog::instance(&w)}, {"scale", ScaleDialog::instance(&w)},
                    {"transpose", TransposeDialog::instance(&w)}, {"chnexchange", ChnExchangeDialog::instance(&w)},
                    {"changevolume", ChangeVolumeDialog::instance(&w)}, {"renderwav", new RenderWavDialog(&w)}};
                for (auto &d : dlgs) {
                    d.second->show();
                    QCoreApplication::processEvents();
                    d.second->grab().save(dir + "/" + d.first + ".png");
                    d.second->hide();
                }
            }
            w.grab().save(qEnvironmentVariable("VGMMM_SCREENSHOT"));
            _Exit(0);
        });
    }
    return app.exec();
}
