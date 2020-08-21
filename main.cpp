/* 
 *
 * Copyright 1991-2019 Rui Fernando Ferreira Ribeiro.
 *
 */

#include "QtSpecem.h"
#include "interfacez/interfacez.h"
#include <getopt.h>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>

extern "C" void init_emul();
extern "C" void init_pallete();
extern "C" void open_sna(const char *);
extern "C" void writebyte(unsigned short, unsigned char);
extern "C" void patch_rom(int);
extern unsigned char * mem;
static InterfaceZ *iz;

static char *customrom=NULL;

int main(int argc, char **argv)
{

    QApplication app(argc, argv);
    //DrawnWindow draw;
    SpectrumWidget *spectrumWidget = new SpectrumWidget();

    KeyCapturer *key= new KeyCapturer();
    app.installEventFilter(key);

    QMainWindow *mainw = new EmulatorWindow();

    //spectrumWidget->installEventFilter(mainw);

    QWidget *mainwidget = new QWidget();

    mainw->setCentralWidget(mainwidget);

    QVBoxLayout *l = new QVBoxLayout();
    l->setSpacing(0);


    mainwidget->setLayout(l);

    // Add buttons.
    QHBoxLayout *hl = new QHBoxLayout();
    QPushButton *nmi = new QPushButton("NMI");
    QPushButton *io0 = new QPushButton("IO0");
    hl->addWidget(nmi);
    hl->addWidget(io0);


    l->addLayout(hl);
    l->addWidget(spectrumWidget);
    mainw->show();

    const char * p;
    QByteArray data;
    int i;
    int c;

    do {
        c = getopt(argc, argv, "r:");

        if (c<0)
            break;
        switch (c) {
        case 'r':
            customrom = optarg;
            break;
        default:
            printf("Unk %d\n", c);
            break;
        }
    } while (1);


    QString filename;

    filename = ":/rom/spectrum.rom";

    printf("Using rom %s\n", filename.toLatin1().constData());
    QFile file(filename);

    printf("Init pallete\n");
    init_pallete();
    
    printf("Init emul\n");
    init_emul();

    //draw.show();

    if(file.open(QIODevice::ReadOnly)){
        data=file.readAll();
        file.close();
        p=data;
        for (i=0; i < 16384 ; i++)
            *(mem+i) = *(p++);
    } else {
        printf("Cannot open ROM file\n");
        return -1;
    }

    iz = InterfaceZ::get();

    if (iz->init()<0) {
        fprintf(stderr,"Cannot init InterfaceZ\n");
        return -1;
    }

    iz->linkGPIO(nmi, 34);
    iz->linkGPIO(io0, 0);



    if (customrom) {
        iz->loadCustomROM(customrom);
    }

    //QObject::connect( spectrumWidget, &DrawnWindow::sdConnected, iz, &InterfaceZ::onSDConnected);
    //QObject::connect( spectrumWidget, &DrawnWindow::sdDisconnected, iz, &InterfaceZ::onSDDisconnected);
    QObject::connect( spectrumWidget, &SpectrumWidget::NMI, iz, &InterfaceZ::onNMI);

    int lindex = 0;
    for (int index = optind; index < argc; index++) {
        //printf ("Non-option argument %s\n", argv[index]);
        if (lindex==0) {
            open_sna(argv[index]);
        }
        lindex++;
    }


    spectrumWidget->show();

    return app.exec();
}

