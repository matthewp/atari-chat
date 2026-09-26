/*
 * atari-chat: talk to Claude from a GEM window.
 *
 * The window shows the conversation (word-wrapped, scrollable) above a
 * typing line. Return sends. The info line under the title shows what the
 * network code is doing, since a question can take a while.
 */
#include <gem.h>
#include <string.h>
#include <stdlib.h>
#include "config.h"
#include "menu.h"
#include "transcript.h"
#include "claude.h"

long _stksize = 65536;      /* the crypto code needs a big stack */

#define MAX_INPUT 1000
#define MARGIN 8

static short vdi_handle, win, cell_w, cell_h;
static OBJECT *menu;
static GRECT desk;

static char input[MAX_INPUT + 1];
static size_t input_len;
static int top_line;        /* first transcript line shown */
static int busy;

/* ---- geometry ---------------------------------------------------------- */

static void work_area(GRECT *r)
{
    wind_get_grect(win, WF_WORKXYWH, r);
}

/* Text area: everything above the input line. */
static void text_area(GRECT *r)
{
    work_area(r);
    r->g_x += MARGIN;
    r->g_w -= 2 * MARGIN;
    r->g_y += MARGIN / 2;
    r->g_h -= MARGIN / 2 + 2 * cell_h;
}

static int visible_lines(void)
{
    GRECT t;
    text_area(&t);
    return t.g_h > 0 ? t.g_h / cell_h : 0;
}

static int text_cols(void)
{
    GRECT t;
    text_area(&t);
    return t.g_w / cell_w;
}

static int max_top(void)
{
    int m = transcript_lines() - visible_lines();
    return m > 0 ? m : 0;
}

static void update_slider(void)
{
    int total = transcript_lines(), vis = visible_lines(), mt = max_top();
    short size = (total > vis && total > 0) ? (short)(1000L * vis / total) : 1000;
    short pos = mt ? (short)(1000L * top_line / mt) : 0;

    wind_set(win, WF_VSLSIZE, size, 0, 0, 0);
    wind_set(win, WF_VSLIDE, pos, 0, 0, 0);
}

static void clamp_top(void)
{
    if (top_line > max_top())
        top_line = max_top();
    if (top_line < 0)
        top_line = 0;
}

static void relayout(int stick_to_bottom)
{
    int was_bottom = top_line >= max_top();

    transcript_layout(text_cols());
    if (stick_to_bottom || was_bottom)
        top_line = max_top();
    clamp_top();
    update_slider();
}

/* ---- drawing ----------------------------------------------------------- */

static void open_vdi(void)
{
    short work_in[11] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2 };
    short work_out[57], attrib[10], box_w, box_h, char_w, char_h;

    vdi_handle = graf_handle(&char_w, &char_h, &box_w, &box_h);
    v_opnvwk(work_in, &vdi_handle, work_out);

    /* The real text cell (8x16 on a mono screen): what v_gtext advances by. */
    vqt_attributes(vdi_handle, attrib);
    cell_w = attrib[8];
    cell_h = attrib[9];
}

static void text_at(short x, short y, const char *s, size_t n)
{
    char buf[160];
    size_t i;

    if (n >= sizeof(buf))
        n = sizeof(buf) - 1;
    for (i = 0; i < n; i++)
        buf[i] = (s[i] == '\t') ? ' ' : s[i];
    buf[n] = '\0';
    v_gtext(vdi_handle, x, y, buf);
}

