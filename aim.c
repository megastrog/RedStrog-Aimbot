/*
--------------------------------------------------
    James William Fletcher (github.com/mrbid)
--------------------------------------------------
        MARCH 2023
        ~~~~~~~~~~

    This is for Quake Champions.
    - Set enemy outline to "ON" and outline color to red.
    - Make sure anti-aliasing is OFF in VIDEO settings.
    - Set mouse sensitivity to 1 with 0 acceleration. (you can adjust from here for fine tuning)

    With mouse sensitivity I usually have my gaming mouse on the highest DPI
    so a slow sensivitity of 1 is not really an issue for me. If you do not have
    a high DPS mouse and wish to play with a higher sensitivity you can try to
    reduce the "mouse_scaler" value.

    With MOUSE_SCALER_ENABLED defined, MOUSE4/3/LSHIFT becomes a "rate unlimited" trigger.
    
    Prereq:
    sudo apt install clang xterm espeak libx11-dev libxdo-dev

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

#include <pthread.h>
#include <fcntl.h>

// user configurable
#ifdef PTS
    #define COLOR_DETECT cr > 220 && cg < 70 && cb < 70 // this will be for when the PTS goes live
#else
    #define COLOR_DETECT cr > 250 && cg < 3 && cb < 3
#endif
#define SCAN_DELAY_NS 1000
#define OPTION_DELAY_MS 300000
//#define ENABLE_SHIFT // this will disable MOUSE4&3 for L&R SHIFT
#define ENABLE_MOUSE_SCALER
#ifdef ENABLE_MOUSE_SCALER
    float mouse_scaler;
    float mousescale_small = 1.f;
    float mousescale_large = 2.f;
#endif
uint64_t MOUSE_UPDATE_NS = 16000;

//#define EFFICIENT_SCAN // uncommenting this will increase the SPS rate of the triggerbot

// other
#define uint unsigned int
xdo_t* xdo;
Display *d;
int si;
Window twin;
Window this_win = 0;
int cx=0, cy=0;
uint minimal = 0;
uint enable = 0;
uint crosshair = 1;
uint hotkeys = 1;
uint autoshoot = 0;
uint triggerbot = 0;
uint sps = 0;
uint64_t attack = 0;
int sd=100;
int sd2=200;
int tsd=28;
int tsd2=14;

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
void speakS(const char* text)
{
    char s[256];
    sprintf(s, "/usr/bin/espeak -s 240 \"%s\"", text);
    system(s);
}

/***************************************************
   ~~ X11 Utils
*/
int left=0, right=0, middle=0, four=0;
void *mouseThread(void *arg)
{
    int fd = open("/dev/input/mice", O_RDWR);
    if(fd == -1)
    {
        printf("Failed to open '/dev/input/mice' mouse input is non-operable.\n");
        return 0;
    }
    unsigned char data[3];
    while(1)
    {
        int bytes = read(fd, data, sizeof(data));
        if(bytes > 0)
        {
            left = data[0] & 0x1;
            right = data[0] & 0x2;
            middle = data[0] & 0x4;
            four = data[0] & 0x5;
        }
    }
    close(fd);
}
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
    XImage *img = XGetImage(d, twin, cx-sd, cy-sd, sd2, sd2, AllPlanes, XYPixmap);
    if(img == NULL)
        return;
    
    // increment scans per second
    sps++;

    // top left detection
    int ax = 0, ay = 0;
    uint b1 = 0;
    for(int y = 0; y < sd2; y++)
    {
        for(int x = 0; x < sd2; x++)
        {
            const unsigned long pixel = XGetPixel(img, x, y);
            const unsigned char cr = (pixel & img->red_mask) >> 16;
            const unsigned char cg = (pixel & img->green_mask) >> 8;
            const unsigned char cb = pixel & img->blue_mask;
            if(COLOR_DETECT)
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
    for(int y = sd2; y > 0; y--)
    {
        for(int x = sd2; x > 0; x--)
        {
            const unsigned long pixel = XGetPixel(img, x, y);
            const unsigned char cr = (pixel & img->red_mask) >> 16;
            const unsigned char cg = (pixel & img->green_mask) >> 8;
            const unsigned char cb = pixel & img->blue_mask;
            if(COLOR_DETECT)
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
        const int ady = abs(ay-by);
        const int dy = ady/2;
        int mx = (ax-sd)+dx;
        int my = (ay-sd)+dy;
#ifdef ENABLE_MOUSE_SCALER
        mx = (int)(((float)mx)*mouse_scaler);
        my = (int)(((float)my)*mouse_scaler);
#endif

        // resize scan window
        if(ady > 6 && ady < 100)
        {
            sd  = ady;
            sd2 = sd*2;
        }

        // only move the mouse if one of the mx or my is > 0
#ifdef ENABLE_MOUSE_SCALER
        static uint64_t lt = 0;
        if((mx != 0 || my != 0) && microtime()-lt > MOUSE_UPDATE_NS) // limited to 60hz updates
        {
            xdo_move_mouse_relative(xdo, mx, my);
            lt = microtime();
        }
#else
        if(mx != 0 || my != 0){xdo_move_mouse_relative(xdo, mx, my);}
#endif

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
uint isEnemy()
{
    // get image block
    XImage *img = XGetImage(d, twin, cx-tsd2, cy-tsd2, tsd, tsd, AllPlanes, XYPixmap);
    if(img == NULL)
        return 0;
    
    // increment scans per second
    sps++;

    // extract colour information
    uint c1 = 0;
    for(int y = 0; y < tsd2; y++)
    {
        for(int x = 0; x < tsd2; x++)
        {
            const unsigned long pixel = XGetPixel(img, x, y);
            const unsigned char cr = (pixel & img->red_mask) >> 16;
            const unsigned char cg = (pixel & img->green_mask) >> 8;
            const unsigned char cb = pixel & img->blue_mask;
            if(COLOR_DETECT)
            {
                c1=1;
                break;
            }
        }
        if(c1 == 1){break;}
    }
#ifdef EFFICIENT_SCAN
    if(c1 == 0)
    {
        XDestroyImage(img);
        return 0;
    }
#endif
    uint c2 = 0;
    for(int y = tsd2; y < tsd; y++)
    {
        for(int x = 0; x < tsd2; x++)
        {
            const unsigned long pixel = XGetPixel(img, x, y);
            const unsigned char cr = (pixel & img->red_mask) >> 16;
            const unsigned char cg = (pixel & img->green_mask) >> 8;
            const unsigned char cb = pixel & img->blue_mask;
            if(COLOR_DETECT)
            {
                c2=1;
                break;
            }
        }
        if(c2 == 1){break;}
    }
#ifdef EFFICIENT_SCAN
    if(c2 == 0)
    {
        XDestroyImage(img);
        return 0;
    }
#endif
    uint c3 = 0;
    for(int y = 0; y < tsd2; y++)
    {
        for(int x = tsd2; x < tsd; x++)
        {
            const unsigned long pixel = XGetPixel(img, x, y);
            const unsigned char cr = (pixel & img->red_mask) >> 16;
            const unsigned char cg = (pixel & img->green_mask) >> 8;
            const unsigned char cb = pixel & img->blue_mask;
            if(COLOR_DETECT)
            {
                c3=1;
                break;
            }
        }
        if(c3 == 1){break;}
    }
#ifdef EFFICIENT_SCAN
    if(c3 == 0)
    {
        XDestroyImage(img);
        return 0;
    }
#endif
    uint c4 = 0;
    for(int y = tsd2; y < tsd; y++)
    {
        for(int x = tsd2; x < tsd; x++)
        {
            const unsigned long pixel = XGetPixel(img, x, y);
            const unsigned char cr = (pixel & img->red_mask) >> 16;
            const unsigned char cg = (pixel & img->green_mask) >> 8;
            const unsigned char cb = pixel & img->blue_mask;
            if(COLOR_DETECT)
            {
                c4=1;
                break;
            }
        }
        if(c4 == 1){break;}
    }
#ifdef EFFICIENT_SCAN
    if(c4 == 0)
    {
        XDestroyImage(img);
        return 0;
    }
#endif

#ifndef EFFICIENT_SCAN
    if(c1+c2+c3+c4 < 3)
        return 0;
#endif

    // done
    XDestroyImage(img);
    return 1;
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
#ifdef PTS
        printf("\033[1m\033[0;31m>>> RedStrog Aimbot v3 PTS by MegaStrog <<<\e[0m\n");
#else
        printf("\033[1m\033[0;31m>>> RedStrog Aimbot v3 by MegaStrog <<<\e[0m\n");
#endif
        rainbow_line_printf("original code by the loser below\n");
        rainbow_line_printf("James Cuckiam Fletcher (github.com/mrbid)\n\n");
        rainbow_line_printf("L-CTRL + L-ALT = Toggle BOT ON/OFF\n");
        rainbow_line_printf("R-CTRL + R-ALT = Toggle HOTKEYS ON/OFF\n");
        rainbow_line_printf("MOUSE1 = Target enemy (rate limit).\n");
        rainbow_line_printf("MOUSE4/3/LSHIFT = Target enemy (rate unlimited).\n");
        rainbow_line_printf("U = Speak whats enabled.\n");
        rainbow_line_printf("I = Toggle autoshoot.\n");
        rainbow_line_printf("O = Toggle triggerbot.\n");
        rainbow_line_printf("P = Toggle crosshair.\n");
        rainbow_line_printf("H = Hold pressed to print scans per second.\n");
        rainbow_line_printf("\nDisable the game crosshair.\n");
        rainbow_line_printf("> If your monitor provides a crosshair that will work fine.\n");
        rainbow_line_printf("> OR just use the crosshair this bot provides.\n");
        printf("\e[38;5;76m");
        printf("\nSet enemy outline to \"ON\" and outline color to red.\nMake sure anti-aliasing is OFF in VIDEO settings.\nSet mouse sensitivity to 1 with 0 acceleration.\n\n");
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

            if(triggerbot == 1)
                rainbow_line_printf("TRIGGERBOT: \033[1m\e[32mON\e[0m\n");
            else
                rainbow_line_printf("TRIGGERBOT: \033[1m\e[31mOFF\e[0m\n");

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

            if(triggerbot == 1)
                printf("\e[38;5;%umTRIGGERBOT: \033[1m\e[32mON\e[0m | ", minimal);
            else
                printf("\e[38;5;%umTRIGGERBOT: \033[1m\e[31mOFF\e[0m | ", minimal);

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
    mouse_scaler = mousescale_large;

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
    time_t launch_time = 0;
    if(minimal > 0){launch_time = time(0);}

    // get graphics context
    gc = DefaultGC(d, si);

    // find bottom window
    twin = findWindow(d, 0, "Quake Champions");
    if(twin != 0)
        reprint();


    // start mouse thread
    pthread_t tid;
    if(pthread_create(&tid, NULL, mouseThread, NULL) != 0)
    {
        pthread_detach(tid);
        printf("pthread_create(mouseThread) failed.\n");
        return 0;
    }
    
    // begin bot
    while(1)
    {
        // loop every 1 ms (1,000 microsecond = 1 millisecond)
        usleep(SCAN_DELAY_NS);

        // do minimal?
        if(launch_time != 0 && time(0)-launch_time >= 1)
        {
            xdo_get_active_window(xdo, &this_win);
#ifdef PTS
            xdo_set_window_property(xdo, this_win, "WM_NAME", "RedStrog Aimbot V3 PTS");
#else
            xdo_set_window_property(xdo, this_win, "WM_NAME", "RedStrog Aimbot V3");
#endif
            xdo_set_window_size(xdo, this_win, 600, 1, 0);
            MakeAlwaysOnTop(d, XDefaultRootWindow(d), this_win);
            launch_time = 0;
        }

        // trigger timeout
        if(autoshoot == 1 && attack-microtime() > 333000)
        {
            xdo_mouse_up(xdo, CURRENTWINDOW, 1);
            sd = 100, sd2 = 200;
        }
        else if(left == 0)
        {
            sd = 100, sd2 = 200; // reset scan
        }

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
                usleep(OPTION_DELAY_MS);
                reprint();
            }
            else
            {
                enable = 0;
                usleep(OPTION_DELAY_MS);
                reprint();
            }
        }
        
        // bot on/off
        if(enable == 1)
        {
            // always tracks sps
            const int spson = key_is_pressed(XK_H);
            static uint64_t st = 0;
            if(microtime() - st >= 1000000)
            {
                if(spson == 1)
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
                    usleep(OPTION_DELAY_MS);
                    reprint();
                }
                else
                {
                    hotkeys = 0;
                    usleep(OPTION_DELAY_MS);
                    reprint();
                }
            }

            // do hotkeys
            if(hotkeys == 1)
            {
                // speak enabled
                if(key_is_pressed(XK_U))
                {
                    if(triggerbot == 1){speakS("triggerbot");}
                    else if(autoshoot == 1){speakS("autoshoot");}
                    else{speakS("base");}
                }

                // autoshoot toggle
                if(key_is_pressed(XK_I))
                {
                    if(autoshoot == 0)
                    {
                        autoshoot = 1;
                        usleep(OPTION_DELAY_MS);
                        reprint();
                    }
                    else
                    {
                        autoshoot = 0;
                        usleep(OPTION_DELAY_MS);
                        reprint();
                    }
                }

                // triggerbot toggle
                if(key_is_pressed(XK_O))
                {
                    if(triggerbot == 0)
                    {
                        triggerbot = 1;
                        usleep(OPTION_DELAY_MS);
                        reprint();
                    }
                    else
                    {
                        triggerbot = 0;
                        usleep(OPTION_DELAY_MS);
                        reprint();
                    }
                }

                // crosshair toggle
                if(key_is_pressed(XK_P))
                {
                    if(crosshair == 0)
                    {
                        crosshair = 1;
                        usleep(OPTION_DELAY_MS);
                        reprint();
                    }
                    else
                    {
                        crosshair = 0;
                        usleep(OPTION_DELAY_MS);
                        reprint();
                    }
                }
            }

            if(triggerbot == 1)
            {
                if(four >= 4){tsd=14,tsd2=7;}else{tsd=28,tsd2=14;} // scale flippening
                if(isEnemy() == 1)
                {
                    xdo_mouse_down(xdo, CURRENTWINDOW, 1);
                    usleep(10000); // or cheating ban
                    xdo_mouse_up(xdo, CURRENTWINDOW, 1);
                }
            }
            else
            {
                // target
#ifdef ENABLE_MOUSE_SCALER
                //printf("%li - %i\n", MOUSE_UPDATE_NS, four);
#ifdef ENABLE_SHIFT
                const int isp = key_is_pressed(XK_Shift_L) || key_is_pressed(XK_Shift_R);
#else
                const int isp = four > 0;
#endif
                if(isp){MOUSE_UPDATE_NS = 0;mouse_scaler=mousescale_small;sd=50,sd2=100;}else{MOUSE_UPDATE_NS=16000;mouse_scaler=mousescale_large;} // MOUSE4 = Super Accuracy
#endif
                if(spson == 1 || left == 1 || autoshoot == 1 || isp)
                    targetEnemy();
            }

            // crosshair
            if(crosshair == 1)
            {
                XSetForeground(d, gc, 65280);
                if(triggerbot == 1)
                    XDrawRectangle(d, twin, gc, cx-tsd2-1, cy-tsd2-1, tsd+2, tsd+2);
                else
                    XDrawRectangle(d, twin, gc, cx-sd-1, cy-sd-1, sd2+2, sd2+2);
                XFlush(d);
            }
        }

        //
    }

    // done, never gets here in regular execution flow
    XCloseDisplay(d);
    return 0;
}

