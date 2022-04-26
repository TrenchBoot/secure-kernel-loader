/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <boot.h>
#include <types.h>

#ifdef DEBUG

static void print_char(char c)
{
    while ( !(inb(0x3f8 + 5) & 0x20) )
        ;

    outb(c, 0x3f8);
}

void print(const char * txt)
{
    while ( *txt != '\0' )
    {
        if ( *txt == '\n' )
            print_char('\r');
        print_char(*txt++);
    }
}

void print_p(const void * _p)
{
    char tmp[sizeof(void*)*2 + 5] = "0x";
    int i;
    size_t p = (size_t)_p;

    for ( i=0; i<sizeof(void*); i++ )
    {
        if ( (p & 0xf) >= 10 )
            tmp[sizeof(void*)*2 + 1 - 2*i] = (p & 0xf) + 'a' - 10;
        else
            tmp[sizeof(void*)*2 + 1 - 2*i] = (p & 0xf) + '0';
        p >>= 4;
        if ( (p & 0xf) >= 10 )
            tmp[sizeof(void*)*2 - 2*i] = (p & 0xf) + 'a' - 10;
        else
            tmp[sizeof(void*)*2 - 2*i] = (p & 0xf) + '0';
        p >>= 4;
    }
    tmp[sizeof(void*)*2 + 2] = ':';
    tmp[sizeof(void*)*2 + 3] = ' ';
    tmp[sizeof(void*)*2 + 4] = '\0';
    print(tmp);
}

void print_u64(u64 p)
{
    char tmp[sizeof(void*)*2 + 5] = "0x";
    int i;

    for ( i=0; i<sizeof(void*); i++ )
     {
        if ( (p & 0xf) >= 10 )
            tmp[sizeof(void*)*2 + 1 - 2*i] = (p & 0xf) + 'a' - 10;
        else
            tmp[sizeof(void*)*2 + 1 - 2*i] = (p & 0xf) + '0';
        p >>= 4;
        if ((p & 0xf) >= 10)
            tmp[sizeof(void*)*2 - 2*i] = (p & 0xf) + 'a' - 10;
        else
            tmp[sizeof(void*)*2 - 2*i] = (p & 0xf) + '0';
        p >>= 4;
    }
    tmp[sizeof(void*)*2 + 2] = ':';
    tmp[sizeof(void*)*2 + 3] = ' ';
    tmp[sizeof(void*)*2 + 4] = '\0';
    print(tmp);
}

static void print_b(char p)
{
    char tmp[4];

    if ( (p & 0xf) >= 10 )
        tmp[1] = (p & 0xf) + 'a' - 10;
    else
        tmp[1] = (p & 0xf) + '0';
    p >>= 4;
    if ( (p & 0xf) >= 10 )
        tmp[0] = (p & 0xf) + 'a' - 10;
    else
        tmp[0] = (p & 0xf) + '0';

    tmp[2] = ' ';
    tmp[3] = '\0';
    print(tmp);
}

static inline int isprint(int c)
{
    return c >= ' ' && c <= '~';
}

void hexdump(const void *memory, size_t length)
{
    int i;
    u8 *line;
    int all_zero = 0;
    int all_one = 0;
    size_t num_bytes;

    for ( i = 0; i < length; i += 16 )
    {
        int j;
        num_bytes = 16;
        line = ((u8 *)memory) + i;

        all_zero++;
        all_one++;
        for ( j = 0; j < num_bytes; j++ )
        {
            if ( line[j] != 0 )
            {
                all_zero = 0;
                break;
            }
        }

        for ( j = 0; j < num_bytes; j++ )
        {
            if ( line[j] != 0xff )
            {
                all_one = 0;
                break;
            }
        }

        if ( (all_zero < 2) && (all_one < 2) )
        {
            print_p(memory + i);
            for ( j = 0; j < num_bytes; j++ )
                print_b(line[j]);
            for ( ; j < 16; j++ )
                print("   ");
            print("  ");
            for ( j = 0; j < num_bytes; j++ )
                isprint(line[j]) ? print_char(line[j]) : print_char('.');
            print("\n");
        }
        else if ( (all_zero == 2) || (all_one == 2) )
        {
            print("...\n");
        }
    }
}

#endif /* DEBUG */