static void draw_content(const GRECT *clip)
{
    GRECT work, ta;
    short pxy[4], dummy;
    int i, vis, cols;
    size_t shown;

    work_area(&work);
    text_area(&ta);

    pxy[0] = clip->g_x;
    pxy[1] = clip->g_y;
    pxy[2] = clip->g_x + clip->g_w - 1;
    pxy[3] = clip->g_y + clip->g_h - 1;
    vs_clip(vdi_handle, 1, pxy);

    vswr_mode(vdi_handle, MD_REPLACE);
    vsf_interior(vdi_handle, FIS_SOLID);
    vsf_color(vdi_handle, G_WHITE);
    vsf_perimeter(vdi_handle, 0);
    v_bar(vdi_handle, pxy);

    vst_alignment(vdi_handle, TA_LEFT, TA_TOP, &dummy, &dummy);
    vst_color(vdi_handle, G_BLACK);

    /* The conversation. */
    vis = visible_lines();
    for (i = 0; i < vis; i++) {
        const tline *l = transcript_line(top_line + i);
        const message *m;
        short y = ta.g_y + i * cell_h;
        if (!l)
            break;
        if (l->msg < 0)
            continue;
        m = transcript_get(l->msg);
        if (l->first) {
            const char *label = m->role == ROLE_USER ? "You:"
                              : m->role == ROLE_ASSISTANT ? "Claude:" : "*";
            vst_effects(vdi_handle, 1);         /* bold */
            v_gtext(vdi_handle, ta.g_x, y, (char *)label);
            vst_effects(vdi_handle, 0);
        }
        vst_effects(vdi_handle, m->role == ROLE_NOTE ? 4 : 0);   /* notes in italics */
        text_at((short)(ta.g_x + LABEL_WIDTH * cell_w), y, m->text + l->start, l->len);
        vst_effects(vdi_handle, 0);
    }

    /* A rule, then the typing line, showing its tail if it's long. */
    {
        short line[4];
        short iy = work.g_y + work.g_h - cell_h - MARGIN / 2;
        line[0] = work.g_x;
        line[1] = line[3] = iy - MARGIN / 2;
        line[2] = work.g_x + work.g_w - 1;
        vsl_color(vdi_handle, G_BLACK);
        v_pline(vdi_handle, 2, line);

        cols = text_cols() + LABEL_WIDTH - 3;
        shown = input_len > (size_t)cols ? (size_t)cols : input_len;
        v_gtext(vdi_handle, ta.g_x, iy, busy ? "  " : "> ");
        text_at((short)(ta.g_x + 2 * cell_w), iy, input + input_len - shown, shown);
        if (!busy)
            v_gtext(vdi_handle, (short)(ta.g_x + (2 + shown) * cell_w), iy, "_");
    }

    vs_clip(vdi_handle, 0, pxy);
}

static void redraw(const GRECT *dirty)
{
    GRECT r;

    wind_update(BEG_UPDATE);
    graf_mouse(M_OFF, NULL);
    wind_get_grect(win, WF_FIRSTXYWH, &r);
    while (r.g_w && r.g_h) {
        if (rc_intersect(dirty, &r))
            draw_content(&r);
        wind_get_grect(win, WF_NEXTXYWH, &r);
    }
    graf_mouse(M_ON, NULL);
    wind_update(END_UPDATE);
}

static void redraw_all(void)
{
    GRECT w;
    work_area(&w);
    redraw(&w);
}

/* ---- talking to Claude ------------------------------------------------- */

static void set_status(const char *s)
{
    static char info[100];
    info[0] = ' ';
    strncpy(info + 1, s, sizeof(info) - 2);
    info[sizeof(info) - 1] = '\0';
    wind_set_str(win, WF_INFO, info);
}

static void send_question(void)
{
    char *reply;
    size_t reply_len;
    const char *err;
    char saved[MAX_INPUT + 1];

    if (input_len == 0 || busy)
        return;
    memcpy(saved, input, input_len + 1);
    transcript_add(ROLE_USER, input, input_len);
    input_len = 0;
    input[0] = '\0';
    busy = 1;
    relayout(1);
    redraw_all();

    graf_mouse(BUSYBEE, NULL);
    err = claude_ask(&reply, &reply_len);
    graf_mouse(ARROW, NULL);

    if (err) {
        /* Take the question back out (so the conversation stays valid)
           and return it to the typing line to try again. */
        transcript_remove_last();
        strcpy(input, saved);
        input_len = strlen(input);
        transcript_add(ROLE_NOTE, err, strlen(err));
        set_status("Something went wrong. Press Return to try again.");
    } else {
        transcript_add(ROLE_ASSISTANT, reply, reply_len);
        free(reply);
        set_status("Connected. Type your next question.");
    }
    busy = 0;
    relayout(1);
    redraw_all();
}

/* ---- input ------------------------------------------------------------- */

static void scroll_by(int lines)
{
    int old = top_line;
    top_line += lines;
    clamp_top();
    if (top_line != old) {
        update_slider();
        redraw_all();
    }
}

/* Returns 0 to quit. */
static int handle_key(short key)
{
    unsigned char ch = (unsigned char)(key & 0xff);
    unsigned char scan = (unsigned char)((key >> 8) & 0xff);

    if (ch == 0x11)                         /* Ctrl+Q */
        return 0;
    if (ch == 0) {                          /* special keys: scroll */
        if (scan == 0x48) scroll_by(-1);            /* up */
        else if (scan == 0x50) scroll_by(1);        /* down */
        else if (scan == 0x47) scroll_by(-transcript_lines());   /* Clr/Home */
        return 1;
    }
    if (ch == '\r') {
        send_question();
        return 1;
    }
    if (ch == '\b') {
        if (input_len > 0)
            input[--input_len] = '\0';
    } else if (ch == 0x1b) {                /* Esc clears the line */
        input_len = 0;
        input[0] = '\0';
    } else if (ch >= ' ' && ch != 0x7f && input_len < MAX_INPUT) {
        input[input_len++] = (char)ch;
        input[input_len] = '\0';
    } else {
        return 1;
    }
    /* Typing jumps back to the latest messages. */
    if (top_line != max_top()) {
        top_line = max_top();
        update_slider();
    }
    redraw_all();
    return 1;
}

