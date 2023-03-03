/*
--------------------------------------------------
    James William Fletcher (github.com/mrbid)
--------------------------------------------------
        MARCH 2023
        ~~~~~~~~~~~~~

    This is for Quake Champions.
    - Set enemy outline to "ON" and outline color to red.
    - Make sure anti-aliasing is OFF in VIDEO settings.
    
    Prereq:
    sudo apt install clang xterm libx11-dev libxdo-dev

*/

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <X11/Xutil.h>
#include <xdo.h>

// user configurable (if you really want to)
#define SCAN_DELAY 1000
const int sx=50, sy=50;

// other
#define uint unsigned int
xdo_t* xdo;
Display *d;
int si;
Window twin;
Window this_win = 0;
uint sps = 0;
int cx=0, cy=0;
uint64_t attack = 0;
uint minimal = 0;
uint enable = 0;
uint crosshair = 1;
uint hotkeys = 1;
uint autoshoot = 1;

/***************************************************
   ~~ Utils
*/
int key_is_pressed(KeySym ks)
{
    // https://www.cl.cam.ac.uk/~mgk25/ucs/keysymdef.h
    // https://stackoverflow.com/questions/18281412/check-keypress-in-c-on-linux/52801588
    char keys_return[32];
    XQueryKeymap(d, keys_return);
    KeyCode kc2 = XKeysymToKeycode(d, ks);
    int isPressed = !!(keys_return[kc2 >> 3] & (1 << (kc2 & 7)));
    return isPressed;
}
uint64_t microtime()
{
    struct timeval tv;
    struct timezone tz;
    memset(&tz, 0, sizeof(struct timezone));
    gettimeofday(&tv, &tz);
    return 1000000 * tv.tv_sec + tv.tv_usec;
}

/***************************************************
   ~~ X11 Utils
*/
Window getWindow(Display* d, const int si) // gets child window mouse is over
{
    XEvent event;
    memset(&event, 0x00, sizeof(event));
    XQueryPointer(d, RootWindow(d, si), &event.xbutton.root, &event.xbutton.window, &event.xbutton.x_root, &event.xbutton.y_root, &event.xbutton.x, &event.xbutton.y, &event.xbutton.state);
    event.xbutton.subwindow = event.xbutton.window;
    while(event.xbutton.subwindow)
    {
        event.xbutton.window = event.xbutton.subwindow;
        XQueryPointer(d, event.xbutton.window, &event.xbutton.root, &event.xbutton.subwindow, &event.xbutton.x_root, &event.xbutton.y_root, &event.xbutton.x, &event.xbutton.y, &event.xbutton.state);
    }
    return event.xbutton.window;
}
Window findWindow(Display *d, Window current, char const *needle)
{
    Window ret = 0, root, parent, *children;
    unsigned cc;
    char *name = NULL;

    if(current == 0)
        current = XDefaultRootWindow(d);

    if(XFetchName(d, current, &name) > 0)
    {
        if(strstr(name, needle) != NULL)
        {
            XFree(name);
            return current;
        }
        XFree(name);
    }

    if(XQueryTree(d, current, &root, &parent, &children, &cc) != 0)
    {
        for(uint i = 0; i < cc; ++i)
        {
            Window win = findWindow(d, children[i], needle);

            if(win != 0)
            {
                ret = win;
                break;
            }
        }
        XFree(children);
    }
    return ret;
}
#define _NET_WM_STATE_REMOVE 0
#define _NET_WM_STATE_ADD    1
#define _NET_WM_STATE_TOGGLE 2 
Bool MakeAlwaysOnTop(Display* display, Window root, Window mywin)
{
    // https://stackoverflow.com/a/16235920
    Atom wmStateAbove = XInternAtom(display, "_NET_WM_STATE_ABOVE", 1);
    if(wmStateAbove == None){return False;}
    Atom wmNetWmState = XInternAtom(display, "_NET_WM_STATE", 1);
    if(wmNetWmState == None){return False;}
    if(wmStateAbove != None)
    {
        XClientMessageEvent xclient;
        memset(&xclient, 0, sizeof(xclient));
        xclient.type = ClientMessage;
        xclient.window = mywin;
        xclient.message_type = wmNetWmState;
        xclient.format = 32;
        xclient.data.l[0] = _NET_WM_STATE_ADD;
        xclient.data.l[1] = wmStateAbove;
        xclient.data.l[2] = 0;
        xclient.data.l[3] = 0;
        xclient.data.l[4] = 0;
        XSendEvent(display, root, False, SubstructureRedirectMask|SubstructureNotifyMask, (XEvent*)&xclient);
        XFlush(display);
        return True;
    }
    return False;
}
Window getNextChild(Display* d, Window current)
{
    uint cc = 0;
    Window root, parent, *children;
    if(XQueryTree(d, current, &root, &parent, &children, &cc) == 0)
        return current;
    const Window rw = children[0];
    XFree(children);
    //printf("%lX\n", children[i]);
    return rw;
}

