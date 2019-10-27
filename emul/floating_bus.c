 /* floating_bus.c: implement behaviour of floating bus in unused ports
 *
 * Copyright 2019 Rui Fernando Ferreira Ribeiro.
 *
 */

// see http://ramsoft.bbk.org.omegahg.com/floatingbus.html

#include "../z80core/env.h"

// RAM address of each pixel line on screen
int screen_lines[192] =
{
   0x4000,
   0x4100,
   0x4200,
   0x4300,
   0x4400,
   0x4500,
   0x4600,
   0x4700,
   0x4020,
   0x4120,
   0x4220,
   0x4320,
   0x4420,
   0x4520,
   0x4620,
   0x4720,
   0x4040,
   0x4140,
   0x4240,
   0x4340,
   0x4440,
   0x4540,
   0x4640,
   0x4740,
   0x4060,
   0x4160,
   0x4260,
   0x4360,
   0x4460,
   0x4560,
   0x4660,
   0x4760,
   0x4080,
   0x4180,
   0x4280,
   0x4380,
   0x4480,
   0x4580,
   0x4680,
   0x4780,
   0x40A0,
   0x41A0,
   0x42A0,
   0x43A0,
   0x44A0,
   0x45A0,
   0x46A0,
   0x47A0,
   0x40C0,
   0x41C0,
   0x42C0,
   0x43C0,
   0x44C0,
   0x45C0,
   0x46C0,
   0x47C0,
   0x40E0,
   0x41E0,
   0x42E0,
   0x43E0,
   0x44E0,
   0x45E0,
   0x46E0,
   0x47E0,
   0x4800,
   0x4900,
   0x4A00,
   0x4B00,
   0x4C00,
   0x4D00,
   0x4E00,
   0x4F00,
   0x4820,
   0x4920,
   0x4A20,
   0x4B20,
   0x4C20,
   0x4D20,
   0x4E20,
   0x4F20,
   0x4840,
   0x4940,
   0x4A40,
   0x4B40,
   0x4C40,
   0x4D40,
   0x4E40,
   0x4F40,
   0x4860,
   0x4960,
   0x4A60,
   0x4B60,
   0x4C60,
   0x4D60,
   0x4E60,
   0x4F60,
   0x4880,
   0x4980,
   0x4A80,
   0x4B80,
   0x4C80,
   0x4D80,
   0x4E80,
   0x4F80,
   0x48A0,
   0x49A0,
   0x4AA0,
   0x4BA0,
   0x4CA0,
   0x4DA0,
   0x4EA0,
   0x4FA0,
   0x48C0,
   0x49C0,
   0x4AC0,
   0x4BC0,
   0x4CC0,
   0x4DC0,
   0x4EC0,
   0x4FC0,
   0x48E0,
   0x49E0,
   0x4AE0,
   0x4BE0,
   0x4CE0,
   0x4DE0,
   0x4EE0,
   0x4FE0,
   0x5000,
   0x5100,
   0x5200,
   0x5300,
   0x5400,
   0x5500,
   0x5600,
   0x5700,
   0x5020,
   0x5120,
   0x5220,
   0x5320,
   0x5420,
   0x5520,
   0x5620,
   0x5720,
   0x5040,
   0x5140,
   0x5240,
   0x5340,
   0x5440,
   0x5540,
   0x5640,
   0x5740,
   0x5060,
   0x5160,
   0x5260,
   0x5360,
   0x5460,
   0x5560,
   0x5660,
   0x5760,
   0x5080,
   0x5180,
   0x5280,
   0x5380,
   0x5480,
   0x5580,
   0x5680,
   0x5780,
   0x50A0,
   0x51A0,
   0x52A0,
   0x53A0,
   0x54A0,
   0x55A0,
   0x56A0,
   0x57A0,
   0x50C0,
   0x51C0,
   0x52C0,
   0x53C0,
   0x54C0,
   0x55C0,
   0x56C0,
   0x57C0,
   0x50E0,
   0x51E0,
   0x52E0,
   0x53E0,
   0x54E0,
   0x55E0,
   0x56E0,
   0x57E0
};

// t_states is T states from first line pixel
// not total machine T states
//
// invoked from ports.c:readport()
//
int floating_bus(unsigned int t_states)
{
   // 224T per scan line (96T for border)
   int line = t_states / 224;
   int h  = ( t_states % 224 ) / 8;  // T per 2 columns
   int x, col;

   if ( h > 15 )	// if T*2 = 32 columns, starting from 0
      return 0xFF;	// not valid - ULA scanning border

   // get element pos
   // P1 A1 P2 A2 255 255 255 255
   x = ( t_states % 224 ) % 8;

   // check if to add +1 or not
   switch ( x )
   {
      case 3: // 0x5801 (A2)
      case 2: // 0x4001 (P2)
            col = h * 2 + 1;
            break;
      case 0: // 0x4000 (P1)
      case 1: // 0x5800 (A1)
            col = h * 2;
            break;
            
   }

   // if ULA scanning attributes
   // 0x5800 RAM address of attributes area
   if ( (x == 1) || (x == 3) )
      return readbyte( 0x5800 + line / 8 * 32 + col );

   // if ULA scanning pixel area
   if ( (x == 0) || (x == 2) )
      return readbyte( screen_lines[line] + col );

   // if ULA neither scanning attributes or pixel area
   return 0xFF;
}

