#ifndef __RAND___H_
#define __RAND___H_

#include<QApplication>
#include<QMainWindow>
#include<QWidget>
#include<QPainter>
#include<QImage>
#include<QTimer>
#include <QtGui>
#include "SpectrumWidget.h"

class EmulatorWindow: public QMainWindow {
Q_OBJECT
    public:
    EmulatorWindow(QWidget *parent = 0);

protected:
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;

};
#endif

