#pragma once
class QWidget;
class QObject;
class QHBoxLayout;
class QMenuBar;
class MainWindow;

QMenuBar *createAppMenu(QWidget *host, MainWindow *viewport, QObject *controls,
                        QWidget *panel, QHBoxLayout *row);