void targetEnemy()
{
    // get image block
    XImage *img = XGetImage(d, twin, cx-sx, cy-sy, sx*2, sy*2, AllPlanes, XYPixmap);
    if(img == NULL)
        return;

    // increment scans per second
    sps++;

    // pre-compute vars
    const float sy2 = sy*2;
    const float sx2 = sx*2;

    // top left detection
    int ax = 0, ay = 0;
    uint b1 = 0;
    for(int y = 0; y < sy2; y++)
    {
        for(int x = 0; x < sx2; x++)
        {
            const unsigned long pixel = XGetPixel(img, x, y);
            const unsigned char cr = (pixel & img->red_mask) >> 16;
            const unsigned char cg = (pixel & img->green_mask) >> 8;
            const unsigned char cb = pixel & img->blue_mask;
            //if(cr > 220 && cg < 33 && cb < 33)
            if(cr > 250 && cg < 5 && cb < 5)
            {
                ax = x, ay = y;
                b1 = 1;
                break;
            }
        }
        if(b1 == 1){break;}
    }

    // early exit
    if(b1 == 0)
    {
        XDestroyImage(img);
        return;
    }

    // bottom right detection
    int bx = 0, by = 0;
    uint b2 = 0;
    for(int y = sy2; y > 0; y--)
    {
        for(int x = sx2; x > 0; x--)
        {
            const unsigned long pixel = XGetPixel(img, x, y);
            const unsigned char cr = (pixel & img->red_mask) >> 16;
            const unsigned char cg = (pixel & img->green_mask) >> 8;
            const unsigned char cb = pixel & img->blue_mask;
            //if(cr > 220 && cg < 33 && cb < 33)
            if(cr > 250 && cg < 5 && cb < 5)
            {
                bx = x, by = y;
                b2 = 1;
                break;
            }
        }
        if(b2 == 1){break;}
    }

    // early exit
    if(b2 == 0)
    {
        XDestroyImage(img);
        return;
    }

    // target it
    if(b1 == 1 && b2 == 1)
    {
        // center on target
        const int dx = abs(ax-bx)/2;
        const int dy = abs(ay-by)/2;
        int mx = (ax-sx)+dx;
        int my = (ay-sy)+dy;
        
        // prevent the mouse jumping too large distances
        if(mx > sx){mx=sx;}
        if(my > sx){my=sx;}

        // only move the mouse if one of the mx or my is > 0
        if(mx != 0 || my != 0){xdo_move_mouse_relative(xdo, mx, my);}

        // attack!
        if(autoshoot == 1)
        {
            attack = microtime();
            xdo_mouse_down(xdo, CURRENTWINDOW, 1);
        }
    }

    // done
    XDestroyImage(img);
}

