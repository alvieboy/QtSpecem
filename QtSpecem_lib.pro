TEMPLATE = lib
QT += widgets
TARGET = QtSpecem
INCLUDEPATH += .
QT += widgets network
RESOURCES += QtSpecem.qrc
ICON = icon.icns
CONFIG += app_bundle debug

# Input
HEADERS += QtSpecem.h \
           SpectrumWidget.h \
           h/quirks.h \
           h/snap.h \
           z80core/env.h \
           z80core/iglobal.h \
           z80core/ivars.h \
           z80core/z80.h \
           expansion/expansion.h \
           interfacez/interfacez.h \
           interfacez/SnaFile.h \
           interfacez/Tape.h \
           interfacez/Client.h \
           interfacez/SocketClient.h \
           interfacez/LinkClient.h 

SOURCES += main.cpp \
           QtSpecem.cpp \
           emul/error.c \
           emul/floating_bus.c \
           emul/initmem.c \
           emul/shm_server.c \
           emul/sna_load.c \
           emul/sna_save.c \
           emul/ndebgz80.c \
           emul/ports.c \
           emul/video.c \
           z80core/bits.c \
           z80core/callret.c \
           z80core/exctranf.c \
           z80core/flags.c \
           z80core/init.c \
           z80core/inout.c \
           z80core/intr.c \
           z80core/jump.c \
           z80core/kernel.c \
           z80core/ld16bits.c \
           z80core/ld8bits.c \
           z80core/math16bi.c \
           z80core/math8bit.c \
           z80core/mem.c \
           z80core/misc.c \
           z80core/rotate.c \
           z80core/shutdown.c \
           z80core/stack.c \
           expansion/expansion.c \
           interfacez/interfacez.cpp \
           interfacez/hdlc_decoder.c \ 
           interfacez/hdlc_encoder.c \
           interfacez/Tape.cpp \
           interfacez/Client.cpp \
           interfacez/SocketClient.cpp \
           interfacez/LinkClient.cpp

RESOURCES += QtSpecem.qrc
