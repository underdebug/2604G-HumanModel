#include "AppMenu.h"
#include "MainWindow.h"
#include "Test.h"

#include <QAction>
#include <QCoreApplication>
#include <QDialog>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QTemporaryDir>
#include <QVariant>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

namespace {
void showDepthBuffer(QWidget *host, MainWindow *viewport)
{
    struct Capture {
        std::vector<float> depth;
        uint32_t width = 0, height = 0;
        float low = 0, high = 1;
        QImage image;
    };
    auto state = std::make_shared<Capture>();
    auto *dialog = new QDialog(host);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Depth Buffer");
    dialog->resize(800, 650);
    auto *layout = new QVBoxLayout(dialog);
    auto *status = new QLabel(dialog);
    layout->addWidget(status);
    auto *scroll = new QScrollArea(dialog);
    auto *image = new QLabel;
    image->setAlignment(Qt::AlignCenter);
    scroll->setWidget(image);
    layout->addWidget(scroll, 1);
    auto *low = new QSlider(Qt::Horizontal, dialog);
    auto *high = new QSlider(Qt::Horizontal, dialog);
    for (auto *slider : {low, high}) slider->setRange(0, 1000);
    low->setMaximum(999); high->setMinimum(1);
    high->setValue(1000);
    layout->addWidget(new QLabel("Black level", dialog)); layout->addWidget(low);
    layout->addWidget(new QLabel("White level", dialog)); layout->addWidget(high);
    auto redraw = [state, low, high, image, status]() {
        if (state->depth.empty()) return;
        const float range = std::max(state->high - state->low, 1e-7f);
        const float black = state->low + range * low->value() / 1000.0f;
        const float white = state->low + range * high->value() / 1000.0f;
        state->image = QImage(int(state->width), int(state->height), QImage::Format_Grayscale8);
        for (uint32_t y = 0; y < state->height; ++y) {
            auto *line = state->image.scanLine(int(y));
            for (uint32_t x = 0; x < state->width; ++x) {
                const float d = state->depth[size_t(y) * state->width + x];
                line[x] = std::isfinite(d) && d < 1.0f
                    ? uchar(255 * std::clamp((d - black) / std::max(white - black, 1e-7f), 0.0f, 1.0f)) : 0;
            }
        }
        image->setPixmap(QPixmap::fromImage(state->image));
        image->resize(state->image.size());
        status->setText(QString("%1 × %2 | Depth window: %3 – %4")
            .arg(state->width).arg(state->height).arg(black, 0, 'f', 6).arg(white, 0, 'f', 6));
    };
    QObject::connect(low, &QSlider::valueChanged, dialog, [low, high, redraw](int value) {
        if (value >= high->value()) high->setValue(std::min(value + 1, 1000));
        redraw();
    });
    QObject::connect(high, &QSlider::valueChanged, dialog, [low, high, redraw](int value) {
        if (value <= low->value()) low->setValue(std::max(value - 1, 0));
        redraw();
    });
    auto refresh = [state, viewport, status, redraw]() {
        try {
            if (viewport->isExposed()) viewport->render();
            if (!viewport->captureDepthBuffer(state->depth, state->width, state->height)) {
                status->setText("Depth is not available yet. Show the viewport and click Refresh.");
                return;
            }
            state->low = 1; state->high = 0;
            for (float d : state->depth) if (std::isfinite(d) && d >= 0 && d < 1) {
                state->low = std::min(state->low, d); state->high = std::max(state->high, d);
            }
            if (state->high < state->low) { state->low = 0; state->high = 1; }
            redraw();
        } catch (const std::exception &error) { status->setText(error.what()); }
    };
    auto *buttons = new QHBoxLayout;
    auto *refreshButton = new QPushButton("Refresh", dialog);
    auto *save = new QPushButton("Save PNG…", dialog);
    buttons->addWidget(refreshButton); buttons->addWidget(save);
    layout->addLayout(buttons);
    QObject::connect(refreshButton, &QPushButton::clicked, dialog, refresh);
    QObject::connect(save, &QPushButton::clicked, dialog, [state, dialog]() {
        if (state->image.isNull()) return;
        const auto path = QFileDialog::getSaveFileName(dialog, "Save depth image", "depth.png", "PNG image (*.png)");
        if (!path.isEmpty() && !state->image.save(path, "PNG"))
            QMessageBox::warning(dialog, "Save failed", "Could not save the depth image.");
    });
    refresh();
    dialog->show();
}
}

