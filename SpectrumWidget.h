#ifndef __SPECTRUMWIDGET_H__
#define __SPECTRUMWIDGET_H__

#include<QApplication>
#include<QMainWindow>
#include<QWidget>
#include<QPainter>
#include<QImage>
#include<QTimer>
#include <QtGui>

class Q_DECL_EXPORT SpectrumWidget: public QWidget {
Q_OBJECT
public:
    SpectrumWidget(QWidget *parent = 0);
    void paintEvent(QPaintEvent *) override;
    void KeyPress(QMainWindow *parent = 0);
protected:
    void drawBorder();
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *e) override;

    void contextMenuEvent(QContextMenuEvent *event) override;
    void triggerNMI();
signals:
    void NMI();
private:
    QTimer *timer;
};

#endif