static int handle_menu(short title, short item)
{
    int keep_running = 1;

    if (item == MENU_ABOUT)
        form_alert(1, "[1][atari-chat|Talk to Claude from your Atari ST.|"
                      "TLS, HTTP and JSON all run on|the 68000.][ OK ]");
    else if (item == MENU_QUIT)
        keep_running = 0;
    menu_tnormal(menu, title, 1);
    return keep_running;
}

static void handle_arrow(short how)
{
    int page = visible_lines() - 1;
    switch (how) {
    case WA_UPPAGE: scroll_by(-page); break;
    case WA_DNPAGE: scroll_by(page); break;
    case WA_UPLINE: scroll_by(-1); break;
    case WA_DNLINE: scroll_by(1); break;
    }
}

int main(void)
{
    static const char welcome[] =
        "Type a question and press Return. The first one takes about half a "
        "minute while the Atari sets up a secure connection; after that it's "
        "quicker. Up/Down scroll, Esc clears the line, Ctrl+Q quits.";
    short msg[8], events, mx, my, mb, ks, key, clicks;
    int running = 1;

    if (appl_init() < 0)
        return 1;
    open_vdi();
    menu = menu_build();
    menu_bar(menu, MENU_INSTALL);
    transcript_init();

    wind_get_grect(0, WF_WORKXYWH, &desk);
    win = wind_create_grect(NAME | CLOSER | MOVER | SIZER | FULLER | INFO
                            | UPARROW | DNARROW | VSLIDE, &desk);
    if (win < 0) {
        form_alert(1, "[3][Could not create a window.][ OK ]");
        menu_bar(menu, MENU_REMOVE);
        v_clsvwk(vdi_handle);
        appl_exit();
        return 1;
    }
    wind_set_str(win, WF_NAME, " atari-chat ");
    set_status("Starting...");
    wind_open_grect(win, &desk);
    transcript_add(ROLE_NOTE, welcome, strlen(welcome));
    relayout(1);

    busy = 1;
    graf_mouse(BUSYBEE, NULL);
    redraw_all();
    claude_init(set_status);
    graf_mouse(ARROW, NULL);
    busy = 0;
    set_status("Ready. Model: " AIG_MODEL);
    redraw_all();

    while (running) {
        events = evnt_multi(MU_MESAG | MU_KEYBD, 0, 0, 0, 0, 0, 0, 0, 0,
                            0, 0, 0, 0, 0, msg, 0, &mx, &my, &mb, &ks, &key, &clicks);

        if ((events & MU_KEYBD) && !handle_key(key))
            running = 0;

        if (events & MU_MESAG) {
            switch (msg[0]) {
            case MN_SELECTED:
                if (!handle_menu(msg[3], msg[4]))
                    running = 0;
                break;
            case WM_REDRAW:
                redraw((GRECT *)&msg[4]);
                break;
            case WM_TOPPED:
                wind_set(win, WF_TOP, 0, 0, 0, 0);
                break;
            case WM_MOVED:
                wind_set_grect(win, WF_CURRXYWH, (GRECT *)&msg[4]);
                break;
            case WM_SIZED:
                wind_set_grect(win, WF_CURRXYWH, (GRECT *)&msg[4]);
                relayout(0);
                redraw_all();
                break;
            case WM_FULLED: {
                GRECT cur, prev;
                wind_get_grect(win, WF_CURRXYWH, &cur);
                wind_get_grect(win, WF_PREVXYWH, &prev);
                wind_set_grect(win, WF_CURRXYWH,
                               (cur.g_w == desk.g_w && cur.g_h == desk.g_h) ? &prev : &desk);
                relayout(0);
                redraw_all();
                break;
            }
            case WM_ARROWED:
                handle_arrow(msg[4]);
                break;
            case WM_VSLID:
                top_line = (int)((long)msg[4] * max_top() / 1000);
                clamp_top();
                update_slider();
                redraw_all();
                break;
            case WM_CLOSED:
            case AP_TERM:
                running = 0;
                break;
            }
        }
    }

    set_status("Hanging up...");
    claude_disconnect();
    wind_close(win);
    wind_delete(win);
    menu_bar(menu, MENU_REMOVE);
    v_clsvwk(vdi_handle);
    appl_exit();
    return 0;
}
