#include "MainWindow.h"
#include "Clothing.h"
#include "AppMenu.h"
#include <QApplication>
#include <QMenuBar>
#include <QAction>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QQuickView>
#include <QQuickItem>
#include <QTimer>
#include <QFileDialog>
#include <cmath>
#include <cstdio>
#include <exception>

// 普通 Qt 控件创建函数：QML 发出信号，标准 QTimer 合并同一轮事件中的更新。
// 不定义 Q_OBJECT/Q_PROPERTY，也不需要项目生成的 moc 文件。
QWidget *createControls(MainWindow *viewport, QWidget *parent, QObject **rootOut = nullptr)
{
    auto *controls = new QQuickView;
    controls->setResizeMode(QQuickView::SizeRootObjectToView);
    controls->setSource(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (controls->status() == QQuickView::Error)
    {
        delete controls;
        return nullptr;
    }

    auto *panel = QWidget::createWindowContainer(controls, parent);
    panel->setFixedWidth(280);
    auto *root = controls->rootObject();
    if (rootOut) *rootOut = root;
    viewport->setNavigationControls(root);
    auto *update = new QTimer(panel);
    update->setSingleShot(true);
    update->setInterval(0);

    auto applyControls = [viewport, root]()
    {
        viewport->applyCameraControls();
        viewport->requestUpdate();
    };

    // QML 动态信号连接到 Qt 自带的 start() 槽；timeout 再调用普通 C++ lambda。
    if (!QObject::connect(root, SIGNAL(settingsChanged()), update, SLOT(start())))
    {
        delete panel;
        return nullptr;
    }
    QObject::connect(update, &QTimer::timeout, viewport, applyControls);

    viewport->prepareNavigation = [update, applyControls]() mutable
    {
        update->stop();
        applyControls();
    };
    QObject::connect(root, &QObject::destroyed, viewport, [viewport]()
    {
        viewport->prepareNavigation = {};
    });
    auto *bodyUpdate = new QTimer(panel);
    bodyUpdate->setSingleShot(true);
    QObject::connect(root, SIGNAL(bodyChanged()), bodyUpdate, SLOT(start()));
    QObject::connect(bodyUpdate, &QTimer::timeout, viewport, [viewport, root]() {
        viewport->applyBodyControls(root);
    });
    auto *open = new QTimer(panel);
    open->setSingleShot(true);
    QObject::connect(root, SIGNAL(openObj()), open, SLOT(start()));
    QObject::connect(open, &QTimer::timeout, viewport, [viewport, panel]() {
        const auto path = QFileDialog::getOpenFileName(panel, "Open mesh", {}, "OBJ mesh (*.obj *.OBJ)");
        if (path.isEmpty()) return;
        try { viewport->openObjFile(path.toStdString()); }
        catch (const std::exception &error) { QMessageBox::warning(panel, "Cannot load mesh", error.what()); }
    });
    auto *cube = new QTimer(panel);
    cube->setSingleShot(true);
    QObject::connect(root, SIGNAL(showCube()), cube, SLOT(start()));
    QObject::connect(cube, &QTimer::timeout, viewport, [viewport]() { viewport->showCubeMesh(); });
    auto *shirtUpdate = new QTimer(panel);
    shirtUpdate->setSingleShot(true);
    QObject::connect(root, SIGNAL(shirtChanged()), shirtUpdate, SLOT(start()));
    QObject::connect(shirtUpdate, &QTimer::timeout, viewport, [viewport, root]() {
        viewport->setShirtEnabled(root->property("shirtEnabled").toBool());
    });
    auto *clothUpdate = new QTimer(panel);
    clothUpdate->setSingleShot(true);
    QObject::connect(root, SIGNAL(clothSettingsChanged()), clothUpdate, SLOT(start()));
    QObject::connect(clothUpdate, &QTimer::timeout, viewport, [viewport, root]() {
        viewport->setClothState(root->property("clothEnabled").toBool(),
            root->property("clothRunning").toBool(), root->property("clothWind").toFloat());
    });
    auto *clothReset = new QTimer(panel);
    clothReset->setSingleShot(true);
    QObject::connect(root, SIGNAL(resetCloth()), clothReset, SLOT(start()));
    QObject::connect(clothReset, &QTimer::timeout, viewport, [viewport]() { viewport->resetCloth(); });
    viewport->applyBodyControls(root);
    viewport->setClothState(root->property("clothEnabled").toBool(),
        root->property("clothRunning").toBool(), root->property("clothWind").toFloat());
    applyControls();
    return panel;
}

int main(int argc, char **argv)
{
    const bool validate = argc > 1 && std::string(argv[1]) == "--validate";
    if (validate) qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    if (validate) {
        QWidget host;
        auto *viewport = new MainWindow;
        QObject *controls = nullptr;
        auto *panel = createControls(viewport, &host, &controls);
        if (!panel) { delete viewport; return 1; }
        const auto mesh = makehuman::buildHumanMesh({});
        bool valid = !mesh.empty() && mesh.size() % 3 == 0;
        for (const auto &v : mesh)
            valid = valid && std::isfinite(v.pos.x) && std::isfinite(v.pos.y) && std::isfinite(v.pos.z);
        auto *row = new QHBoxLayout(&host);
        row->addWidget(panel);
        auto *viewportPlaceholder = new QWidget(&host);
        row->addWidget(viewportPlaceholder);
        auto *menu = createAppMenu(&host, viewport, controls, panel, row);
        row->setMenuBar(menu);
        valid = valid && menu->actions().size() == 6;
        const auto trigger = [menu](const char *name) {
            auto *action = menu->findChild<QAction *>(name);
            if (!action) return false;
            action->trigger(); return true;
        };
        valid = trigger("rightView") && valid;
        valid = valid && viewport->spin == -90 && viewport->tilt == 0;
        valid = trigger("topView") && valid;
        valid = valid && viewport->spin == 0 && viewport->tilt == 90;
        valid = trigger("frontView") && valid;
        valid = valid && viewport->spin == 0 && viewport->tilt == 0;
        valid = trigger("panelRight") && valid;
        valid = valid && row->indexOf(panel) == 1;
        valid = trigger("panelLeft") && valid;
        valid = valid && row->indexOf(panel) == 0;
        valid = trigger("viewParameters") && valid;
        valid = valid && !controls->property("humanPage").toBool();
        valid = trigger("makeHuman") && valid;
        valid = valid && controls->property("humanPage").toBool();
        valid = valid && controls->property("clothEnabled").toBool();
        valid = trigger("wearCloth") && valid;
        app.processEvents();
        valid = valid && !controls->property("clothEnabled").toBool();
        valid = trigger("wearCloth") && valid;
        app.processEvents();
        valid = valid && controls->property("clothEnabled").toBool();
        valid = trigger("pauseCloth") && valid;
        app.processEvents();
        valid = valid && !controls->property("clothRunning").toBool();
        valid = trigger("resetCloth") && valid;
        app.processEvents();
        valid = trigger("wearShirt") && valid;
        app.processEvents();
        valid = valid && !controls->property("shirtEnabled").toBool();
        valid = trigger("wearShirt") && valid;
        app.processEvents();
        valid = valid && controls->property("shirtEnabled").toBool();
        const auto top = buildShirt(mesh);
        valid = valid && !top.empty() && top.size()%3 == 0;
        for (auto v : top) valid = valid && std::isfinite(v.pos.x) && std::isfinite(v.pos.y) && std::isfinite(v.pos.z);
        ClothSimulation garment;
        garment.reset(mesh);
        const auto anchors = garment.positions();
        for (int step = 0; step < 20; ++step) garment.step(1.0f/60, 4);
        valid = valid && garment.mesh().size() == 27*48*6;
        for (size_t i = 0; i < garment.positions().size(); ++i) {
            const auto p = garment.positions()[i];
            valid = valid && std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
            if (i < 48) valid = valid && p == anchors[i];
        }
        valid = trigger("cube") && valid;
        app.processEvents();
        valid = valid && !controls->property("clothEnabled").toBool();
        delete menu;
        delete panel;
        delete viewport;
        fprintf(stdout, "QML, menus, human mesh and cloth validation: %s (%zu vertices)\n", valid ? "passed" : "failed", mesh.size());
        return valid ? 0 : 1;
    }
    if (QGuiApplication::platformName() != QStringLiteral("xcb"))
    {
        QMessageBox::critical(nullptr, "Unsupported window system",
                              "This native Vulkan surface implementation uses X11. "
                              "Run with QT_QPA_PLATFORM=xcb.");
        return 1;
    }

    QWidget window;
    window.setWindowTitle("Human model — Vulkan");
    window.resize(1100, 800);

    auto *row = new QHBoxLayout(&window);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    auto *viewport = new MainWindow;

    auto *viewportContainer = QWidget::createWindowContainer(viewport);

    QObject *controls = nullptr;
    auto *panel = createControls(viewport, &window, &controls);
    if (!panel)
        return 1;
    row->addWidget(panel);
    row->addWidget(viewportContainer, 1);
    row->setMenuBar(createAppMenu(&window, viewport, controls, panel, row));

    window.show();
    return app.exec();
}