QMenuBar *createAppMenu(QWidget *host, MainWindow *viewport, QObject *controls,
                        QWidget *panel, QHBoxLayout *row)
{
    // Native popup menus can extend beyond the embedded QML and Vulkan windows.
    auto *bar = new QMenuBar(host);
    bar->setNativeMenuBar(false);
    bar->setObjectName("applicationMenu");
    auto add = [bar](QMenu *menu, const char *name, const QString &text, auto callback) {
        auto *action = menu->addAction(text);
        action->setObjectName(name);
        QObject::connect(action, &QAction::triggered, bar, callback);
        return action;
    };
    auto *file = bar->addMenu("File");
    auto *open = add(file, "openObj", "Open…", [controls]() { QMetaObject::invokeMethod(controls, "openObj"); });
    open->setShortcut(QKeySequence::Open);
    auto *view = bar->addMenu("View");
    auto rotation = [viewport](float spin, float tilt) {
        if (viewport->prepareNavigation) viewport->prepareNavigation();
        viewport->spin = spin; viewport->tilt = tilt; viewport->navigationChanged();
    };
    add(view, "frontView", "Front View", [rotation]() { rotation(0, 0); });
    add(view, "rightView", "Right View", [rotation]() { rotation(-90, 0); });
    add(view, "topView", "Top View", [rotation]() { rotation(0, 90); });
    view->addSeparator();
    add(view, "viewParameters", "View parameters", [controls, panel]() {
        controls->setProperty("humanPage", false); panel->show();
    });
    view->addSeparator();
    add(view, "panelLeft", "Panel Left", [row, panel]() { row->removeWidget(panel); row->insertWidget(0, panel); });
    add(view, "panelRight", "Panel Right", [row, panel]() { row->removeWidget(panel); row->addWidget(panel); });
    auto *shape = bar->addMenu("Shape");
    add(shape, "cube", "Cube", [viewport]() { viewport->showCubeMesh(); });
    auto *human = bar->addMenu("Human");
    add(human, "makeHuman", "Make Human", [controls, panel]() {
        controls->setProperty("humanPage", true); panel->show();
        QMetaObject::invokeMethod(controls, "bodyChanged");
    });
    auto *tools = bar->addMenu("Tools");
    add(tools, "depthBuffer", "Depth Buffer", [host, viewport]() { showDepthBuffer(host, viewport); });
    auto *clothMenu = tools->addMenu("Cloth Simulation");
    auto *wearShirt = add(clothMenu, "wearShirt", "Remove shirt", [controls]() {
        controls->setProperty("shirtEnabled", !controls->property("shirtEnabled").toBool());
    });
    QObject::connect(clothMenu, &QMenu::aboutToShow, bar, [wearShirt, controls]() {
        wearShirt->setText(controls->property("shirtEnabled").toBool() ? "Remove shirt" : "Wear shirt");
    });
    auto *wear = add(clothMenu, "wearCloth", "Wear skirt", [controls]() {
        controls->setProperty("clothEnabled", !controls->property("clothEnabled").toBool());
    });
    auto *pause = add(clothMenu, "pauseCloth", "Pause / Resume", [controls]() {
        controls->setProperty("clothRunning", !controls->property("clothRunning").toBool());
    });
    auto *reset = add(clothMenu, "resetCloth", "Reset cloth", [controls]() { QMetaObject::invokeMethod(controls, "resetCloth"); });
    QObject::connect(clothMenu, &QMenu::aboutToShow, bar, [wear, pause, reset, controls]() {
        wear->setText(controls->property("clothEnabled").toBool() ? "Remove skirt" : "Wear skirt");
        pause->setText(controls->property("clothRunning").toBool() ? "Pause" : "Resume");
        pause->setEnabled(controls->property("clothEnabled").toBool());
        reset->setEnabled(controls->property("clothEnabled").toBool());
    });
    auto *test = bar->addMenu("Test");
    add(test, "lscm", "lscm", [host, viewport]() {
        try {
            QTemporaryDir output;
            if (!output.isValid()) throw std::runtime_error("Cannot create LSCM output directory");
            const auto input = QCoreApplication::applicationDirPath() + "/models/qian.OBJ";
            const auto path = output.filePath("qian_NL.obj");
            if (!lscm(input.toStdString(), path.toStdString()))
                throw std::runtime_error("LSCM failed for models/qian.OBJ");
            viewport->openObjFile(path.toStdString());
        } catch (const std::exception &error) { QMessageBox::warning(host, "LSCM", error.what()); }
    });
    return bar;
}