/***************************************************
   ~~ Program Entry Point
*/
void rainbow_line_printf(const char* text)
{
    static unsigned char base_clr = 0;
    if(base_clr == 0)
        base_clr = (rand()%125)+55;
    
    printf("\e[38;5;%im", base_clr);
    base_clr++;
    if(base_clr >= 230)
        base_clr = (rand()%125)+55;

    const uint len = strlen(text);
    for(uint i = 0; i < len; i++)
        printf("%c", text[i]);
    printf("\e[38;5;123m");
}
void reprint()
{
    system("clear");

    if(minimal == 0)
    {
        printf("\033[1m\033[0;31m>>> RedStrog Aimbot v1 by MegaStrog <<<\e[0m\n");
        rainbow_line_printf("original code by the loser below\n");
        rainbow_line_printf("James Cuckiam Fletcher (github.com/mrbid)\n\n");
        rainbow_line_printf("L-CTRL + L-ALT = Toggle BOT ON/OFF\n");
        rainbow_line_printf("R-CTRL + R-ALT = Toggle HOTKEYS ON/OFF\n");
        rainbow_line_printf("O = Toggle autoshoot.\n");
        rainbow_line_printf("P = Toggle crosshair.\n");
        rainbow_line_printf("H = Hold pressed to print scans per second.\n");
        rainbow_line_printf("\nDisable the game crosshair.\n");
        rainbow_line_printf("> If your monitor provides a crosshair that will work fine.\n");
        rainbow_line_printf("> OR just use the crosshair this bot provides.\n");
        printf("\e[38;5;76m");
        printf("\nSet enemy outline to \"ON\" and outline color to red.\nMake sure anti-aliasing is OFF in VIDEO settings.\n\n");
        printf("\e[38;5;123m");

        if(twin != 0)
        {
            printf("Quake Champions Win: 0x%lX\n\n", twin);

            if(enable == 1)
                rainbow_line_printf("BOT: \033[1m\e[32mON\e[0m\n");
            else
                rainbow_line_printf("BOT: \033[1m\e[31mOFF\e[0m\n");

            if(autoshoot == 1)
                rainbow_line_printf("AUTOSHOOT: \033[1m\e[32mON\e[0m\n");
            else
                rainbow_line_printf("AUTOSHOOT: \033[1m\e[31mOFF\e[0m\n");

            if(crosshair == 1)
                rainbow_line_printf("CROSSHAIR: \033[1m\e[32mON\e[0m\n");
            else
                rainbow_line_printf("CROSSHAIR: \033[1m\e[31mOFF\e[0m\n");

            if(hotkeys == 1)
                rainbow_line_printf("HOTKEYS: \033[1m\e[32mON\e[0m\n");
            else
                rainbow_line_printf("HOTKEYS: \033[1m\e[31mOFF\e[0m\n");

            printf("\n");
        }
    }
    else
    {
        if(twin != 0)
        {
            if(enable == 1)
                printf("\e[38;5;%umBOT: \033[1m\e[32mON\e[0m | ", minimal);
            else
                printf("\e[38;5;%umBOT: \033[1m\e[31mOFF\e[0m | ", minimal);

            if(autoshoot == 1)
                printf("\e[38;5;%umAUTOSHOOT: \033[1m\e[32mON\e[0m | ", minimal);
            else
                printf("\e[38;5;%umAUTOSHOOT: \033[1m\e[31mOFF\e[0m | ", minimal);

            if(crosshair == 1)
                printf("\e[38;5;%umCROSSHAIR: \033[1m\e[32mON\e[0m | ", minimal);
            else
                printf("\e[38;5;%umCROSSHAIR: \033[1m\e[31mOFF\e[0m | ", minimal);

            if(hotkeys == 1)
                printf("\e[38;5;%umHOTKEYS: \033[1m\e[32mON\e[0m ", minimal);
            else
                printf("\e[38;5;%umHOTKEYS: \033[1m\e[31mOFF\e[0m ", minimal);

            fflush(stdout);
        }
        else
        {
            printf("\e[38;5;123mPress \e[38;5;76mL-CTRL\e[38;5;123m + \e[38;5;76mL-ALT\e[38;5;123m to enable bot.\e[0m");
            fflush(stdout);
        }
    }
}
int main(int argc, char *argv[])
{
    srand(time(0));

    // is minimal ui?
    if(argc == 2)
    {
        minimal = atoi(argv[1]);
        if(minimal == 1){minimal = 8;}
        else if(minimal == 8){minimal = 1;}
    }

    // intro
    reprint();

    //

    GC gc = 0;
    time_t ct = time(0);

    //

    // try to open the default display
    d = XOpenDisplay(getenv("DISPLAY")); // explicit attempt on environment variable
    if(d == NULL)
    {
        d = XOpenDisplay((char*)NULL); // implicit attempt on environment variable
        if(d == NULL)
        {
            d = XOpenDisplay(":0"); // hedge a guess
            if(d == NULL)
            {
                printf("Failed to open display :'(\n");
                return 0;
            }
        }
    }

    // get default screen
    si = XDefaultScreen(d);

    //xdo
    xdo = xdo_new_with_opened_display(d, (char*)NULL, 0);
    if(minimal > 0)
    {
        xdo_get_active_window(xdo, &this_win);
        xdo_set_window_property(xdo, this_win, "WM_NAME", "RedStrog Aimbot V1");
        xdo_set_window_size(xdo, this_win, 600, 1, 0);
        MakeAlwaysOnTop(d, XDefaultRootWindow(d), this_win);
    }

    // get graphics context
    gc = DefaultGC(d, si);

    // find bottom window
    twin = findWindow(d, 0, "Quake Champions");
    if(twin != 0)
        reprint();
    
    while(1)
    {
        // loop every 1 ms (1,000 microsecond = 1 millisecond)
        usleep(SCAN_DELAY);

        // trigger timeout
        if(autoshoot == 1 && attack-microtime() > 333000)
            xdo_mouse_up(xdo, CURRENTWINDOW, 1);

        // inputs
        if(key_is_pressed(XK_Control_L) && key_is_pressed(XK_Alt_L))
        {
            if(enable == 0)
            {                
                // get window
                twin = findWindow(d, 0, "Quake Champions");
                if(twin != 0)
                    twin = getNextChild(d, twin);
                else
                    twin = getWindow(d, si);

                if(twin == 0)
                {
                    if(minimal == 0)
                    {
                        printf("Failed to detect a Quake Champions window.\n");
                    }
                    else
                    {
                        system("clear");
                        printf("Failed to detect a Quake Champions window.");
                        fflush(stdout);
                    }
                }

                // get center window point (x & y)
                XWindowAttributes attr;
                XGetWindowAttributes(d, twin, &attr);
                cx = attr.width/2;
                cy = attr.height/2;

                // toggle
                enable = 1;
                usleep(300000);
                reprint();
            }
            else
            {
                enable = 0;
                usleep(300000);
                reprint();
            }
        }
        
        // bot on/off
        if(enable == 1)
        {
            // always tracks sps
            static uint64_t st = 0;
            if(microtime() - st >= 1000000)
            {
                if(key_is_pressed(XK_H))
                {
                    if(minimal > 0){system("clear");}
                    printf("\e[36mSPS: %u\e[0m", sps);
                    if(minimal == 0){printf("\n");}else{fflush(stdout);}
                }
                sps = 0;
                st = microtime();
            }

            // input toggle
            if(key_is_pressed(XK_Control_R) && key_is_pressed(XK_Alt_R))
            {
                if(hotkeys == 0)
                {
                    hotkeys = 1;
                    usleep(300000);
                    reprint();
                }
                else
                {
                    hotkeys = 0;
                    usleep(300000);
                    reprint();
                }
            }

            // do hotkeys
            if(hotkeys == 1)
            {
                // crosshair toggle
                if(key_is_pressed(XK_P))
                {
                    if(crosshair == 0)
                    {
                        crosshair = 1;
                        usleep(300000);
                        reprint();
                    }
                    else
                    {
                        crosshair = 0;
                        usleep(300000);
                        reprint();
                    }
                }

                // autoshoot toggle
                if(key_is_pressed(XK_O))
                {
                    if(autoshoot == 0)
                    {
                        autoshoot = 1;
                        usleep(300000);
                        reprint();
                    }
                    else
                    {
                        autoshoot = 0;
                        usleep(300000);
                        reprint();
                    }
                }
            }

            // target
            targetEnemy();

            // crosshair
            if(crosshair == 1)
            {
                XSetForeground(d, gc, 65280);
                XDrawRectangle(d, twin, gc, cx-sx-1, cy-sy-1, (sx*2)+2, (sy*2)+2);
                XFlush(d);
            }
        }

        //
    }

    // done, never gets here in regular execution flow
    XCloseDisplay(d);
    return 0;
}

