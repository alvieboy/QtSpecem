/* 
 *
 * Copyright 1991-2019 Rui Fernando Ferreira Ribeiro.
 *
 */

#include "QtSpecem.h"
#include "interfacez/interfacez.h"
#include <getopt.h>

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
    DrawnWindow draw;
    DrawnWindow *keyPress = new DrawnWindow();
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

    iz = new InterfaceZ();
    if (iz->init()<0) {
        fprintf(stderr,"Cannot init InterfaceZ\n");
        return -1;
    }

    if (customrom) {
        iz->loadCustomROM(customrom);
    }

    //QObject::connect( keyPress, &DrawnWindow::sdConnected, iz, &InterfaceZ::onSDConnected);
    //QObject::connect( keyPress, &DrawnWindow::sdDisconnected, iz, &InterfaceZ::onSDDisconnected);
    QObject::connect( keyPress, &DrawnWindow::NMI, iz, &InterfaceZ::onNMI);

    int lindex = 0;
    for (int index = optind; index < argc; index++) {
        //printf ("Non-option argument %s\n", argv[index]);
        if (lindex==0) {
            open_sna(argv[index]);
        }
        lindex++;
    }


    keyPress->show();

    return app.exec();
}

