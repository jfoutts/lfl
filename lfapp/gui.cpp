/*
 * $Id: gui.cpp 1336 2014-12-08 09:29:59Z justin $
 * Copyright (C) 2009 Lucid Fusion Labs

 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "lfapp/lfapp.h"
#include "lfapp/dom.h"
#include "lfapp/css.h"
#include "lfapp/flow.h"
#include "lfapp/gui.h"
#include "lfapp/ipc.h"
#include "../crawler/html.h"
#include "../crawler/document.h"

#ifndef _WIN32
#include <sys/ioctl.h>
#include <termios.h>
#endif

#ifdef LFL_QT
#include <QWindow>
#include <QWebView>
#include <QWebFrame>
#include <QWebElement>
#include <QMessageBox>
#include <QMouseEvent>
#include <QApplication>
#endif

#ifdef LFL_BERKELIUM
#include "berkelium/Berkelium.hpp"
#include "berkelium/Window.hpp"
#include "berkelium/WindowDelegate.hpp"
#include "berkelium/Context.hpp"
#endif

namespace LFL {
#if defined(LFL_ANDROID) || defined(LFL_IPHONE)
DEFINE_bool(multitouch, true, "Touchscreen controls");
#else
DEFINE_bool(multitouch, false, "Touchscreen controls");
#endif
DEFINE_string(console_font, "", "Console font, blank for default_font");
DEFINE_bool(draw_grid, false, "Draw lines intersecting mouse x,y");

Window::WindowMap Window::active;
Window *Window::Get(void *id) { return FindOrNull(Window::active, id); }

Window::Window() : caption("lfapp"), fps(128) {
    id = gl = surface = glew_context = user1 = user2 = user3 = 0;
    minimized = cursor_grabbed = 0;
    target_fps = FLAGS_target_fps;
    opengles_version = 1;
    opengles_cubemap = 0;
    width = 640; height = 480;
    multitouch_keyboard_x = .93; 
    cam = new Entity(v3(5.54, 1.70, 4.39), v3(-.51, -.03, -.49), v3(-.03, 1, -.03));
    ClearEvents();
    ClearGesture();
}

Window::~Window() {
    if (console) {
        console->WriteHistory(LFAppDownloadDir(), "console");
        delete console;
    }
    delete cam;
}

void Window::ClearMouseGUIEvents() {
    for (auto i = mouse_gui.begin(); i != mouse_gui.end(); ++i) (*i)->ClearEvents();
}
void Window::ClearKeyboardGUIEvents() {
    for (auto i = keyboard_gui.begin(); i != keyboard_gui.end(); ++i) (*i)->ClearEvents();
}
void Window::ClearInputBindEvents() {
    for (auto i = input_bind.begin(); i != input_bind.end(); ++i) (*i)->ClearEvents();
}

void Window::ClearEvents() { 
    memzero(events);
    ClearMouseGUIEvents();
    ClearKeyboardGUIEvents();
    ClearInputBindEvents();
}

void Window::ClearGesture() {
    gesture_swipe_up = gesture_swipe_down = 0;
    gesture_tap[0] = gesture_tap[1] = gesture_dpad_stop[0] = gesture_dpad_stop[1] = 0;
    gesture_dpad_dx[0] = gesture_dpad_dx[1] = gesture_dpad_dy[0] = gesture_dpad_dy[1] = 0;
}

void Window::InitConsole() {
    console = new Console(screen, Fonts::Get(A_or_B(FLAGS_console_font, FLAGS_default_font), "", 9));
    console->ReadHistory(LFAppDownloadDir(), "console");
    console->Write(StrCat(screen->caption, " started"));
    console->Write("Try console commands 'cmds' and 'flags'");
}

void Window::DrawDialogs() {
    if (screen->console) screen->console->Draw();
    if (FLAGS_draw_grid) {
        Color c(.7, .7, .7);
        glIntersect(screen->mouse.x, screen->mouse.y, &c);
        Fonts::Default()->Draw(StrCat("draw_grid ", screen->mouse.x, " , ", screen->mouse.y), point(0,0));
    }
    for (auto i = screen->dialogs.rbegin(); i != screen->dialogs.rend(); ++i) (*i)->Draw();
}

void KeyboardGUI::AddHistory(const string &cmd) {
    lastcmd.ring.PushBack(1);
    lastcmd[(lastcmd_ind = -1)] = cmd;
}

int KeyboardGUI::WriteHistory(const string &dir, const string &name, const string &hdr) {
    if (!lastcmd.Size()) return 0;
    LocalFile history(dir + MatrixFile::Filename(name, "history", "string", 0), "w");
    MatrixFile::WriteHeader(&history, BaseName(history.fn), hdr, lastcmd.ring.count, 1);
    for (int i=0; i<lastcmd.ring.count; i++) StringFile::WriteRow(&history, lastcmd[-1-i]);
    return 0;
}

int KeyboardGUI::ReadHistory(const string &dir, const string &name) {
    StringFile history;
    VersionedFileName vfn(dir.c_str(), name.c_str(), "history");
    if (history.ReadVersioned(vfn) < 0) { ERROR("missing ", name); return -1; }
    for (int i=0, l=history.Lines(); i<l; i++) AddHistory((*history.F)[l-1-i]);
    return 0;
}

int TextGUI::Line::Erase(int x, int l) {
    if (!(l = max(0, min(Size() - x, l)))) return 0;
    bool token_processing = parent->token_processing;
    LineTokenProcessor update(token_processing ? this : 0, x, DrawableBoxRun(&data->glyphs[x], l), l);
    if (token_processing) {
        update.SetNewLineBoundaryConditions(!x ? update.nw : update.lbw, x + l == update.line_size ? update.pw : update.lew);
        update.ProcessUpdate();
    }
    data->glyphs.Erase(x, l, true);
    data->flow.p.x = data->glyphs.Position(data->glyphs.Size()).x;
    if (update.nw) update.ni -= l;
    if (token_processing) update.ProcessResult();
    return l;
}

template <class X> int TextGUI::Line::InsertTextAt(int x, const StringPieceT<X> &v, int attr) {
    DrawableBoxArray b;
    b.attr.source = data->glyphs.attr.source;
    EncodeText(&b, data->glyphs.Position(x).x, v, attr);
    int ret = b.Size();

    bool token_processing = parent->token_processing, append = x == Size();
    LineTokenProcessor update(token_processing ? this : 0, x, DrawableBoxRun(&b[0], ret), 0);
    if (token_processing) {
        update.SetNewLineBoundaryConditions(!x ? update.sw : update.lbw, x == update.line_size-1 ? update.ew : update.lew);
        update.ProcessResult();
    }

    data->glyphs.InsertAt(x, b);
    data->flow.p.x = data->glyphs.Position(data->glyphs.Size()).x;
    if (!append && update.nw) update.ni += ret;
    
    if (token_processing) update.ProcessUpdate();
    return ret;
}

template <class X> int TextGUI::Line::OverwriteTextAt(int x, const StringPieceT<X> &v, int attr) {
    int size = Size(), pad = max(0, x + v.len - size), grow = 0;
    if (pad) data->flow.AppendText(basic_string<X>(pad, ' '), attr);

    DrawableBoxArray b;
    b.attr.source = data->glyphs.attr.source;
    EncodeText(&b, data->glyphs.Position(x).x, v, attr);
    if ((size = b.Size()) && size - v.len > 0 && (grow = max(0, x + size - Size())))
        data->flow.AppendText(basic_string<X>(grow, ' '), attr);
    DrawableBoxRun orun(&data->glyphs[x], size), nrun(&b[0], size);

    bool token_processing = parent->token_processing;
    LineTokenProcessor update(token_processing ? this : 0, x, orun, size);
    if (token_processing) {
        update.FindBoundaryConditions(nrun, &update.osw, &update.oew);
        update.SetNewLineBoundaryConditions(!x ? update.osw : update.lbw, x + size == update.line_size ? update.oew : update.lew);
        update.ProcessUpdate();
    }
    data->glyphs.OverwriteAt(x, b.data);
    data->flow.p.x = data->glyphs.Position(data->glyphs.Size()).x;
    if (token_processing) {
        update.PrepareOverwrite(nrun);
        update.ProcessUpdate();
    }
    return size;
}

template <class X> int TextGUI::Line::UpdateText(int x, const StringPieceT<X> &v, int attr, int max_width, bool *append_out, int mode) {
    bool append = 0, insert_mode = mode == -1 ? parent->insert_mode : mode;
    int size = Size(), ret = 0;
    if (insert_mode) {
        if (size < x)                 data->flow.AppendText(basic_string<X>(x - size, ' '), attr);
        if ((append = (Size() == x))) ret  = AppendText  (   v, attr);
        else                          ret  = InsertTextAt(x, v, attr);
        if (max_width)                       Erase(max_width);
    } else {
        data->flow.cur_attr.font = parent->font;
        ret = OverwriteTextAt(x, v, attr);
    }
    if (append_out) *append_out = append;
    return ret;
}

template int TextGUI::Line::UpdateText<char>   (int x, const StringPiece   &v, int attr, int max_width, bool *append, int);
template int TextGUI::Line::UpdateText<short>  (int x, const String16Piece &v, int attr, int max_width, bool *append, int);
template int TextGUI::Line::InsertTextAt<char> (int x, const StringPiece   &v, int attr);
template int TextGUI::Line::InsertTextAt<short>(int x, const String16Piece &v, int attr);

void TextGUI::Line::Layout(Box win, bool flush) {
    if (data->box.w == win.w && !flush) return;
    data->box = win;
    ScopedDeltaTracker<int> SWLT(cont ? &cont->wrapped_lines : 0, bind(&Line::Lines, this));
    DrawableBoxArray b;
    swap(b, data->glyphs);
    data->glyphs.attr.source = b.attr.source;
    Clear();
    data->flow.AppendBoxArrayText(b);
}

point TextGUI::Line::Draw(point pos, int relayout_width, int g_offset, int g_len) {
    if (relayout_width >= 0) Layout(relayout_width);
    data->glyphs.Draw((p = pos), g_offset, g_len);
    return p - point(0, parent->font->Height() + data->glyphs.height);
}

TextGUI::LineTokenProcessor::LineTokenProcessor(TextGUI::Line *l, int o, const DrawableBoxRun &V, int Erase)
    : L(l), x(o), line_size(L?L->Size():0), erase(Erase) {
    if (!L) return;
    const DrawableBoxArray &glyphs = L->data->glyphs;
    const Drawable *p=0, *n=0;
    CHECK_LE(x, line_size);
    LoadV(V);
    ni = x + (Erase ? Erase : 0);
    nw = ni<line_size && (n=glyphs[ni ].drawable) && !isspace(n->Id());
    pw = x >0         && (p=glyphs[x-1].drawable) && !isspace(p->Id());
    pi = x - pw;
    if ((pw && nw) || (pw && sw)) FindPrev(glyphs);
    if ((pw && nw) || (nw && ew)) FindNext(glyphs);
    FindBoundaryConditions(DrawableBoxRun(&glyphs[0], line_size), &lbw, &lew);
}

void TextGUI::LineTokenProcessor::ProcessUpdate() {
    int tokens = 0, vl = v.Size();
    if (!vl) return;

    StringWordIterT<DrawableBox> word(v.data.buf, v.data.len, isspace, 0);
    for (const DrawableBox *w = word.Next(); w; w = word.Next(), tokens++) {
        int start_offset = w - v.data.buf, end_offset = start_offset + word.cur_len;
        bool first = start_offset == 0, last = end_offset == v.data.len;
        if (first && last && pw && nw) L->parent->UpdateToken(L, pi, ni-pi+1,                             erase ? -1 : 1, this);
        else if (first && pw)          L->parent->UpdateToken(L, pi, x+end_offset-pi,                     erase ? -2 : 2, this);
        else if (last && nw)           L->parent->UpdateToken(L, x+start_offset, ni-x-start_offset+1,     erase ? -3 : 3, this);
        else                           L->parent->UpdateToken(L, x+start_offset, end_offset-start_offset, erase ? -4 : 4, this);
    }
    if ((!tokens || overwrite) && vl) {
        const DrawableBoxArray &glyphs = L->data->glyphs;
        if (pw && !sw && osw) { FindPrev(glyphs); L->parent->UpdateToken(L, pi, x-pi,        erase ? -5 : 5, this); }
        if (nw && !ew && oew) { FindNext(glyphs); L->parent->UpdateToken(L, x+vl, ni-x-vl+1, erase ? -6 : 6, this); }
    }
}

void TextGUI::LineTokenProcessor::ProcessResult() {
    const DrawableBoxArray &glyphs = L->data->glyphs;
    if      (pw && nw) L->parent->UpdateToken(L, pi, ni - pi + 1, erase ? 7 : -7, this);
    else if (pw && sw) L->parent->UpdateToken(L, pi, x  - pi,     erase ? 8 : -8, this);
    else if (nw && ew) L->parent->UpdateToken(L, x,  ni - x + 1,  erase ? 9 : -9, this);
}

TextGUI::LinesFrameBuffer *TextGUI::LinesFrameBuffer::Attach(TextGUI::LinesFrameBuffer **last_fb) {
    if (*last_fb != this) fb.Attach();
    return *last_fb = this;
}

bool TextGUI::LinesFrameBuffer::SizeChanged(int W, int H, Font *font) {
    lines = H / font->Height();
    return RingFrameBuffer::SizeChanged(W, H, font);
}

void TextGUI::LinesFrameBuffer::Update(TextGUI::Line *l, int flag) {
    if (!(flag & Flag::NoLayout)) l->Layout(wrap ? w : 0, flag & Flag::Flush);
    RingFrameBuffer::Update(l, Box(w, l->Lines() * font_height), paint_cb, true);
}

int TextGUI::LinesFrameBuffer::PushFrontAndUpdate(TextGUI::Line *l, int xo, int wlo, int wll, int flag) {
    if (!(flag & Flag::NoLayout)) l->Layout(wrap ? w : 0, flag & Flag::Flush);
    int wl = max(0, l->Lines() - wlo), lh = (wll ? min(wll, wl) : wl) * font_height;
    if (!lh) return 0; Box b(xo, wl * font_height - lh, w, lh);
    return RingFrameBuffer::PushFrontAndUpdate(l, b, paint_cb, !(flag & Flag::NoVWrap)) / font_height;
}

int TextGUI::LinesFrameBuffer::PushBackAndUpdate(TextGUI::Line *l, int xo, int wlo, int wll, int flag) {
    if (!(flag & Flag::NoLayout)) l->Layout(wrap ? w : 0, flag & Flag::Flush);
    int wl = max(0, l->Lines() - wlo), lh = (wll ? min(wll, wl) : wl) * font_height;
    if (!lh) return 0; Box b(xo, wlo * font_height, w, lh);
    return RingFrameBuffer::PushBackAndUpdate(l, b, paint_cb, !(flag & Flag::NoVWrap)) / font_height;
}

void TextGUI::LinesFrameBuffer::PushFrontAndUpdateOffset(TextGUI::Line *l, int lo) {
    Update(l, RingFrameBuffer::BackPlus(point(0, (1 + lo) * font_height)));
    RingFrameBuffer::AdvancePixels(-l->Lines() * font_height);
}

void TextGUI::LinesFrameBuffer::PushBackAndUpdateOffset(TextGUI::Line *l, int lo) {
    Update(l, RingFrameBuffer::BackPlus(point(0, lo * font_height)));
    RingFrameBuffer::AdvancePixels(l->Lines() * font_height);
}

point TextGUI::LinesFrameBuffer::Paint(TextGUI::Line *l, point lp, const Box &b, int offset, int len) {
    Scissor scissor(lp.x, lp.y - b.h, b.w, b.h);
    screen->gd->Clear();
    l->Draw(lp + b.Position(), -1, offset, len);
    return point(lp.x, lp.y-b.h);
}

void TextGUI::Enter() {
    string cmd = Text();
    AssignInput("");
    if (!cmd.empty()) { AddHistory(cmd); Run(cmd); }
    TouchDevice::CloseKeyboard();
    if (deactivate_on_enter) active = false;
}

void TextGUI::Draw(const Box &b) {
    if (cmd_fb.SizeChanged(b.w, b.h, font)) {
        cmd_fb.p = point(0, font->Height());
        cmd_line.Draw(point(0, cmd_line.Lines() * font->Height()), cmd_fb.w);
        cmd_fb.SizeChangedDone();
    }
    // screen->gd->PushColor();
    // screen->gd->SetColor(cmd_color);
    cmd_fb.Draw(b.Position(), point());
    DrawCursor(b.Position() + cursor.p);
    // screen->gd->PopColor();
}

void TextGUI::DrawCursor(point p) {
    if (cursor.type == Cursor::Block) {
        screen->gd->EnableBlend();
        screen->gd->BlendMode(GraphicsDevice::OneMinusDstColor, GraphicsDevice::OneMinusSrcAlpha);
        screen->gd->FillColor(cmd_color);
        Box(p.x, p.y - font->Height(), font->max_width, font->Height()).Draw();
        screen->gd->BlendMode(GraphicsDevice::SrcAlpha, GraphicsDevice::One);
        screen->gd->DisableBlend();
    } else {
        bool blinking = false;
        Time now = Now(), elapsed; 
        if (active && (elapsed = now - cursor.blink_begin) > cursor.blink_time) {
            if (elapsed > cursor.blink_time * 2) cursor.blink_begin = now;
            else blinking = true;
        }
        if (blinking) font->Draw("_", p);
    }
}

void TextGUI::UpdateToken(Line *L, int word_offset, int word_len, int update_type, const LineTokenProcessor*) {
    const DrawableBoxArray &glyphs = L->data->glyphs;
    CHECK_LE(word_offset + word_len, glyphs.Size());
    string text = DrawableBoxRun(&glyphs[word_offset], word_len).Text();
    UpdateLongToken(L, word_offset, L, word_offset+word_len-1, text, update_type);
}

void TextGUI::UpdateLongToken(Line *BL, int beg_offset, Line *EL, int end_offset, const string &text, int update_type) {
    StringPiece textp(text);
    int offset = 0, url_offset = -1, fh = font->Height();
    for (; textp.len>1 && MatchingParens(*textp.buf, *textp.rbegin()); offset++, textp.buf++, textp.len -= 2) {}
    if (int punct = LengthChar(textp.buf, ispunct, textp.len)) { offset += punct; textp.buf += punct; }
    if      (textp.len > 7 && PrefixMatch(textp.buf, "http://"))  url_offset = offset + 7;
    else if (textp.len > 8 && PrefixMatch(textp.buf, "https://")) url_offset = offset + 8;
    if (url_offset >= 0) {
        if (update_type < 0) BL->data->links.erase(beg_offset);
        else {
            LinesFrameBuffer *fb = GetFrameBuffer();
            Box gb = Box(BL->data->glyphs[beg_offset].box).SetY(BL->p.y - fh);
            Box ge = Box(EL->data->glyphs[end_offset].box).SetY(EL->p.y - fh);
            Box3 box(Box(fb->Width(), fb->Height()), gb.Position(), ge.Position() + point(ge.w, 0), fh, fh);
            auto i = Insert(BL->data->links, beg_offset, shared_ptr<Link>(new Link(this, &mouse_gui, box, offset ? textp.str() : text)));
            if (new_link_cb) new_link_cb(i->second);
        }
    }
}

/* TextArea */

void TextArea::Write(const string &s, bool update_fb, bool release_fb) {
    if (!MainThread()) return RunInMainThread(new Callback(bind(&TextArea::Write, this, s, update_fb, release_fb)));
    write_last = Now();
    bool wrap = Wrap();
    int update_flag = LineFBPushBack();
    LinesFrameBuffer *fb = GetFrameBuffer();
    if (update_fb && fb->lines) fb->fb.Attach();
    ScopedDrawMode drawmode(update_fb ? DrawMode::_2D : DrawMode::NullOp);
    StringLineIter add_lines(s, StringLineIter::Flag::BlankLines);
    for (const char *add_line = add_lines.Next(); add_line; add_line = add_lines.Next()) {
        bool append = !write_newline && add_lines.first && add_lines.CurrentLength() && line.ring.count;
        Line *l = append ? &line[-1] : line.InsertAt(-1);
        if (!append) { l->Clear(); if (start_line) { start_line++; end_line++; } }
        if (write_timestamp) l->AppendText(StrCat(logtime(Now()), " "), cursor.attr);
        l->AppendText(StringPiece(add_line, add_lines.CurrentLength()), cursor.attr);
        l->Layout(wrap ? fb->w : 0);
        if (scrolled_lines) v_scrolled = (float)++scrolled_lines / (WrappedLines()-1);
        if (!update_fb || start_line) continue;
        LineUpdate(&line[-start_line-1], fb, (!append ? update_flag : 0));
    }
    if (update_fb && release_fb && fb->lines) fb->fb.Release();
}

void TextArea::Resized(const Box &b) {
    if (selection.enabled) {
        mouse_gui.box.SetDimension(b.Dimension());
        mouse_gui.UpdateBox(Box(b.Dimension()), -1, selection.gui_ind);
    }
    UpdateLines(last_v_scrolled, 0, 0, 0);
    Redraw(false);
}

void TextArea::Redraw(bool attach) {
    ScopedDrawMode drawmode(DrawMode::_2D);
    LinesFrameBuffer *fb = GetFrameBuffer();
    int fb_flag = LinesFrameBuffer::Flag::NoVWrap | LinesFrameBuffer::Flag::Flush;
    int lines = start_line_adjust + skip_last_lines, font_height = font->Height();
    int (LinesFrameBuffer::*update_cb)(Line*, int, int, int, int) =
        reverse_line_fb ? &LinesFrameBuffer::PushBackAndUpdate
                        : &LinesFrameBuffer::PushFrontAndUpdate;
    fb->p = reverse_line_fb ? point(0, fb->Height() - start_line_adjust * font_height)
                            : point(0, start_line_adjust * font_height);
    if (attach) { fb->fb.Attach(); screen->gd->Clear(); }
    for (int i=start_line; i<line.ring.count && lines < fb->lines; i++)
        lines += (fb->*update_cb)(&line[-i-1], -line_left, 0, fb->lines - lines, fb_flag);
    fb->p = point(0, fb->Height());
    if (attach) { fb->scroll = v2(); fb->fb.Release(); }
}

int TextArea::UpdateLines(float v_scrolled, int *first_ind, int *first_offset, int *first_len) {
    LinesFrameBuffer *fb = GetFrameBuffer();
    pair<int, int> old_first_line(start_line, -start_line_adjust), new_first_line, new_last_line;
    FlattenedArrayValues<TextGUI::Lines>
        flattened_lines(&line, line.Size(), bind(&TextArea::LayoutBackLine, this, _1, _2));
    flattened_lines.AdvanceIter(&new_first_line, (scrolled_lines = RoundF(v_scrolled * (WrappedLines()-1))));
    flattened_lines.AdvanceIter(&(new_last_line = new_first_line), fb->lines-1);
    LayoutBackLine(&line, new_last_line.first);
    bool up = new_first_line < old_first_line;
    int dist = flattened_lines.Distance(new_first_line, old_first_line, fb->lines-1);
    if (first_offset) *first_offset = up ?  start_line_cutoff+1 :  end_line_adjust+1;
    if (first_ind)    *first_ind    = up ? -start_line-1        : -end_line-1;
    if (first_len)    *first_len    = up ? -start_line_adjust   :  end_line_cutoff;
    start_line        =  new_first_line.first;
    start_line_adjust = -new_first_line.second;
    end_line          =  new_last_line.first;
    end_line_adjust   =  new_last_line.second;
    end_line_cutoff   = line[  -end_line-1].Lines() -  new_last_line.second - 1;
    start_line_cutoff = line[-start_line-1].Lines() - new_first_line.second - 1;
    return dist * (up ? -1 : 1);
}

void TextArea::UpdateScrolled() {
    int max_w = 4000;
    bool h_changed = Wrap() ? 0 : EqualChanged(&last_h_scrolled, h_scrolled);
    bool v_updated=0, v_changed = EqualChanged(&last_v_scrolled, v_scrolled);
    if (v_changed) {
        int first_ind = 0, first_offset = 0, first_len = 0;
        int dist = UpdateLines(v_scrolled, &first_ind, &first_offset, &first_len);
        if ((v_updated = dist)) {
            if (h_changed) UpdateHScrolled(RoundF(max_w * h_scrolled), false);
            if (1)         UpdateVScrolled(abs(dist), dist<0, first_ind, first_offset, first_len);
        }
    }
    if (h_changed && !v_updated) UpdateHScrolled(RoundF(max_w * h_scrolled), true);
}

void TextArea::UpdateHScrolled(int x, bool update_fb) {
    line_left = x;
    if (!update_fb) return;
    Redraw(true);
}

void TextArea::UpdateVScrolled(int dist, bool up, int ind, int first_offset, int first_len) {
    LinesFrameBuffer *fb = GetFrameBuffer();
    if (dist >= fb->lines) Redraw(true);
    else {
        bool front = up == reverse_line_fb, decr = front != reverse_line_fb;
        int wl = 0, (LinesFrameBuffer::*update_cb)(Line*, int, int, int, int) =
            front ? &LinesFrameBuffer::PushFrontAndUpdate : &LinesFrameBuffer::PushBackAndUpdate;
        ScopedDrawMode drawmode(DrawMode::_2D);
        fb->fb.Attach();
        if (first_len)  wl += (fb->*update_cb)(&line[ind], -line_left, first_offset, min(dist, first_len), 0); 
        while (wl<dist) wl += (fb->*update_cb)(&line[decr ? --ind : ++ind], -line_left, 0, dist-wl, 0);
        fb->fb.Release();
    }
}

void TextArea::Draw(const Box &b, bool draw_cursor) {
    int font_height = font->Height();
    LinesFrameBuffer *fb = GetFrameBuffer();
    if (fb->SizeChanged(b.w, b.h, font)) { Resized(b); fb->SizeChangedDone(); }
    if (clip) screen->gd->PushScissor(Box::DelBorder(b, *clip));
    fb->Draw(b.Position(), point(0, CommandLines() * font_height));
    if (clip) screen->gd->PopScissor();
    if (draw_cursor) TextGUI::Draw(Box(b.x, b.y, b.w, font_height));
    if (selection.enabled) mouse_gui.box.SetPosition(b.Position());
    if (selection.changing) DrawSelection();
    if (hover_link) {
        int fb_h = line_fb.Height();
        for (const auto &b : hover_link->box) {
            if (!b.w || !b.h) continue;
            point p = b.BottomLeft();
            p.y = RingIndex::Wrap(p.y + line_fb.scroll.y * fb_h, fb_h);
            glLine(p, point(b.BottomRight().x, p.y), &Color::white);
        }
        if (hover_link_cb) hover_link_cb(hover_link);
    }
}

bool TextArea::GetGlyphFromCoordsOffset(const point &p, Selection::Point *out, int sl, int sla) {
    LinesFrameBuffer *fb = GetFrameBuffer();
    int h = fb->Height(), fh = font->Height(), targ = reverse_line_fb ? ((h - p.y) / fh - sla) : (p.y / fh + sla);
    for (int i=sl, lines=0, ll; i<line.ring.count && lines<line_fb.lines; i++, lines += ll) {
        Line *L = &line[-i-1];
        if (lines + (ll = L->Lines()) <= targ) continue;
        L->data->glyphs.GetGlyphFromCoords(p, &out->char_ind, &out->glyph, targ - lines);
        out->glyph.y = lines * fh;
        out->line_ind = i;
        return true;
    }
    return false;
}

void TextArea::InitSelection() {
    mouse_gui.Activate();
    selection.gui_ind = mouse_gui.AddDragBox
        (Box(), MouseController::CoordCB(bind(&TextArea::DragCB, this, _1, _2, _3, _4)));
}

void TextArea::DrawSelection() {
    screen->gd->EnableBlend();
    screen->gd->FillColor(selection_color);
    selection.box.Draw(mouse_gui.box.BottomLeft());
}

void TextArea::DragCB(int button, int, int, int down) {
    point p = screen->mouse - mouse_gui.box.BottomLeft();
    LinesFrameBuffer *fb = GetFrameBuffer();
    Selection *s = &selection;
    if (!(s->changing = down)) {
        bool swap = s->end < s->beg;
        CopyText(swap ? s->end : s->beg, swap ? s->beg : s->end);
        s->changing_previously = 0;
        return;
    }

    int scp = s->changing_previously, fh = font->Height(), h = fb->Height();
    if (scp) GetGlyphFromCoords((s->end.click = point(line_left+p.x, p.y)), &s->end);
    else   { GetGlyphFromCoords((s->beg.click = point(line_left+p.x, p.y)), &s->beg); s->end = s->beg; }

    bool swap = (!reverse_line_fb && s->end < s->beg) || (reverse_line_fb && s->beg < s->end);
    Box gb = swap ? s->end.glyph : s->beg.glyph;
    Box ge = swap ? s->beg.glyph : s->end.glyph;
    if (reverse_line_fb) { gb.y=h-gb.y-gb.h; ge.y=h-ge.y-ge.h; }
    s->box = Box3(Box(fb->Width(), fb->Height()), gb.Position(), ge.Position() + point(ge.w, 0), fh, fh);
    s->changing_previously = s->changing;
}

void TextArea::CopyText(const Selection::Point &beg, const Selection::Point &end) {
    string copy_text = CopyText(beg.line_ind, beg.char_ind, end.line_ind, end.char_ind, true);
    if (!copy_text.empty()) Clipboard::Set(copy_text);
}

string TextArea::CopyText(int beg_line_ind, int beg_char_ind, int end_line_ind, int end_char_ind, bool add_nl) {
    string copy_text;
    int d = reverse_line_fb ? 1 : -1;
    int b = reverse_line_fb ? end_line_ind : beg_line_ind;
    int e = reverse_line_fb ? beg_line_ind : end_line_ind;
    for (int i = b; /**/; i += d) {
        Line *l = &line[-i-1];
        int len = l->Size();
        if (i == beg_line_ind) {
            if (!l->Size() || beg_char_ind < 0) len = -1;
            else {
                len = (beg_line_ind == end_line_ind && end_char_ind >= 0) ? end_char_ind+1 : l->Size();
                copy_text += Substr(l->Text(), beg_char_ind, max(0, len - beg_char_ind));
            }
        } else if (i == end_line_ind) {
            len = (end_char_ind >= 0) ? end_char_ind+1 : l->Size();
            copy_text += Substr(l->Text(), 0, len);
        } else copy_text += l->Text();
        if (add_nl && len == l->Size()) copy_text += "\n";
        if (i == e) break;
    }
    return copy_text;
}

/* Editor */

void Editor::UpdateWrappedLines(int cur_font_size, int width) {
    wrapped_lines = 0;
    file_line.Clear();
    file->Reset();
    int offset = 0, wrap = Wrap(), ll;
    for (const char *l = file->NextLineRaw(&offset); l; l = file->NextLineRaw(&offset)) {
        wrapped_lines += (ll = wrap ? TextArea::font->Lines(l, width) : 1);
        file_line.val.Insert(LineOffset(offset, file->nr.record_len, ll));
    }
    file_line.LoadFromSortedVal();
}

int Editor::UpdateLines(float v_scrolled, int *first_ind, int *first_offset, int *first_len) {
    LinesFrameBuffer *fb = GetFrameBuffer();
    bool width_changed = last_fb_width != fb->w, wrap = Wrap();
    if (width_changed) {
        last_fb_width = fb->w;
        if (wrap || !wrapped_lines) UpdateWrappedLines(TextArea::font->size, fb->w);
    }

    bool resized = (width_changed && wrap) || last_fb_lines != fb->lines;
    int new_first_line = RoundF(v_scrolled * (wrapped_lines - 1)), new_last_line = new_first_line + fb->lines;
    int dist = resized ? fb->lines : abs(new_first_line - last_first_line), read_len = 0, bo = 0, l, e;
    if (!dist || !file_line.size()) return 0;

    bool redraw = dist >= fb->lines;
    if (redraw) { line.Clear(); fb_wrapped_lines = 0; }

    bool up = !redraw && new_first_line < last_first_line;
    if (first_offset) *first_offset = up ?  start_line_cutoff : end_line_adjust;
    if (first_len)    *first_len    = up ? -start_line_adjust : end_line_cutoff;

    pair<int, int> read_lines;
    if (dist < fb->lines) {
        if (up) read_lines = pair<int, int>(new_first_line, dist);
        else    read_lines = pair<int, int>(new_first_line + fb->lines - dist, dist);
    } else      read_lines = pair<int, int>(new_first_line, fb->lines);

    bool head_read = new_first_line == read_lines.first;
    bool tail_read = new_last_line  == read_lines.first + read_lines.second;
    bool short_read = !(head_read && tail_read), shorten_read = short_read && head_read && start_line_adjust;
    int past_end_lines = max(0, min(dist, read_lines.first + read_lines.second - wrapped_lines)), added = 0;
    read_lines.second = max(0, read_lines.second - past_end_lines);

    if      ( up && dist <= -start_line_adjust) { start_line_adjust += dist; read_lines.second=past_end_lines=0; }
    else if (!up && dist <=  end_line_cutoff)   { end_line_cutoff   -= dist; read_lines.second=past_end_lines=0; }

    LineMap::ConstIterator lib, lie;
    if (read_lines.second) {
        CHECK((lib = file_line.LesserBound(read_lines.first)).val);
        if (wrap) {
            if (head_read) start_line_adjust = min(0, lib.key - new_first_line);
            if (short_read && tail_read && end_line_cutoff) ++lib;
        }
        int last_read_line = read_lines.first + read_lines.second - 1;
        for (lie = lib; lie.val && lie.key <= last_read_line; ++lie) {
            auto v = lie.val;
            if (shorten_read && !(lie.key + v->wrapped_lines <= last_read_line)) break;
            if (v->size >= 0) read_len += (v->size + 1);
        }
        if (wrap && tail_read) {
            LineMap::ConstIterator i = lie;
            end_line_cutoff = max(0, (--i).key + i.val->wrapped_lines - new_last_line);
        }
    }

    string buf((read_len = X_or_1(read_len)-1), 0);
    if (read_len) {
        file->Seek(lib.val->offset, File::Whence::SET);
        CHECK_EQ(buf.size(), file->Read((char*)buf.data(), buf.size()));
    }
    
    Line *L = 0;
    if (up) for (LineMap::ConstIterator li = lie; li != lib; bo += l + (L != 0), added++) {
        l = (e = -min(0, (--li).val->size)) ? 0 : li.val->size;
        (L = line.PushBack())->AssignText(e ? edits[e-1] : StringPiece(buf.data() + read_len - bo - l, l));
        fb_wrapped_lines += L->Layout(fb->w);
    }
    else for (LineMap::ConstIterator li = lib; li != lie; ++li, bo += l+1, added++) {
        l = (e = -min(0, li.val->size)) ? 0 : li.val->size;
        (L = line.PushFront())->AssignText(e ? edits[e-1] : StringPiece(buf.data() + bo, l));
        fb_wrapped_lines += L->Layout(fb->w);
    }
    if (!up) for (int i=0; i<past_end_lines; i++, added++) { 
        (L = line.PushFront())->Clear();
        fb_wrapped_lines += L->Layout(fb->w);
    }

    CHECK_LT(line.ring.count, line.ring.size);
    if (!redraw) {
        for (bool first=1;;first=0) {
            int ll = (L = up ? line.Front() : line.Back())->Lines();
            if (fb_wrapped_lines + (up ? start_line_adjust : -end_line_cutoff) - ll < fb->lines) break;
            fb_wrapped_lines -= ll;
            if (up) line.PopFront(1);
            else    line.PopBack (1);
        }
        if (up) end_line_cutoff   =  (fb_wrapped_lines + start_line_adjust - fb->lines);
        else    start_line_adjust = -(fb_wrapped_lines - end_line_cutoff   - fb->lines);
    }

    end_line_adjust   = line.Front()->Lines() - end_line_cutoff;
    start_line_cutoff = line.Back ()->Lines() + start_line_adjust;
    if (first_ind) *first_ind = up ? -added-1 : -line.Size()+added;

    last_fb_lines = fb->lines;
    last_first_line = new_first_line;
    return dist * (up ? -1 : 1);
}

/* Terminal */

#ifdef  LFL_TERMINAL_DEBUG
#define TerminalTrace(...) printf(__VA_ARGS__)
#define TerminalDebug(...) ERRORf(__VA_ARGS__)
#else
#define TerminalTrace(...)
#define TerminalDebug(...)
#endif

Terminal::StandardVGAColors::StandardVGAColors() { 
    c[0] = Color(  0,   0,   0); c[ 8] = Color( 85,  85,  85);
    c[1] = Color(170,   0,   0); c[ 9] = Color(255,  85,  85);
    c[2] = Color(  0, 170,   0); c[10] = Color( 85, 255,  85);
    c[3] = Color(170,  85,   0); c[11] = Color(255, 255,  85);
    c[4] = Color(  0,   0, 170); c[12] = Color( 85,  85, 255);
    c[5] = Color(170,   0, 170); c[13] = Color(255,  85, 255);
    c[6] = Color(  0, 170, 170); c[14] = Color( 85, 255, 255);
    c[7] = Color(170, 170, 170); c[15] = Color(255, 255, 255);
    bg_index = 0; normal_index = 7; bold_index = 15;
}

/// Solarized palette by Ethan Schoonover
Terminal::SolarizedColors::SolarizedColors() { 
    c[0] = Color(  7,  54,  66); c[ 8] = Color(  0,  43,  54);
    c[1] = Color(220,  50,  47); c[ 9] = Color(203,  75,  22);
    c[2] = Color(133, 153,   0); c[10] = Color( 88, 110, 117);
    c[3] = Color(181, 137,   0); c[11] = Color(101, 123, 131);
    c[4] = Color( 38, 139, 210); c[12] = Color(131, 148, 150);
    c[5] = Color(211,  54, 130); c[13] = Color(108, 113, 196);
    c[6] = Color( 42, 161, 152); c[14] = Color(147, 161, 161);
    c[7] = Color(238, 232, 213); c[15] = Color(253, 246, 227);
    bg_index = 8; normal_index = 12; bold_index = 12;
}

Terminal::Terminal(int FD, Window *W, Font *F) :TextArea(W, F), fd(FD), fb_cb(bind(&Terminal::GetFrameBuffer, this, _1)) {
    CHECK(F->fixed_width || (F->flag & FontDesc::Mono));
    wrap_lines = write_newline = insert_mode = 0;
    for (int i=0; i<line.ring.size; i++) line[i].data->glyphs.attr.source = this;
    SetColors(Singleton<SolarizedColors>::Get());
    cursor.attr = default_cursor_attr;
    cursor.type = Cursor::Block;
    token_processing = 1;
    cmd_prefix = "";
}

void Terminal::Resized(const Box &b) {
    int old_term_width = term_width, old_term_height = term_height;
    SetDimension(b.w / font->FixedWidth(), b.h / font->Height());
    TerminalDebug("Resized %d, %d <- %d, %d\n", term_width, term_height, old_term_width, old_term_height);
    bool grid_changed = term_width != old_term_width || term_height != old_term_height;

#ifndef _WIN32
    if (grid_changed || first_resize) {
        struct winsize ws;
        memzero(ws);
        ws.ws_row = term_height;
        ws.ws_col = term_width;
        ioctl(fd, TIOCSWINSZ, &ws);
    }
#endif

    int height_dy = term_height - old_term_height;
    if      (height_dy > 0) TextArea::Write(string(height_dy, '\n'), 0);
    else if (height_dy < 0 && term_cursor.y < old_term_height) line.PopBack(-height_dy);

    term_cursor.x = min(term_cursor.x, term_width);
    term_cursor.y = min(term_cursor.y, term_height);
    UpdateCursor();
    TextArea::Resized(b);
    if (clip) clip = UpdateClipBorder();
    ResizedLeftoverRegion(b.w, b.h);
}

void Terminal::ResizedLeftoverRegion(int w, int h, bool update_fb) {
    if (!cmd_fb.SizeChanged(w, h, font)) return;
    if (update_fb) {
        for (int i=0; i<start_line;      i++) cmd_fb.Update(&line[-i-1],             point(0,GetCursorY(term_height-i)), LinesFrameBuffer::Flag::Flush);
        for (int i=0; i<skip_last_lines; i++) cmd_fb.Update(&line[-line_fb.lines+i], point(0,GetCursorY(i+1)),           LinesFrameBuffer::Flag::Flush);
    }
    cmd_fb.SizeChangedDone();
    last_fb = 0;
}

void Terminal::SetScrollRegion(int b, int e, bool release_fb) {
    if (b<0 || e<0 || e>term_height || b>e) { TerminalDebug("%d-%d outside 1-%d\n", b, e, term_height); return; }
    int prev_region_beg = scroll_region_beg, prev_region_end = scroll_region_end, font_height = font->Height();
    scroll_region_beg = b;
    scroll_region_end = e;
    bool no_region = !scroll_region_beg || !scroll_region_end ||
        (scroll_region_beg == 1 && scroll_region_end == term_height);
    skip_last_lines = no_region ? 0 : scroll_region_beg - 1;
    start_line_adjust = start_line = no_region ? 0 : term_height - scroll_region_end;
    clip = no_region ? 0 : UpdateClipBorder();
    ResizedLeftoverRegion(line_fb.w, line_fb.h, false);

    if (release_fb) { last_fb=0; screen->gd->DrawMode(DrawMode::_2D, 0); }
    int   prev_beg_or_1=X_or_1(prev_region_beg),     prev_end_or_ht=X_or_Y(  prev_region_end, term_height);
    int scroll_beg_or_1=X_or_1(scroll_region_beg), scroll_end_or_ht=X_or_Y(scroll_region_end, term_height);

    if (scroll_beg_or_1 != prev_beg_or_1 || prev_end_or_ht != scroll_end_or_ht) GetPrimaryFrameBuffer();
    for (int i =  scroll_beg_or_1; i <    prev_beg_or_1; i++) line_fb.Update(GetTermLine(i),   line_fb.BackPlus(point(0, (term_height-i+1)*font_height)), LinesFrameBuffer::Flag::NoLayout);
    for (int i =   prev_end_or_ht; i < scroll_end_or_ht; i++) line_fb.Update(GetTermLine(i+1), line_fb.BackPlus(point(0, (term_height-i)  *font_height)), LinesFrameBuffer::Flag::NoLayout);

    if (prev_beg_or_1 < scroll_beg_or_1 || scroll_end_or_ht < prev_end_or_ht) GetSecondaryFrameBuffer();
    for (int i =    prev_beg_or_1; i < scroll_beg_or_1; i++) cmd_fb.Update(GetTermLine(i),   point(0, GetCursorY(i)),   LinesFrameBuffer::Flag::NoLayout);
    for (int i = scroll_end_or_ht; i <  prev_end_or_ht; i++) cmd_fb.Update(GetTermLine(i+1), point(0, GetCursorY(i+1)), LinesFrameBuffer::Flag::NoLayout);
    if (release_fb) cmd_fb.fb.Release();
}

void Terminal::SetDimension(int w, int h) {
    term_width  = w;
    term_height = h;
    if (bg_color) screen->gd->ClearColor(*bg_color);
    if (!line.Size()) TextArea::Write(string(term_height, '\n'), 0);
}

void Terminal::UpdateToken(Line *L, int word_offset, int word_len, int update_type, const LineTokenProcessor *token) {
    CHECK_LE(word_offset + word_len, L->data->glyphs.Size());
    Line *BL = L, *EL = L, *NL;
    int in_line_ind = -line.IndexOf(L)-1, beg_line_ind = in_line_ind, end_line_ind = in_line_ind;
    int beg_offset = word_offset, end_offset = word_offset + word_len - 1, new_offset, gs;

    for (; !beg_offset && beg_line_ind < term_height; beg_line_ind++, beg_offset = term_width - new_offset, BL = NL) {
        const DrawableBoxArray &glyphs = (NL = &line[-beg_line_ind-1-1])->data->glyphs;
        if ((gs = glyphs.Size()) != term_width || !(new_offset = RLengthChar(&glyphs[gs-1], notspace, gs))) break;
    }
    for (; end_offset == term_width-1 && end_line_ind >= 0; end_line_ind--, end_offset = new_offset - 1, EL = NL) {
        const DrawableBoxArray &glyphs = (NL = &line[-beg_line_ind-1+1])->data->glyphs;
        if (!(new_offset = LengthChar(&glyphs[0], notspace, glyphs.Size()))) break;
    }

    string text = CopyText(beg_line_ind, beg_offset, end_line_ind, end_offset, 0);
    if (update_type < 0) UpdateLongToken(BL, beg_offset, EL, end_offset, text, update_type);
    
    if (BL != L && !word_offset && token->lbw != token->nlbw)
        UpdateLongToken(BL, beg_offset, &line[-in_line_ind-1-1], term_width-1,
                        CopyText(beg_line_ind, beg_offset, in_line_ind+1, term_width-1, 0), update_type * -10);
    if (EL != L && word_offset + word_len == term_width && token->lew != token->nlew)
        UpdateLongToken(&line[-in_line_ind-1+1], 0, EL, end_offset,
                        CopyText(in_line_ind-1, 0, end_line_ind, end_offset, 0), update_type * -11);

    if (update_type > 0) UpdateLongToken(BL, beg_offset, EL, end_offset, text, update_type);
}

const Drawable::Attr *Terminal::GetAttr(int attr) const {
    static thread_local Drawable::Attr ret;
    Color *fg = colors ? &colors->c[Attr::GetFGColorIndex(attr)] : 0;
    Color *bg = colors ? &colors->c[Attr::GetBGColorIndex(attr)] : 0;
    if (attr & Attr::Reverse) swap(fg, bg);
    ret.font = Fonts::Change(font, font->size, *fg, *bg, font->flag);
    ret.bg = bg; // &font->bg;
    ret.underline = attr & Attr::Underline;
    return &ret;
}

void Terminal::Draw(const Box &b, bool draw_cursor) {
    TextArea::Draw(b, false);
    if (clip) {
        { Scissor s(Box::TopBorder(b, *clip)); cmd_fb.Draw(b.Position(), point(), false); }
        { Scissor s(Box::BotBorder(b, *clip)); cmd_fb.Draw(b.Position(), point(), false); }
    }
    if (draw_cursor) TextGUI::DrawCursor(b.Position() + cursor.p);
    if (selection.changing) DrawSelection();
}

void Terminal::Write(const string &s, bool update_fb, bool release_fb) {
    if (!MainThread()) return RunInMainThread(new Callback(bind(&Terminal::Write, this, s, update_fb, release_fb)));
    TerminalTrace("Terminal: Write('%s', %zd)\n", CHexEscapeNonAscii(s).c_str(), s.size());
    screen->gd->DrawMode(DrawMode::_2D, 0);
    if (bg_color) screen->gd->ClearColor(*bg_color);
    last_fb = 0;
    for (int i = 0; i < s.size(); i++) {
        const unsigned char &c = s[i];
        if (c == 0x18 || c == 0x1a) { /* CAN or SUB */ parse_state = State::TEXT; continue; }
        if (parse_state == State::ESC) {
            parse_state = State::TEXT; // default next state
            TerminalTrace("ESC %c\n", c);
            if (c >= '(' && c <= '/') {
                parse_state = State::CHARSET;
                parse_charset = c;
            } else switch (c) {
                case '[':
                    parse_state = State::CSI; // control sequence introducer
                    parse_csi.clear(); break;
                case ']':
                    parse_state = State::OSC; // operating system command
                    parse_osc.clear();
                    parse_osc_escape = false; break;
                case '=': case '>':                        break; // application or normal keypad
                case 'D': Newline();                       break;
                case 'M': NewTopline();                    break;
                case '7': saved_term_cursor = term_cursor; break;
                case '8': term_cursor = point(Clamp(saved_term_cursor.x, 1, term_width),
                                              Clamp(saved_term_cursor.y, 1, term_height));
                default: TerminalDebug("unhandled escape %c (%02x)\n", c, c);
            }
        } else if (parse_state == State::CHARSET) {
            TerminalTrace("charset %c%c\n", parse_charset, c);
            parse_state = State::TEXT;

        } else if (parse_state == State::OSC) {
            if (!parse_osc_escape) {
                if (c == 0x1b) { parse_osc_escape = 1; continue; }
                if (c != '\b') { parse_osc       += c; continue; }
            }
            else if (c != 0x5c) { TerminalDebug("within-OSC-escape %c (%02x)\n", c, c); parse_state = State::TEXT; continue; }
            parse_state = State::TEXT;

            if (0) {}
            else TerminalDebug("unhandled OSC %s\n", parse_osc.c_str());

        } else if (parse_state == State::CSI) {
            // http://en.wikipedia.org/wiki/ANSI_escape_code#CSI_codes
            if (c < 0x40 || c > 0x7e) { parse_csi += c; continue; }
            TerminalTrace("CSI %s%c (cur=%d,%d)\n", parse_csi.c_str(), c, term_cursor.x, term_cursor.y);
            parse_state = State::TEXT;

            int parsed_csi=0, parse_csi_argc=0, parse_csi_argv[16];
            unsigned char parse_csi_argv00 = parse_csi.empty() ? 0 : (isdig(parse_csi[0]) ? 0 : parse_csi[0]);
            for (/**/; Within<int>(parse_csi[parsed_csi], 0x20, 0x2f); parsed_csi++) {}
            StringPiece intermed(parse_csi.data(), parsed_csi);

            memzeros(parse_csi_argv);
            bool parse_csi_arg_done = 0;
            // http://www.inwap.com/pdp10/ansicode.txt 
            for (/**/; Within<int>(parse_csi[parsed_csi], 0x30, 0x3f); parsed_csi++) {
                if (parse_csi[parsed_csi] <= '9') { // 0x30 == '0'
                    AccumulateAsciiDigit(&parse_csi_argv[parse_csi_argc], parse_csi[parsed_csi]);
                    parse_csi_arg_done = 0;
                } else if (parse_csi[parsed_csi] <= ';') {
                    parse_csi_arg_done = 1;
                    parse_csi_argc++;
                } else continue;
            }
            if (!parse_csi_arg_done) parse_csi_argc++;

            switch (c) {
                case '@': {
                    LineUpdate l(GetCursorLine(), fb_cb);
                    l->UpdateText(term_cursor.x-1, StringPiece(string(X_or_1(parse_csi_argv[0]), ' ')), cursor.attr, term_width, 0, 1);
                } break;
                case 'A': term_cursor.y = max(term_cursor.y - X_or_1(parse_csi_argv[0]), 1);           break;
                case 'B': term_cursor.y = min(term_cursor.y + X_or_1(parse_csi_argv[0]), term_height); break;
                case 'C': term_cursor.x = min(term_cursor.x + X_or_1(parse_csi_argv[0]), term_width);  break;
                case 'D': term_cursor.x = max(term_cursor.x - X_or_1(parse_csi_argv[0]), 1);           break;
                case 'H': term_cursor = point(Clamp(parse_csi_argv[1], 1, term_width),
                                              Clamp(parse_csi_argv[0], 1, term_height)); break;
                case 'J': {
                    LineUpdate l(GetCursorLine(), fb_cb);
                    int clear_beg_y = 1, clear_end_y = term_height;
                    if      (parse_csi_argv[0] == 0) { l->Erase(term_cursor.x-1);  clear_beg_y = term_cursor.y; }
                    else if (parse_csi_argv[0] == 1) { l->Erase(0, term_cursor.x); clear_end_y = term_cursor.y; }
                    else if (parse_csi_argv[0] == 2) { l->Clear(); term_cursor.x = term_cursor.y = 1; }
                    for (int i = clear_beg_y; i <= clear_end_y; i++) LineUpdate(GetTermLine(i), fb_cb)->Clear();
                } break;
                case 'K': {
                    LineUpdate l(GetCursorLine(), fb_cb);
                    if      (parse_csi_argv[0] == 0) l->Erase(term_cursor.x-1);
                    else if (parse_csi_argv[0] == 1) l->Erase(0, term_cursor.x);
                    else if (parse_csi_argv[0] == 2) l->Clear();
                } break;
                case 'L': case 'M': {
                    int sl = (c == 'L' ? 1 : -1) * X_or_1(parse_csi_argv[0]);
                    if (clip && term_cursor.y < scroll_region_beg) {
                        TerminalDebug("%s outside scroll region %d-%d\n",
                                      term_cursor.DebugString().c_str(), scroll_region_beg, scroll_region_end);
                    } else {
                        int end_y = X_or_Y(scroll_region_end, term_height);
                        int flag = sl < 0 ? LineUpdate::PushBack : LineUpdate::PushFront;
                        int offset = sl < 0 ? start_line : -skip_last_lines;
                        Scroll(term_cursor.y, end_y, sl, clip);
                        GetPrimaryFrameBuffer();
                        for (int i=0, l=abs(sl); i<l; i++)
                            LineUpdate(GetTermLine(sl<0 ? (end_y-l+i+1) : (term_cursor.y+l-i-1)),
                                       &line_fb, flag, offset);
                    }
                } break;
                case 'P': {
                    LineUpdate l(GetCursorLine(), fb_cb);
                    int erase = max(1, parse_csi_argv[0]);
                    l->Erase(term_cursor.x-1, erase);
                } break;
                case 'h': { // set mode
                    int mode = parse_csi_argv[0];
                    if      (parse_csi_argv00 == 0   && mode ==    4) { insert_mode = true;           }
                    else if (parse_csi_argv00 == 0   && mode ==   34) { /* steady cursor */           }
                    else if (parse_csi_argv00 == '?' && mode ==    1) { /* guarded area tx = all */   }
                    else if (parse_csi_argv00 == '?' && mode ==   25) { cursor_enabled = true;        }
                    else if (parse_csi_argv00 == '?' && mode ==   47) { /* alternate screen buffer */ }
                    else if (parse_csi_argv00 == '?' && mode == 1049) { /* save screen */             }
                    else TerminalDebug("unhandled CSI-h mode = %d av00 = %d i= %s\n", mode, parse_csi_argv00, intermed.str().c_str());
                } break;
                case 'l': { // reset mode
                    int mode = parse_csi_argv[0];
                    if      (parse_csi_argv00 == 0   && mode ==    4) { insert_mode = false;                 }
                    else if (parse_csi_argv00 == 0   && mode ==   34) { /* blink cursor */                   }
                    else if (parse_csi_argv00 == '?' && mode ==    1) { /* guarded areax tx = unprot only */ }
                    else if (parse_csi_argv00 == '?' && mode ==   25) { cursor_enabled = false;              }
                    else if (parse_csi_argv00 == '?' && mode ==   47) { /* normal screen buffer */           }
                    else if (parse_csi_argv00 == '?' && mode == 1049) { /* restore screen */                 }
                    else TerminalDebug("unhandled CSI-l mode = %d av00 = %d i= %s\n", mode, parse_csi_argv00, intermed.str().c_str());
                } break;
                case 'm':
                    for (int i=0; i<parse_csi_argc; i++) {
                        int sgr = parse_csi_argv[i]; // select graphic rendition
                        if      (sgr >= 30 && sgr <= 37) Attr::SetFGColorIndex(&cursor.attr, sgr-30);
                        else if (sgr >= 40 && sgr <= 47) Attr::SetBGColorIndex(&cursor.attr, sgr-40);
                        else switch(sgr) {
                            case 0:         cursor.attr  =  default_cursor_attr;                       break;
                            case 1:         cursor.attr |=  Attr::Bold;                                break;
                            case 3:         cursor.attr |=  Attr::Italic;                              break;
                            case 4:         cursor.attr |=  Attr::Underline;                           break;
                            case 5: case 6: cursor.attr |=  Attr::Blink;                               break;
                            case 7:         cursor.attr |=  Attr::Reverse;                             break;
                            case 22:        cursor.attr &= ~Attr::Bold;                                break;
                            case 23:        cursor.attr &= ~Attr::Italic;                              break;
                            case 24:        cursor.attr &= ~Attr::Underline;                           break;
                            case 25:        cursor.attr &= ~Attr::Blink;                               break;
                            case 39:        Attr::SetFGColorIndex(&cursor.attr, colors->normal_index); break;
                            case 49:        Attr::SetBGColorIndex(&cursor.attr, colors->bg_index);     break;
                            default:        TerminalDebug("unhandled SGR %d\n", sgr);
                        }
                    } break;
                case 'r':
                    if (parse_csi_argc == 2) SetScrollRegion(parse_csi_argv[0], parse_csi_argv[1]);
                    else TerminalDebug("invalid scroll region argc %d\n", parse_csi_argc);
                    break;
                default:
                    TerminalDebug("unhandled CSI %s%c\n", parse_csi.c_str(), c);
            }
        } else {
            // http://en.wikipedia.org/wiki/C0_and_C1_control_codes#C0_.28ASCII_and_derivatives.29
            bool C0_control = (c >= 0x00 && c <= 0x1f) || c == 0x7f;
            bool C1_control = (c >= 0x80 && c <= 0x9f);
            if (C0_control || C1_control) {
                TerminalTrace("C0/C1 control: %02x\n", c);
                FlushParseText();
            }
            if (C0_control) switch(c) {
                case '\a':   TerminalDebug("%s", "bell");                      break; // bell
                case '\b':   term_cursor.x = max(term_cursor.x-1, 1);          break; // backspace
                case '\t':   term_cursor.x = min(term_cursor.x+8, term_width); break; // tab 
                case '\r':   term_cursor.x = 1;                                break; // carriage return
                case '\x1b': parse_state = State::ESC;                         break;
                case '\x14': case '\x15': case '\x7f':                         break; // shift charset in, out, delete
                case '\n':   case '\v':   case '\f':   Newline();              break; // line feed, vertical tab, form feed
                default:                               TerminalDebug("unhandled C0 control %02x\n", c);
            } else if (0 && C1_control) {
                if (0) {}
                else TerminalDebug("unhandled C1 control %02x\n", c);
            } else {
                parse_text += c;
            }
        }
    }
    FlushParseText();
    UpdateCursor();
    line_fb.fb.Release();
    last_fb = 0;
}

void Terminal::FlushParseText() {
    if (parse_text.empty()) return;
    bool append = 0;
    int consumed = 0, write_size = 0, update_size = 0;
    CHECK_GT(term_cursor.x, 0);
    font = GetAttr(cursor.attr)->font;
    String16 input_text = String::ToUTF16(parse_text, &consumed);
    TerminalTrace("Terminal: (cur=%d,%d) FlushParseText('%s').size = [%zd, %d]\n", term_cursor.x, term_cursor.y,
                  StringPiece(parse_text.data(), consumed).str().c_str(), input_text.size(), consumed);
    for (int wrote = 0; wrote < input_text.size(); wrote += write_size) {
        if (wrote || term_cursor.x > term_width) Newline(true);
        Line *l = GetCursorLine();
        LinesFrameBuffer *fb = GetFrameBuffer(l);
        int remaining = input_text.size() - wrote, o = term_cursor.x-1;
        write_size = min(remaining, term_width - o);
        String16Piece input_piece(input_text.data() + wrote, write_size);
        update_size = l->UpdateText(o, input_piece, cursor.attr, term_width, &append);
        TerminalTrace("Terminal: FlushParseText: UpdateText(%d, %d, '%s').size = [%d, %d] attr=%d\n",
                      term_cursor.x, term_cursor.y, String::ToUTF8(input_piece).c_str(), write_size, update_size, cursor.attr);
        CHECK_GE(update_size, write_size);
        l->Layout();
        if (!fb->lines) continue;
        int s = l->Size(), ol = s - o, sx = l->data->glyphs.LeftBound(o), ex = l->data->glyphs.RightBound(s-1);
        if (append) l->Draw(l->p, -1, o, ol);
        else LinesFrameBuffer::Paint(l, point(sx, l->p.y), Box(-sx, 0, ex - sx, fb->font_height), o, ol);
    }
    term_cursor.x += update_size;
    parse_text.erase(0, consumed);
}

void Terminal::Newline(bool carriage_return) {
    if (clip && term_cursor.y == scroll_region_end) {
        Scroll(scroll_region_beg, scroll_region_end, -1, clip);
        LineUpdate(GetTermLine(scroll_region_end), GetPrimaryFrameBuffer(), LineUpdate::PushBack, start_line);
    } else if (term_cursor.y == term_height) {
        if (!clip) { TextArea::Write(string(1, '\n'), true, false); last_fb=&line_fb; }
    } else term_cursor.y = min(term_height, term_cursor.y+1);
    if (carriage_return) term_cursor.x = 1;
}

void Terminal::NewTopline() {
    if (clip && term_cursor.y == scroll_region_beg) {
        LineUpdate(line.InsertAt(GetTermLineIndex(term_cursor.y), 1, start_line_adjust),
                   GetPrimaryFrameBuffer(), LineUpdate::PushFront, skip_last_lines);
    } else if (term_cursor.y == 1) {
        if (!clip) LineUpdate(line.InsertAt(-term_height, 1, start_line_adjust),
                              GetPrimaryFrameBuffer(), LineUpdate::PushFront);
    } else term_cursor.y = max(1, term_cursor.y-1);
}

/* Console */

bool Console::Toggle() {
    if (!TextGUI::Toggle()) return false;
    bool last_animating = animating;
    Time now = Now(), elapsed = now - anim_begin;
    anim_begin = now - (elapsed < anim_time ? anim_time - elapsed : Time(0));
    animating = (elapsed = now - anim_begin) < anim_time;
    if (animating && !last_animating && animating_cb) animating_cb();
    return true;
}

void Console::Draw() {
    if (!ran_startcmd && (ran_startcmd = 1)) if (startcmd.size()) Run(startcmd);

    drawing = 1;
    Time now=Now(), elapsed;
    bool last_animating = animating;
    int h = active ? (int)(screen->height*screen_percent) : 0;
    if ((animating = (elapsed = now - anim_begin) < anim_time)) {
        if (active) h = (int)(screen->height*(  (double)elapsed.count()/anim_time.count())*screen_percent);
        else        h = (int)(screen->height*(1-(double)elapsed.count()/anim_time.count())*screen_percent);
    }
    if (!animating) {
        if (last_animating && animating_cb) animating_cb();
        if (!active) { drawing = 0; return; }
    }

    screen->gd->FillColor(color);
    if (blend) screen->gd->EnableBlend(); 
    else       screen->gd->DisableBlend();

    int y = bottom_or_top ? 0 : screen->height-h;
    Box(0, y, screen->width, h).Draw();

    screen->gd->ClearColor(Color::clear);
    screen->gd->SetColor(Color::white);
    TextArea::Draw(Box(0, y, screen->width, h), true);
}

/* Dialog */

void Dialog::Draw() {
    if (child_box.data.empty()) Layout();
    if (moving) box.SetPosition(win_start + screen->mouse - mouse_start);

    Box outline = BoxAndTitle();
    static const int min_width = 50, min_height = 1;
    if (resizing_left)   MinusPlus(&outline.x, &outline.w, max(-outline.w + min_width,            (int)(mouse_start.x - screen->mouse.x)));
    if (resizing_bottom) MinusPlus(&outline.y, &outline.h, max(-outline.h + min_height + title.h, (int)(mouse_start.y - screen->mouse.y)));
    if (resizing_right)  outline.w += max(-outline.w + min_width, (int)(screen->mouse.x - mouse_start.x));

    if (!app->input.MouseButton1Down()) {
        if (resizing_left || resizing_right || resizing_top || resizing_bottom) {
            box = Box(outline.x, outline.y, outline.w, outline.h - title.h);
            moving = true;
        }
        if (moving) Layout();
        moving = resizing_left = resizing_right = resizing_top = resizing_bottom = 0;
    }

    screen->gd->FillColor(color);
    box.Draw();

    screen->gd->SetColor(color + Color(0,0,0,(int)(color.A()*.25)));
    (title + box.TopLeft()).Draw();
    screen->gd->SetColor(Color::white);

    if (moving || resizing_left || resizing_right || resizing_top || resizing_bottom)
        BoxOutline().Draw(outline);

    child_box.Draw(box.TopLeft());
}

/* Browsers */

BrowserInterface *CreateDefaultBrowser(Window *W, Asset *a, int w, int h) {
    BrowserInterface *ret = 0;
    if ((ret = CreateQTWebKitBrowser(a)))        return ret;
    if ((ret = CreateBerkeliumBrowser(a, w, h))) return ret;
    return new Browser(W, Box(w, h));
}

int PercentRefersTo(unsigned short prt, Flow *inline_context) {
    switch (prt) {
        case DOM::CSSPrimitiveValue::PercentRefersTo::FontWidth:           return inline_context->cur_attr.font->max_width;
        case DOM::CSSPrimitiveValue::PercentRefersTo::FontHeight:          return inline_context->cur_attr.font->Height();
        case DOM::CSSPrimitiveValue::PercentRefersTo::LineHeight:          return inline_context->layout.line_height;
        case DOM::CSSPrimitiveValue::PercentRefersTo::ContainingBoxHeight: return inline_context->container->h;
    }; return inline_context->container->w;
}

#ifdef LFL_LIBCSS
css_error StyleSheet::Import(void *pw, css_stylesheet *parent, lwc_string *url, uint64_t media) {
    LFL::DOM::Document *D = (LFL::DOM::Document*)((StyleSheet*)pw)->ownerDocument;
    if (D) D->parser->OpenStyleImport(LibCSS_String::ToUTF8String(url)); // XXX use parent
    INFO("libcss Import ", LibCSS_String::ToString(url));
    return CSS_INVALID; // CSS_OK;
}
#endif

string DOM::Node::HTML4Style() { 
    string ret;
    if (Node *a = getAttributeNode("align")) {
        const DOMString &v = a->nodeValue();
        if      (StringEquals(v, "right"))  StrAppend(&ret, "margin-left: auto;\n");
        else if (StringEquals(v, "center")) StrAppend(&ret, "margin-left: auto;\nmargin-right: auto;\n");
    }
    if (Node *a = getAttributeNode("background")) StrAppend(&ret, "background-image: ", a->nodeValue(), ";\n");
    if (Node *a = getAttributeNode("bgcolor"))    StrAppend(&ret, "background-color: ", a->nodeValue(), ";\n");
    if (Node *a = getAttributeNode("border"))     StrAppend(&ret, "border-width: ",     a->nodeValue(), "px;\n");
    if (Node *a = getAttributeNode("color"))      StrAppend(&ret, "color: ",            a->nodeValue(), ";\n");
    if (Node *a = getAttributeNode("height"))     StrAppend(&ret, "height: ",           a->nodeValue(), "px;\n");
    if (Node *a = getAttributeNode("text"))       StrAppend(&ret, "color: ",            a->nodeValue(), ";\n");
    if (Node *a = getAttributeNode("width"))      StrAppend(&ret, "width: ",            a->nodeValue(), "px;\n");
    return ret;
}

DOM::Node *DOM::Node::cloneNode(bool deep) {
    FATAL("not implemented");
}

DOM::Renderer *DOM::Node::AttachRender() {
    if (!render) render = AllocatorNew(ownerDocument->alloc, (DOM::Renderer), (this));
    return render;
}

void DOM::Element::setAttribute(const DOMString &name, const DOMString &value) {
    DOM::Attr *attr = AllocatorNew(ownerDocument->alloc, (DOM::Attr), (ownerDocument));
    attr->name = name;
    attr->value = value;
    setAttributeNode(attr);
}

float DOM::CSSPrimitiveValue::ConvertToPixels(float n, unsigned short u, unsigned short prt, Flow *inline_context) {
    switch (u) {
        case CSS_PX:         return          n;
        case CSS_EXS:        return          n  * inline_context->cur_attr.font->max_width;
        case CSS_EMS:        return          n  * inline_context->cur_attr.font->size;
        case CSS_IN:         return          n  * FLAGS_dots_per_inch;
        case CSS_CM:         return CMToInch(n) * FLAGS_dots_per_inch;
        case CSS_MM:         return MMToInch(n) * FLAGS_dots_per_inch;
        case CSS_PC:         return          n  /   6 * FLAGS_dots_per_inch;
        case CSS_PT:         return          n  /  72 * FLAGS_dots_per_inch;
        case CSS_PERCENTAGE: return          n  / 100 * LFL::PercentRefersTo(prt, inline_context);
    }; return 0;
}

float DOM::CSSPrimitiveValue::ConvertFromPixels(float n, unsigned short u, unsigned short prt, Flow *inline_context) {
    switch (u) {
        case CSS_PX:         return          n;
        case CSS_EXS:        return          n / inline_context->cur_attr.font->max_width;
        case CSS_EMS:        return          n / inline_context->cur_attr.font->size;
        case CSS_IN:         return          n / FLAGS_dots_per_inch;
        case CSS_CM:         return InchToCM(n / FLAGS_dots_per_inch);
        case CSS_MM:         return InchToMM(n / FLAGS_dots_per_inch);
        case CSS_PC:         return          n *   6 / FLAGS_dots_per_inch;
        case CSS_PT:         return          n *  72 / FLAGS_dots_per_inch;
        case CSS_PERCENTAGE: return          n * 100 / LFL::PercentRefersTo(prt, inline_context);
    }; return 0;                            
}

int DOM::FontSize::getFontSizeValue(Flow *flow) {
    int base = FLAGS_default_font_size; float scale = 1.2;
    switch (attr) {
        case XXSmall: return base/scale/scale/scale;    case XLarge:  return base*scale*scale;
        case XSmall:  return base/scale/scale;          case XXLarge: return base*scale*scale*scale;
        case Small:   return base/scale;                case Larger:  return flow->cur_attr.font->size*scale;
        case Medium:  return base;                      case Smaller: return flow->cur_attr.font->size/scale;
        case Large:   return base*scale;
    }
    return getPixelValue(flow);
}

void DOM::HTMLTableSectionElement::deleteRow(long index) { return parentTable->deleteRow(index); }
DOM::HTMLElement *DOM::HTMLTableSectionElement::insertRow(long index) { return parentTable->insertRow(index); }

void DOM::Renderer::UpdateStyle(Flow *F) {
    DOM::Node *n = style.node;
    CHECK(n->parentNode);
    style.is_root = n->parentNode == n->ownerDocument;
    CHECK(style.is_root || n->parentNode->render);
    DOM::Attr *inline_style = n->getAttributeNode("style");
    n->ownerDocument->style_context->Match(&style, n, style.is_root ? 0 : &n->parentNode->render->style,
                                           inline_style ? inline_style->nodeValue().c_str() : 0);

    DOM::Display              display        = style.Display();
    DOM::Position             position       = style.Position();
    DOM::Float                _float         = style.Float();
    DOM::Clear                clear          = style.Clear();
    DOM::Visibility           visibility     = style.Visibility();
    DOM::Overflow             overflow       = style.Overflow();
    DOM::TextAlign            textalign      = style.TextAlign();
    DOM::TextTransform        texttransform  = style.TextTransform();
    DOM::TextDecoration       textdecoration = style.TextDecoration();
    DOM::CSSStringValue       bgimage        = style.BackgroundImage();
    DOM::BackgroundRepeat     bgrepeat       = style.BackgroundRepeat();
    DOM::BackgroundAttachment bgattachment   = style.BackgroundAttachment();
    DOM::VerticalAlign        valign         = style.VerticalAlign();
    DOM::CSSAutoNumericValue  zindex         = style.ZIndex();
    DOM::BorderStyle          os             = style.OutlineStyle();
    DOM::BorderStyle          bs_top         = style.BorderTopStyle();
    DOM::BorderStyle          bs_bottom      = style.BorderBottomStyle();
    DOM::BorderStyle          bs_left        = style.BorderLeftStyle();
    DOM::BorderStyle          bs_right       = style.BorderRightStyle();

    display_table_element = display.TableElement();
    display_table         = display.v  == DOM::Display::Table;
    display_inline_table  = display.v  == DOM::Display::InlineTable;
    display_block         = display.v  == DOM::Display::Block;
    display_inline        = display.v  == DOM::Display::Inline;
    display_inline_block  = display.v  == DOM::Display::InlineBlock;
    display_list_item     = display.v  == DOM::Display::ListItem;
    display_none          = display.v  == DOM::Display::None || display.v == DOM::Display::Column || display.v == DOM::Display::ColGroup;

    position_relative = position.v == DOM::Position::Relative;
    position_absolute = position.v == DOM::Position::Absolute;
    position_fixed    = position.v == DOM::Position::Fixed;
    positioned = position_relative || position_absolute || position_fixed;

    if (position_absolute || position_fixed) {
        floating = float_left = float_right = 0;
    } else {
        float_left  = _float.v == DOM::Float::Left;
        float_right = _float.v == DOM::Float::Right;
        floating    = float_left || float_right;
    }

    if (position_absolute) {
        DOM::Rect r = style.Clip();
        if ((clip = !r.Null())) {
            clip_rect = Box(r.left .getPixelValue(F), r.top   .getPixelValue(F),
                            r.right.getPixelValue(F), r.bottom.getPixelValue(F));
            clip_rect.w = max(0, clip_rect.w - clip_rect.x);
            clip_rect.h = max(0, clip_rect.h - clip_rect.y);
        }
    } else clip = 0;

    overflow_auto   = overflow.v == DOM::Overflow::Auto;
    overflow_scroll = overflow.v == DOM::Overflow::Scroll;
    overflow_hidden = overflow.v == DOM::Overflow::Hidden;

    normal_flow = !position_absolute && !position_fixed && !floating && !display_table_element && !style.is_root;
    inline_block = display_inline_block || display_inline_table;
    bool block_level_element = display_block || display_list_item || display_table;
    establishes_block = style.is_root || floating || position_absolute || position_fixed ||
        inline_block || display_table_element ||
        (block_level_element && (overflow_hidden || overflow_scroll || overflow_auto));
    block_level_box = block_level_element | establishes_block;
    if (!block_level_box) overflow_auto = overflow_scroll = overflow_hidden = 0;

    right_to_left    = style.Direction     ().v == DOM::Direction::RTL;
    border_collapse  = style.BorderCollapse().v == DOM::BorderCollapse::Collapse;
    clear_left       = (clear.v == DOM::Clear::Left  || clear.v == DOM::Clear::Both);
    clear_right      = (clear.v == DOM::Clear::Right || clear.v == DOM::Clear::Both);
    hidden           = (visibility.v == DOM::Visibility::Hidden || visibility.v == DOM::Visibility::Collapse);
    valign_top       = (valign.v == DOM::VerticalAlign::Top || valign.v == DOM::VerticalAlign::TextTop);
    valign_mid       = (valign.v == DOM::VerticalAlign::Middle);
    valign_px        = (!valign.Null() && !valign.attr) ? valign.getPixelValue(F) : 0;
    bgrepeat_x       = (bgrepeat.v == DOM::BackgroundRepeat::Repeat || bgrepeat.v == DOM::BackgroundRepeat::RepeatX);
    bgrepeat_y       = (bgrepeat.v == DOM::BackgroundRepeat::Repeat || bgrepeat.v == DOM::BackgroundRepeat::RepeatY);
    bgfixed          = bgattachment  .v == DOM::BackgroundAttachment::Fixed;
    textalign_center = textalign     .v == DOM::TextAlign::Center;
    textalign_right  = textalign     .v == DOM::TextAlign::Right;
    underline        = textdecoration.v == DOM::TextDecoration::Underline;
    overline         = textdecoration.v == DOM::TextDecoration::Overline;
    midline          = textdecoration.v == DOM::TextDecoration::LineThrough;
    blink            = textdecoration.v == DOM::TextDecoration::Blink;
    uppercase        = texttransform .v == DOM::TextTransform::Uppercase;
    lowercase        = texttransform .v == DOM::TextTransform::Lowercase;
    capitalize       = texttransform .v == DOM::TextTransform::Capitalize;

    if (n->nodeType == DOM::TEXT_NODE) {
        color = style.Color().v;
        color.a() = 1.0;
    }
    if (style.bgcolor_not_inherited) {
        background_color = style.BackgroundColor().v;
    }

    os            = os       .Null() ? 0 : os.v;
    bs_t          = bs_top   .Null() ? 0 : bs_top.v;
    bs_b          = bs_bottom.Null() ? 0 : bs_bottom.v;
    bs_l          = bs_left  .Null() ? 0 : bs_left.v;
    bs_r          = bs_right .Null() ? 0 : bs_right.v;
    border_top    = Color(style.BorderTopColor   ().v, 1.0);
    border_left   = Color(style.BorderLeftColor  ().v, 1.0);
    border_right  = Color(style.BorderRightColor ().v, 1.0);
    border_bottom = Color(style.BorderBottomColor().v, 1.0);
    outline       = Color(style.OutlineColor     ().v, 1.0);

    if (!bgimage.Null() && !background_image)
        background_image = n->ownerDocument->parser->OpenImage(String::ToUTF8(bgimage.v));

    DOM::CSSNormNumericValue lineheight = style.LineHeight(), charspacing = style.LetterSpacing(), wordspacing = style.WordSpacing();
    lineheight_px  = (!lineheight .Null() && !lineheight. _norm) ? lineheight .getPixelValue(F) : 0;
    charspacing_px = (!charspacing.Null() && !charspacing._norm) ? charspacing.getPixelValue(F) : 0;
    wordspacing_px = (!wordspacing.Null() && !wordspacing._norm) ? wordspacing.getPixelValue(F) : 0;
}

Font *DOM::Renderer::UpdateFont(Flow *F) {
    DOM::FontFamily  font_family  = style.FontFamily();
    DOM::FontSize    font_size    = style.FontSize();
    DOM::FontStyle   font_style   = style.FontStyle();
    DOM::FontWeight  font_weight  = style.FontWeight();
    DOM::FontVariant font_variant = style.FontVariant();

    int font_size_px = font_size.getFontSizeValue(F);
    int font_flag = ((font_weight.v == DOM::FontWeight::_700 || font_weight.v == DOM::FontWeight::_800 || font_weight.v == DOM::FontWeight::_900 || font_weight.v == DOM::FontWeight::Bold || font_weight.v == DOM::FontWeight::Bolder) ? FontDesc::Bold : 0) |
        ((font_style.v == DOM::FontStyle::Italic || font_style.v == DOM::FontStyle::Oblique) ? FontDesc::Italic : 0);
    Font *font = font_family.attr ? Fonts::Get("", String::ToUTF8(font_family.cssText()), font_size_px, Color::white, Color::clear, font_flag) : 0;
    for (int i=0; !font && i<font_family.name.size(); i++) {
        vector<DOM::FontFace> ff;
        style.node->ownerDocument->style_context->FontFaces(font_family.name[i], &ff);
        for (int j=0; j<ff.size(); j++) {
            // INFO(j, " got font face: ", ff[j].family.name.size() ? ff[j].family.name[0] : "<NO1>",
            //      " ", ff[j].source.size() ? ff[j].source[0] : "<NO2>");
        }
    }
    return font ? font : Fonts::Get(FLAGS_default_font, "", font_size_px, Color::white);
}

void DOM::Renderer::UpdateDimensions(Flow *F) {
    DOM::CSSAutoNumericValue     width = style.   Width(),     height = style.   Height();
    DOM::CSSNoneNumericValue max_width = style.MaxWidth(), max_height = style.MaxHeight();
    DOM::CSSAutoNumericValue ml = style.MarginLeft(), mr = style.MarginRight();
    DOM::CSSAutoNumericValue mt = style.MarginTop(),  mb = style.MarginBottom();

    bool no_auto_margin = position_absolute || position_fixed || display_table_element;
    width_percent = width .IsPercent();
    width_auto    = width ._auto;
    height_auto   = height._auto;
    ml_auto       = ml    ._auto && !no_auto_margin;
    mr_auto       = mr    ._auto && !no_auto_margin;
    mt_auto       = mt    ._auto && !no_auto_margin;
    mb_auto       = mb    ._auto && !no_auto_margin;
    width_px      = width_auto  ? 0 : width .getPixelValue(F);
    height_px     = height_auto ? 0 : height.getPixelValue(F);
    ml_px         = ml_auto     ? 0 : ml    .getPixelValue(F);
    mr_px         = mr_auto     ? 0 : mr    .getPixelValue(F);
    mt_px         = mt_auto     ? 0 : mt    .getPixelValue(F);
    mb_px         = mb_auto     ? 0 : mb    .getPixelValue(F);
    bl_px         = !bs_l ? 0 : style.BorderLeftWidth  ().getPixelValue(F);
    br_px         = !bs_r ? 0 : style.BorderRightWidth ().getPixelValue(F);
    bt_px         = !bs_t ? 0 : style.BorderTopWidth   ().getPixelValue(F);
    bb_px         = !bs_b ? 0 : style.BorderBottomWidth().getPixelValue(F);
     o_px         = style.OutlineWidth     ().getPixelValue(F);
    pl_px         = style.PaddingLeft      ().getPixelValue(F);
    pr_px         = style.PaddingRight     ().getPixelValue(F);
    pt_px         = style.PaddingTop       ().getPixelValue(F);
    pb_px         = style.PaddingBottom    ().getPixelValue(F);

    bool include_floats = block_level_box && establishes_block && !floating;
    if (include_floats && width_auto) box.w = box.CenterFloatWidth(F->p.y, F->cur_line.height) - MarginWidth(); 
    else                              box.w = width_auto ? WidthAuto(F) : width_px;
    if (!max_width._none)             box.w = min(box.w, max_width.getPixelValue(F));

    if (!width_auto) UpdateMarginWidth(F, width_px);

    if (block_level_box && (width_auto || width_percent)) {
        if (normal_flow) shrink = inline_block || style.node->parentNode->render->shrink;
        else             shrink = width_auto && (floating || position_absolute || position_fixed);
    } else               shrink = normal_flow ? style.node->parentNode->render->shrink : 0;
}

void DOM::Renderer::UpdateMarginWidth(Flow *F, int w) {
    if (ml_auto && mr_auto) ml_px = mr_px = MarginCenterAuto(F, w);
    else if       (ml_auto) ml_px =         MarginLeftAuto  (F, w);
    else if       (mr_auto) mr_px =         MarginRightAuto (F, w);
}

int DOM::Renderer::ClampWidth(int w) {
    DOM::CSSNoneNumericValue max_width = style.MaxWidth();
    if (!max_width._none) w = min(w, max_width.getPixelValue(flow));
    return max(w, style.MinWidth().getPixelValue(flow));
}

void DOM::Renderer::UpdateBackgroundImage(Flow *F) {
    int bgpercent_x = 0, bgpercent_y = 0; bool bgdone_x = 0, bgdone_y = 0;
    int iw = background_image->width, ih = background_image->height; 
    DOM::CSSNumericValuePair bgposition = style.BackgroundPosition();
    if (!bgposition.Null()) {
        if (bgposition.first .IsPercent()) bgpercent_x = bgposition.first .v; else { bgposition_x = bgposition.first .getPixelValue(F);      bgdone_x = 0; }
        if (bgposition.second.IsPercent()) bgpercent_y = bgposition.second.v; else { bgposition_y = bgposition.second.getPixelValue(F) + ih; bgdone_y = 0; }
    }
    if (!bgdone_x) { float p=bgpercent_x/100.0; bgposition_x = box.w*p - iw*p; }
    if (!bgdone_y) { float p=bgpercent_y/100.0; bgposition_y = box.h*p + ih*(1-p); }
}

void DOM::Renderer::UpdateFlowAttributes(Flow *F) {
    if (charspacing_px)       F->layout.char_spacing       = charspacing_px;
    if (wordspacing_px)       F->layout.word_spacing       = wordspacing_px;
    if (lineheight_px)        F->layout.line_height        = lineheight_px;
    if (valign_px)            F->layout.valign_offset      = valign_px;
    if (1)                    F->layout.align_center       = textalign_center;
    if (1)                    F->layout.align_right        = textalign_right;
    if (1)                    F->cur_attr.underline        = underline;
    if (1)                    F->cur_attr.overline         = overline;
    if (1)                    F->cur_attr.midline          = midline;
    if (1)                    F->cur_attr.blink            = blink;
    if      (capitalize)      F->layout.word_start_char_tf = ::toupper;
    if      (uppercase)       F->layout.char_tf            = ::toupper;
    else if (lowercase)       F->layout.char_tf            = ::tolower;
    else                      F->layout.char_tf            = 0;
    if (valign_top) {}
    if (valign_mid) {}
}

void DOM::Renderer::PushScissor(const Box &w) {
    if (!tiles) return;
    if (!tile_context_opened) { tile_context_opened=1; tiles->ContextOpen(); }
    TilesPreAdd (tiles, &Tiles::PushScissor, tiles, w);
    TilesPostAdd(tiles, &GraphicsDevice::PopScissor, screen->gd);
}

void DOM::Renderer::Finish() {
    if (tile_context_opened) tiles->ContextClose();
    tile_context_opened = 0;
}

Browser::Document::~Document() { delete parser; }
Browser::Document::Document(Window *W, const Box &V) : gui(W, V), parser(new DocumentParser(this)),
    v_scrollbar(&gui, Box(V.w, V.h)),
    h_scrollbar(&gui, Box(V.w, V.h), Widget::Scrollbar::Flag::AttachedHorizontal), alloc(1024*1024) {}

void Browser::Document::Clear() {
    delete js_context;
    VectorClear(&style_sheet);
    gui.Clear();
    v_scrollbar.LayoutAttached(gui.box);
    h_scrollbar.LayoutAttached(gui.box);
    alloc.Reset();
    node = AllocatorNew(&alloc, (DOM::HTMLDocument), (parser, &alloc, &gui));
    node->style_context = AllocatorNew(&alloc, (StyleContext), (node));
    node->style_context->AppendSheet(StyleSheet::Default());
    js_context = CreateV8JSContext(js_console, node);
}

Browser::Browser(Window *W, const Box &V) : doc(W, V) {
    if (Font *maf = Fonts::Get("MenuAtlas1", "", 0, Color::black)) {
        missing_image = maf->glyph->table[12].tex;
        missing_image.width = missing_image.height = 16;
    }
    doc.Clear(); 
}

void Browser::Navigate(const string &url) {
    if (layers.empty()) return SystemBrowser::Open(url.c_str());
}

void Browser::Open(const string &url) {
    doc.parser->OpenFrame(url, (DOM::Frame*)NULL);
}

void Browser::KeyEvent(int key, bool down) {}
void Browser::MouseMoved(int x, int y) { mouse=point(x,y); }
void Browser::MouseButton(int b, bool d) { doc.gui.Input(b, mouse, d, 1); }
void Browser::MouseWheel(int xs, int ys) {}
void Browser::AnchorClicked(DOM::HTMLAnchorElement *anchor) {
    Navigate(String::ToUTF8(anchor->getAttribute("href")));
}

bool Browser::Dirty(Box *VP) {
    if (layers.empty()) return true;
    DOM::Node *n = doc.node->documentElement();
    Box viewport = Viewport();
    bool resized = viewport.w != VP->w || viewport.h != VP->h;
    if (resized && n) n->render->layout_dirty = true;
    return n && (n->render->layout_dirty || n->render->style_dirty);
}

void Browser::Draw(Box *VP) { return Draw(VP, Dirty(VP)); }
void Browser::Draw(Box *VP, bool dirty) {
    if (!VP || !doc.node || !doc.node->documentElement()) return;
    doc.gui.box = *VP;
    bool tiles = layers.size();
    int v_scrolled = doc.v_scrollbar.scrolled * doc.v_scrollbar.doc_height;
    int h_scrolled = doc.h_scrollbar.scrolled * 1000; // doc.v_scrollbar.doc_height;
    if (dirty) {
        DOM::Renderer *html_render = doc.node->documentElement()->render;
        html_render->tiles = tiles ? layers[0] : 0;
        html_render->child_bg.Reset();
        Flow flow(html_render->box.Reset(), 0, html_render->child_box.Reset());
        flow.p.y -= tiles ? 0 : v_scrolled;
        Draw(&flow, Viewport().TopLeft());
        doc.height = html_render->box.h;
        if (tiles) layers.Update();
    }
    for (int i=0; i<layers.size(); i++) layers[i]->Draw(Viewport(), !i ? h_scrolled : 0, !i ? -v_scrolled : 0);
}

void Browser::Draw(Flow *flow, const point &displacement) {
    initial_displacement = displacement;
    DrawNode(flow, doc.node->documentElement(), initial_displacement);
    if (layers.empty()) DrawScrollbar();
}

void Browser::DrawScrollbar() {
    doc.gui.Draw();
    doc.v_scrollbar.SetDocHeight(doc.height);
    doc.v_scrollbar.Update();
    doc.h_scrollbar.Update();
    doc.gui.Activate();
}

void Browser::DrawNode(Flow *flow, DOM::Node *n, const point &displacement_in) {
    if (!n) return;
    DOM::Renderer *render = n->render;
    ComputedStyle *style = &render->style;
    bool is_body  = n->htmlElementType == DOM::HTML_BODY_ELEMENT;
    bool is_table = n->htmlElementType == DOM::HTML_TABLE_ELEMENT;
    if (render->style_dirty || render->layout_dirty) { ScissorStack ss; LayoutNode(flow, n, 0); }
    if (render->display_none) return;

    if (!render->done_positioned) {
        render->done_positioned = 1;
        if (render->positioned) {
            DOM::CSSAutoNumericValue pt = style->Top(),  pb = style->Bottom();
            DOM::CSSAutoNumericValue pl = style->Left(), pr = style->Right();
            int ptp = pt.Null() ? 0 : pt.getPixelValue(flow), pbp = pb.Null() ? 0 : pb.getPixelValue(flow);
            int plp = pl.Null() ? 0 : pl.getPixelValue(flow), prp = pr.Null() ? 0 : pr.getPixelValue(flow);
            if (render->position_relative) { 
                if      (ptp) render->box.y -= ptp;
                else if (pbp) render->box.y += pbp;
                if      (plp) render->box.x += plp;
                else if (prp) render->box.x -= prp;
            } else {
                CHECK(!render->floating);
                Box viewport = Viewport();
                const Box *cont =
                    (render->position_fixed || render->absolute_parent == doc.node->documentElement()) ?
                    &viewport : &render->absolute_parent->render->box;

                if (render->height_auto && !pt._auto && !pb._auto) render->box.h = cont->h - render->MarginHeight() - ptp - pbp;
                if (render->width_auto  && !pl._auto && !pr._auto) render->box.w = cont->w - render->MarginWidth () - plp - prp;

                Box margin = Box::AddBorder(render->box, render->MarginOffset());
                if      (!pt._auto) margin.y =  0       - ptp - margin.h;
                else if (!pb._auto) margin.y = -cont->h + pbp;
                if      (!pl._auto) margin.x =  0       + plp;
                else if (!pr._auto) margin.x =  cont->w - prp - margin.w;
                render->box = Box::DelBorder(margin, render->MarginOffset());
            }
        }
    }

    point displacement = displacement_in + (render->block_level_box ? render->box.TopLeft() : point());
    if (render_log)              UpdateRenderLog(n);
    if (render->clip)            render->PushScissor(render->border.TopLeft(render->clip_rect) + displacement);
    if (render->overflow_hidden) render->PushScissor(render->content                           + displacement);
    if (!render->hidden) {
        if (render->tiles) {
            render->tiles->AddDrawableBoxArray(render->child_bg,  displacement);
            render->tiles->AddDrawableBoxArray(render->child_box, displacement);
        } else {
            render->child_bg .Draw(displacement);
            render->child_box.Draw(displacement);
        }
    }

    if (is_table) {
        DOM::HTMLTableElement *table = n->AsHTMLTableElement();
        HTMLTableRowIter(table)
            if (!row->render->Dirty()) HTMLTableColIter(row) 
                if (!cell->render->Dirty()) DrawNode(render->flow, cell, displacement);
    } else {
        DOM::NodeList *children = &n->childNodes;
        for (int i=0, l=children->length(); i<l; i++) {
            DOM::Node *child = children->item(i);
            if (!child->render->done_floated) DrawNode(render->flow, child, displacement);
        }
    }

    if (render->block_level_box) {
        for (int i=0; i<render->box.float_left .size(); i++) {
            if (render->box.float_left[i].inherited) continue;
            DOM::Node *child = (DOM::Node*)render->box.float_left[i].val;
            if (!child->render->done_positioned) {
                EX_EQ(render->box.float_left[i].w, child->render->margin.w);
                EX_EQ(render->box.float_left[i].h, child->render->margin.h);
                child->render->box.SetPosition(render->box.float_left[i].Position() - child->render->MarginPosition());
            }
            DrawNode(render->flow, child, displacement);
        }
        for (int i=0; i<render->box.float_right.size(); i++) {
            if (render->box.float_right[i].inherited) continue;
            DOM::Node *child = (DOM::Node*)render->box.float_right[i].val;
            if (!child->render->done_positioned) {
                EX_EQ(render->box.float_right[i].w, child->render->margin.w);
                EX_EQ(render->box.float_right[i].h, child->render->margin.h);
                child->render->box.SetPosition(render->box.float_right[i].Position() - child->render->MarginPosition());
            }
            DrawNode(render->flow, child, displacement);
        }
        if (render_log) render_log->indent -= 2;
    }
    render->Finish();
}

DOM::Node *Browser::LayoutNode(Flow *flow, DOM::Node *n, bool reflow) {
    if (!n) return 0;
    DOM::Renderer *render = n->render;
    ComputedStyle *style = &render->style;
    bool is_table = n->htmlElementType == DOM::HTML_TABLE_ELEMENT;
    bool table_element = render->display_table_element;

    if (!reflow) {
        if (render->style_dirty) render->UpdateStyle(flow);
        if (!style->is_root) render->tiles = n->parentNode->render->tiles;
        render->done_positioned = render->done_floated = 0;
        render->max_child_i = -1;
    } else if (render->done_floated) return 0;
    if (render->display_none) { render->style_dirty = render->layout_dirty = 0; return 0; }

    if (render->position_absolute) {
        DOM::Node *ap = n->parentNode;
        while (ap && ap->render && (!ap->render->positioned || !ap->render->block_level_box)) ap = ap->parentNode;
        render->absolute_parent = (ap && ap->render) ? ap : doc.node->documentElement();
    } else render->absolute_parent = 0;

    render->parent_flow = render->absolute_parent ? render->absolute_parent->render->flow : flow;
    if (!table_element) {
        render->box = style->is_root ? Viewport() : Box(max(0, flow->container->w), 0);
        render->UpdateDimensions(render->parent_flow);
    }

    render->clear_height = flow->container->AsFloatContainer()->ClearFloats(flow->p.y, flow->cur_line.height, render->clear_left, render->clear_right);
    if (render->clear_height) flow->AppendVerticalSpace(render->clear_height);

    if (style->font_not_inherited || !(render->child_flow.cur_attr.font = flow->cur_attr.font)) render->child_flow.cur_attr.font = render->UpdateFont(flow);
    if (style->is_root) flow->SetFont(render->child_flow.cur_attr.font);

    if (render->block_level_box) {
        if (!table_element) {
            if (!flow->cur_line.fresh && (render->normal_flow || render->position_absolute) && 
                !render->inline_block && !render->floating && !render->position_absolute) flow->AppendNewlines(1);
            render->box.y = flow->p.y;
        }
        render->child_flow = Flow(&render->box, render->child_flow.cur_attr.font, render->child_box.Reset());
        render->flow = &render->child_flow;
        if (!reflow) {
            render->box.Reset();
            if (render->normal_flow && !render->inline_block) render->box.InheritFloats(flow->container->AsFloatContainer());
            if (render->position_fixed && layers.size()) render->tiles = layers[1];
        }
    } else render->flow = render->parent_flow;
    render->UpdateFlowAttributes(render->flow);

    if (n->nodeType == DOM::TEXT_NODE) {
        DOM::Text *T = n->AsText();
        if (1)                            render->flow->SetFGColor(&render->color);
        // if (style->bgcolor_not_inherited) render->flow->SetBGColor(&render->background_color);
        render->flow->AppendText(T->data);
    } else if ((n->htmlElementType == DOM::HTML_IMAGE_ELEMENT) ||
               (n->htmlElementType == DOM::HTML_INPUT_ELEMENT && StringEquals(n->AsElement()->getAttribute("type"), "image"))) {
        Texture *tex = n->htmlElementType == DOM::HTML_IMAGE_ELEMENT ? n->AsHTMLImageElement()->tex.get() : n->AsHTMLInputElement()->image_tex.get();
        bool missing = !tex || !tex->ID;
        if (missing) tex = &missing_image;

        point dim(n->render->width_px, n->render->height_px);
        if      (!dim.x && !dim.y)                dim   = point(tex->width, tex->height);
        if      ( dim.x && !dim.y && tex->width ) dim.y = RoundF((float)tex->height/tex->width *dim.x);
        else if (!dim.x &&  dim.y && tex->height) dim.x = RoundF((float)tex->width /tex->height*dim.y);

        bool add_margin = !n->render->floating && !n->render->position_absolute && !n->render->position_fixed;
        Border margin = add_margin ? n->render->MarginOffset() : Border();
        if (missing) {
            point d(max(0, dim.x - (int)tex->width), max(0, dim.y - (int)tex->height));
            margin += Border(d.y/2, d.x/2, d.y/2, d.x/2);
            dim -= d;
        }
        render->flow->SetFGColor(&Color::white);
        render->flow->AppendBox(dim.x, dim.y, tex);
        render->box = render->flow->out->data.back().box;
    } 
    else if (n->htmlElementType == DOM::HTML_BR_ELEMENT) render->flow->AppendNewlines(1);
    else if (is_table) LayoutTable(render->flow, n->AsHTMLTableElement());

    vector<Flow::RollbackState> child_flow_state;
    DOM::NodeList *children = &n->childNodes;
    for (int i=0, l=children->length(); !is_table && i<l; i++) {
        if (render->block_level_box) child_flow_state.push_back(render->flow->GetRollbackState());

        DOM::Node *child = children->item(i);
        bool reflow_child = i <= render->max_child_i;
        if (render->style_dirty  && !reflow_child) child->render->style_dirty  = 1;
        if (render->layout_dirty && !reflow_child) child->render->layout_dirty = 1;
        DOM::Node *descendent_float = LayoutNode(render->flow, child, reflow_child);
        Max(&render->max_child_i, i);
        if (!descendent_float) continue;
        if (!render->block_level_box) return descendent_float;

        Box df_margin;
        DOM::Renderer *dfr = descendent_float->render;
        CHECK_EQ(dfr->parent_flow, render->flow);
        int fy = render->flow->p.y - max(0, dfr->margin.h - render->flow->cur_line.height);
        render->box.AddFloat(fy, dfr->margin.w, dfr->margin.h, dfr->float_right, descendent_float, &df_margin);
        dfr->done_floated = 1;

        bool same_line = !render->flow->cur_line.fresh;
        int descendent_float_newlines = render->flow->out->line.size();
        while (child_flow_state.size() && render->flow->out->line.size() == descendent_float_newlines) {
            render->flow->Rollback(PopBack(child_flow_state));
            same_line = (render->flow->out->line.size() == descendent_float_newlines && !render->flow->cur_line.fresh);
        }
        EX_EQ(same_line, false);
        i = child_flow_state.size() - 1;
    }

    if (render->block_level_box) {
        render->flow->Complete();
        int block_height = X_or_Y(render->height_px, max(render->flow->Height(), render->establishes_block ? render->box.FloatHeight() : 0));
        int block_width  = render->shrink ? render->ClampWidth(render->flow->max_line_width) : render->box.w;
        if (render->normal_flow) {
            if (render->inline_block) render->parent_flow->AppendBox  (block_width, block_height, render->MarginOffset(), &render->box);
            else                      render->parent_flow->AppendBlock(block_width, block_height, render->MarginOffset(), &render->box); 
            if (!render->establishes_block) render->box.AddFloatsToParent((FloatContainer*)render->parent_flow->container->AsFloatContainer());
        } else {
            render->box.SetDimension(point(block_width, block_height));
            if (style->is_root || render->position_absolute || table_element) render->box.y -= block_height;
        }
    }

    if (render->background_image) render->UpdateBackgroundImage(flow);
    if (style->bgcolor_not_inherited || render->inline_block || render->clip || render->floating
        || render->bs_t || render->bs_b || render->bs_r || render->bs_l) {
        render->content = Box(0, -render->box.h, render->box.w, render->box.h);
        render->padding = Box::AddBorder(render->content, render->PaddingOffset());
        render->border  = Box::AddBorder(render->content, render->BorderOffset());
        render->margin  = Box::AddBorder(render->content, render->MarginOffset());
    }
    LayoutBackground(n);

    render->style_dirty = render->layout_dirty = 0;
    return (render->floating && !render->done_floated) ? n : 0;
}

void Browser::LayoutBackground(DOM::Node *n) {
    bool is_body = n->htmlElementType == DOM::HTML_BODY_ELEMENT;
    DOM::Renderer *render = n->render;
    ComputedStyle *style = &render->style;
    Flow flow(0, 0, render->child_bg.Reset());

    const Box                                                         *box = &render->content;
    if      (is_body)                                                  box = &render->margin;
    else if (render->block_level_box || render->display_table_element) box = &render->padding;

    if (style->bgcolor_not_inherited && render->background_color.A()) {
        if (is_body) screen->gd->ClearColor(Color(render->background_color, 0.0));
        else {
            flow.SetFGColor(&render->background_color);
            flow.out->PushBack(*box, flow.cur_attr, Singleton<BoxFilled>::Get());
        }
    } else if (is_body) screen->gd->ClearColor(Color(1.0, 1.0, 1.0, 0.0));

    if (render->background_image && render->background_image->width && 
                                    render->background_image->height) {
        Box bgi(box->x     + render->bgposition_x, 
                box->top() - render->bgposition_y, 
                render->background_image->width,
                render->background_image->height);

        flow.cur_attr.Clear();
        flow.cur_attr.scissor = box;
        // flow.cur_attr.tex = &render->background_image;

        int start_x = !n->render->bgrepeat_x ? bgi.x       : box->x - bgi.w + (n->render->bgposition_x % bgi.w);
        int start_y = !n->render->bgrepeat_y ? bgi.y       : box->y - bgi.h + (n->render->bgposition_y % bgi.h);
        int end_x   = !n->render->bgrepeat_x ? bgi.right() : box->right();
        int end_y   = !n->render->bgrepeat_y ? bgi.top  () : box->top  ();
        for     (int y = start_y; y < end_y; y += bgi.h) { bgi.y = y;
            for (int x = start_x; x < end_x; x += bgi.w) { bgi.x = x;
                flow.out->PushBack(bgi, flow.cur_attr, render->background_image.get());
            }
        }
    }

    flow.cur_attr.Clear();
    if (render->bs_t) {
        flow.SetFGColor(&render->border_top);
        flow.out->PushBack(Box(render->border.x, render->padding.top(), render->border.w, render->border.top() - render->padding.top()),
                           flow.cur_attr, Singleton<BoxFilled>::Get());
    }
    if (render->bs_b) {
        flow.SetFGColor(&render->border_bottom);
        flow.out->PushBack(Box(render->border.x, render->border.y, render->border.w, render->padding.y - render->border.y),
                           flow.cur_attr, Singleton<BoxFilled>::Get());
    }
    if (render->bs_r) {
        flow.SetFGColor(&render->border_right);
        flow.out->PushBack(Box(render->padding.right(), render->border.y, render->border.right() - render->padding.right(), render->border.h),
                           flow.cur_attr, Singleton<BoxFilled>::Get());
    }
    if (render->bs_l) {
        flow.SetFGColor(&render->border_left);
        flow.out->PushBack(Box(render->border.x, render->border.y, render->padding.x - render->border.x, render->border.h),
                           flow.cur_attr, Singleton<BoxFilled>::Get());
    }
}

void Browser::LayoutTable(Flow *flow, DOM::HTMLTableElement *n) {
    UpdateTableStyle(flow, n);
    TableFlow table(flow);
    table.Select();

    HTMLTableIterColGroup(n)
        table.SetMinColumnWidth(j, col->render->MarginBoxWidth(), X_or_1(atoi(col->getAttribute("span"))));

    HTMLTableRowIter(n) {
        HTMLTableColIter(row) {
            DOM::Renderer *cr = cell->render;
            TableFlow::Column *cj = table.SetCellDim(j, cr->MarginBoxWidth(), cr->cell_colspan, cr->cell_rowspan);
            if (!n->render->width_auto || !cr->width_auto) continue;
            cr->box = Box(0,0); LayoutNode(flow, cell, 0); Max(&cj->max_width, cr->flow->max_line_width + cr->MarginWidth()); 
            cr->box = Box(1,0); LayoutNode(flow, cell, 0); Max(&cj->min_width, cr->flow->max_line_width + cr->MarginWidth());
        }
        table.NextRowDim();
    }

    n->render->box.w = table.ComputeWidth(n->render->width_auto ? 0 : n->render->width_px);
    if (n->render->width_auto) n->render->UpdateMarginWidth(n->render->parent_flow, n->render->box.w);

    HTMLTableRowIter(n) {
        HTMLTableColIter(row) {
            DOM::Renderer *cr = cell->render;
            table.AppendCell(j, &cr->box, cr->cell_colspan);
            cr->box.DelBorder(cr->MarginOffset().LeftRight());
            LayoutNode(flow, cell, 0);
            table.SetCellHeight(j, cr->box.h + cr->MarginHeight(), cell, cr->cell_colspan, cr->cell_rowspan);
        }
        row->render->row_height = table.AppendRow();
        row->render->layout_dirty = 0;
    }
}

void Browser::UpdateTableStyle(Flow *flow, DOM::Node *n) {
    for (int i=0, l=n->childNodes.length(); i<l; i++) {
        DOM::Node *child = n->childNodes.item(i);
        DOM::HTMLTableCellElement *cell = child->AsHTMLTableCellElement();
        if (n->render->style_dirty) child->render->style_dirty = 1;
        if (!child->AsHTMLTableRowElement() && !child->AsHTMLTableSectionElement() &&
            !child->AsHTMLTableColElement() && !child->AsHTMLTableCaptionElement() && !cell) continue;

        if (child->render->style_dirty) {
            child->render->UpdateStyle(flow);
            child->render->display_table_element = 1;
            child->render->tiles = child->parentNode->render->tiles;
            child->render->UpdateDimensions(flow);
            if (cell) {
                cell->render->cell_colspan = X_or_1(atoi(cell->getAttribute("colspan")));
                cell->render->cell_rowspan = X_or_1(atoi(cell->getAttribute("rowspan")));
            }
        }
        UpdateTableStyle(flow, child);
    }
    n->render->style_dirty = 0;
}

void Browser::UpdateRenderLog(DOM::Node *n) {
    DOM::Renderer *render = n->render;
    if (render_log->data.empty()) {
        CHECK(render->style.is_root);
        Box viewport = Viewport();
        render_log->indent = 2;
        render_log->data = StrCat("layer at (0,0) size ",          viewport.w, "x", viewport.h,
                                  "\n  RenderView at (0,0) size ", viewport.w, "x", viewport.h,
                                  "\nlayer at (0,0) size ", render->box.w, "x", render->box.h, "\n");
    }
    if (render->block_level_box) {
        StrAppend(&render_log->data, string(render_log->indent, ' '), "RenderBlock {", toupper(n->nodeName()), "} at (",
                  render->box.x, ",", ScreenToWebKitY(render->box), ") size ", render->box.w, "x", render->box.h, "\n");
        render_log->indent += 2;
    }
    if (n->nodeType == DOM::TEXT_NODE) {
        StrAppend(&render_log->data, string(render_log->indent, ' '),
                  "text run at (", render->box.x, ",",    ScreenToWebKitY(render->box),
                  ") width ",      render->box.w, ": \"", n->nodeValue(), "\"\n");
    }
}

struct BrowserController : public InputController {
    BrowserInterface *browser;
    BrowserController(BrowserInterface *B) : browser(B) {}
    void Input(InputEvent::Id event, bool down) {
        int key = InputEvent::GetKey(event);
        if (key)                                browser->KeyEvent(key, down);
        else if (event == Mouse::Event::Motion) browser->MouseMoved(screen->mouse.x, screen->mouse.y);
        else if (event == Mouse::Event::Wheel)  browser->MouseWheel(0, down*32);
        else                                    browser->MouseButton(event, down);
    }
};

#ifdef LFL_QT
extern QApplication *lfl_qapp;
class QTWebKitBrowser : public QObject, public BrowserInterface {
    Q_OBJECT
  public:
    Asset* asset;
    int W, H, lastx, lasty, lastd;
    QWebPage page;

    QTWebKitBrowser(Asset *a) : lastx(0), lasty(0), lastd(0) {
        asset = a;
        Resize(screen->width, screen->height);
        lfl_qapp->connect(&page, SIGNAL(repaintRequested(const QRect&)),           this, SLOT(ViewUpdate(const QRect &)));
        lfl_qapp->connect(&page, SIGNAL(scrollRequested(int, int, const QRect &)), this, SLOT(Scroll(int, int, const QRect &)));
    }
    void Resize(int win, int hin) {
        W = win; H = hin;
        page.setViewportSize(QSize(W, H));
        if (!asset->tex.ID) asset->tex.CreateBacked(W, H);
        else                asset->tex.Resize(W, H);
    }
    void Open(const string &url) { page.mainFrame()->load(QUrl(url.c_str())); }
    void MouseMoved(int x, int y) {
        lastx = x; lasty = screen->height - y; 
        QMouseEvent me(QEvent::MouseMove, QPoint(lastx, lasty), Qt::NoButton,
                       lastd ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&page, &me);
    }
    void MouseButton(int b, bool d) {
        lastd = d;
        QMouseEvent me(QEvent::MouseButtonPress, QPoint(lastx, lasty), Qt::LeftButton,
                       lastd ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&page, &me);
    }
    void MouseWheel(int xs, int ys) {}
    void KeyEvent(int key, bool down) {}
    void BackButton() { page.triggerAction(QWebPage::Back); }
    void ForwardButton() { page.triggerAction(QWebPage::Forward);  }
    void RefreshButton() { page.triggerAction(QWebPage::Reload); }
    string GetURL() { return page.mainFrame()->url().toString().toLocal8Bit().data(); }
    void Draw(Box *w) {
        asset->tex.DrawCrimped(*w, 1, 0, 0);
        QPoint scroll = page.currentFrame()->scrollPosition();
        QWebHitTestResult hit_test = page.currentFrame()->hitTestContent(QPoint(lastx, lasty));
        QWebElement hit = hit_test.element();
        QRect rect = hit.geometry();
        Box hitwin(rect.x() - scroll.x(), w->h-rect.y()-rect.height()+scroll.y(), rect.width(), rect.height());
        screen->gd->SetColor(Color::blue);
        hitwin.Draw();
    }
  public slots:
    void Scroll(int dx, int dy, const QRect &rect) { ViewUpdate(QRect(0, 0, W, H)); }
    void ViewUpdate(const QRect &dirtyRect) {
        QImage image(page.viewportSize(), QImage::Format_ARGB32);
        QPainter painter(&image);
        page.currentFrame()->render(&painter, dirtyRect);
        painter.end();

        int instride = image.bytesPerLine(), bpp = Pixel::size(asset->tex.pf);
        int mx = dirtyRect.x(), my = dirtyRect.y(), mw = dirtyRect.width(), mh = dirtyRect.height();
        const unsigned char *in = (const unsigned char *)image.bits();
        unsigned char *buf = asset->tex.buf;
        screen->gd->BindTexture(GraphicsDevice::Texture2D, asset->tex.ID);
        for (int j = 0; j < mh; j++) {
            int src_ind = (j + my) * instride + mx * bpp;
            int dst_ind = ((j + my) * asset->tex.width + mx) * bpp;
            if (true) VideoResampler::RGB2BGRCopyPixels(buf+dst_ind, in+src_ind, mw, bpp);
            else                                 memcpy(buf+dst_ind, in+src_ind, mw*bpp);
        }
        asset->tex.UpdateGL(Box(0, my, asset->tex.width, dirtyRect.height()));
    }
};

BrowserInterface *CreateQTWebKitBrowser(Asset *a) { return new QTWebKitBrowser(a); }

#include "gui.moc"

/* Dialogs */

void Dialog::MessageBox(const string &n) {
    Mouse::ReleaseFocus();
    QMessageBox *msg = new QMessageBox();
    msg->setAttribute(Qt::WA_DeleteOnClose);
    msg->setText("MesssageBox");
    msg->setInformativeText(n.c_str());
    msg->setModal(false);
    msg->open();
}
void Dialog::TextureBox(const string &n) {}

#else /* LFL_QT */
BrowserInterface *CreateQTWebKitBrowser(Asset *a) { return 0; }

void Dialog::MessageBox(const string &n) {
    Mouse::ReleaseFocus();
    new MessageBoxDialog(n);
}
void Dialog::TextureBox(const string &n) {
    Mouse::ReleaseFocus();
    new TextureBoxDialog(n);
}
#endif /* LFL_QT */

#ifdef LFL_BERKELIUM
struct BerkeliumModule : public Module {
    BerkeliumModule() { app->LoadModule(this); }
    int Frame(unsigned t) { Berkelium::update(); return 0; }
    int Free() { Berkelium::destroy(); return 0; }
    int Init() {
        const char *homedir = LFAppDownloadDir();
        INFO("berkelium init");
        Berkelium::init(
#ifdef _WIN32
            homedir ? Berkelium::FileString::point_to(wstring(homedir).c_str(), wstring(homedir).size()) :
#else
            homedir ? Berkelium::FileString::point_to(homedir, strlen(homedir)) :
#endif
            Berkelium::FileString::empty());
        return 0;
    }
};

struct BerkeliumBrowser : public BrowserInterface, public Berkelium::WindowDelegate {
    Asset *asset; int W, H; Berkelium::Window *window;
    BerkeliumBrowser(Asset *a, int win, int hin) : asset(a), window(0) { 
        Singleton<BerkeliumModule>::Get();
        Berkelium::Context* context = Berkelium::Context::create();
        window = Berkelium::Window::create(context);
        delete context;
        window->setDelegate(this);
        resize(win, hin); 
    }

    void Draw(Box *w) { glOverlay(*w, asset, 1); }
    void Open(const string &url) { window->navigateTo(Berkelium::URLString::point_to(url.data(), url.length())); }
    void MouseMoved(int x, int y) { window->mouseMoved((float)x/screen->width*W, (float)(screen->height-y)/screen->height*H); }
    void MouseButton(int b, bool down) {
        if (b == BIND_MOUSE1 && down) {
            int click_x = screen->mouse.x/screen->width*W, click_y = (screen->height - screen->mouse.y)/screen->height*H;
            string js = WStringPrintf(L"lfapp_browser_click(document.elementFromPoint(%d, %d).innerHTML)", click_x, click_y);
            window->executeJavascript(Berkelium::WideString::point_to((wchar_t *)js.c_str(), js.size()));
        }
        window->mouseButton(b == BIND_MOUSE1 ? 0 : 1, down);
    }
    void MouseWheel(int xs, int ys) { window->mouseWheel(xs, ys); }
    void KeyEvent(int key, bool down) {
        int bk = -1;
        switch (key) {
            case Key::PageUp:    bk = 0x21; break;
            case Key::PageDown:  bk = 0x22; break;
            case Key::Left:      bk = 0x25; break;
            case Key::Up:        bk = 0x26; break;
            case Key::Right:     bk = 0x27; break;
            case Key::Down:      bk = 0x28; break;
            case Key::Delete:    bk = 0x2E; break;
            case Key::Backspace: bk = 0x2E; break;
            case Key::Return:    bk = '\r'; break;
            case Key::Tab:       bk = '\t'; break;
        }

        int mods = ctrl_key_down() ? Berkelium::CONTROL_MOD : 0 | shift_key_down() ? Berkelium::SHIFT_MOD : 0;
        window->keyEvent(down, mods, bk != -1 ? bk : key, 0);
        if (!down || bk != -1) return;

        wchar_t wkey[2] = { key, 0 };
        window->textEvent(wkey, 1);
    }

    void Resize(int win, int hin) {
        W = win; H = hin;
        window->resize(W, H);
        if (!asset->texID) PixelBuffer::create(W, H, asset);
        else               asset->tex.resize(W, H, asset->texcoord);
    }

    virtual void onPaint(Berkelium::Window* wini, const unsigned char *in_buf, const Berkelium::Rect &in_rect,
        size_t num_copy_rects, const Berkelium::Rect* copy_rects, int dx, int dy, const Berkelium::Rect& scroll_rect) {
        unsigned char *buf = asset->tex.buf
        int bpp = Pixel::size(asset->tex.pf);
        screen->gd->BindTexture(GL_TEXTURE_2D, asset->texID);

        if (dy < 0) {
            const Berkelium::Rect &r = scroll_rect;
            for (int j = -dy, h = r.height(); j < h; j++) {
                int src_ind = ((j + r.top()     ) * asset->tex.w + r.left()) * bpp;
                int dst_ind = ((j + r.top() + dy) * asset->tex.w + r.left()) * bpp;
                memcpy(buf+dst_ind, buf+src_ind, r.width()*bpp);
            }
        } else if (dy > 0) {
            const Berkelium::Rect &r = scroll_rect;
            for (int j = r.height() - dy; j >= 0; j--) {
                int src_ind = ((j + r.top()     ) * asset->tex.w + r.left()) * bpp;
                int dst_ind = ((j + r.top() + dy) * asset->tex.w + r.left()) * bpp;                
                memcpy(buf+dst_ind, buf+src_ind, r.width()*bpp);
            }
        }
        if (dx) {
            const Berkelium::Rect &r = scroll_rect;
            for (int j = 0, h = r.height(); j < h; j++) {
                int src_ind = ((j + r.top()) * asset->tex.w + r.left()     ) * bpp;
                int dst_ind = ((j + r.top()) * asset->tex.w + r.left() - dx) * bpp;
                memcpy(buf+dst_ind, buf+src_ind, r.width()*bpp);
            }
        }
        for (int i = 0; i < num_copy_rects; i++) {
            const Berkelium::Rect &r = copy_rects[i];
            for(int j = 0, h = r.height(); j < h; j++) {
                int dst_ind = ((j + r.top()) * asset->tex.w + r.left()) * bpp;
                int src_ind = ((j + (r.top() - in_rect.top())) * in_rect.width() + (r.left() - in_rect.left())) * bpp;
                memcpy(buf+dst_ind, in_buf+src_ind, r.width()*bpp);
            }
            asset->tex.flush(0, r.top(), asset->tex.w, r.height());        
        }

        // for (Berkelium::Window::FrontToBackIter iter = wini->frontIter(); 0 && iter != wini->frontEnd(); iter++) { Berkelium::Widget *w = *iter; }
    }

    virtual void onCreatedWindow(Berkelium::Window *win, Berkelium::Window *newWindow, const Berkelium::Rect &initialRect) { newWindow->setDelegate(this); }
    virtual void onAddressBarChanged(Berkelium::Window *win, Berkelium::URLString newURL) { INFO("onAddressBarChanged: ", newURL.data()); }
    virtual void onStartLoading(Berkelium::Window *win, Berkelium::URLString newURL) { INFO("onStartLoading: ", newURL.data()); }
    virtual void onLoad(Berkelium::Window *win) { INFO("onLoad"); }
    virtual void onJavascriptCallback(Berkelium::Window *win, void* replyMsg, Berkelium::URLString url, Berkelium::WideString funcName, Berkelium::Script::Variant *args, size_t numArgs) {
        string fname = WideStringToString(funcName);
        if (fname == "lfapp_browser_click" && numArgs == 1 && args[0].type() == args[0].JSSTRING) {
            string a1 = WideStringToString(args[0].toString());
            INFO("jscb: ", fname, " ", a1);
        } else {
            INFO("jscb: ", fname);
        }
        if (replyMsg) win->synchronousScriptReturn(replyMsg, numArgs ? args[0] : Berkelium::Script::Variant());
    }
    virtual void onRunFileChooser(Berkelium::Window *win, int mode, Berkelium::WideString title, Berkelium::FileString defaultFile) { win->filesSelected(NULL); }
    virtual void onNavigationRequested(Berkelium::Window *win, Berkelium::URLString newURL, Berkelium::URLString referrer, bool isNewWindow, bool &cancelDefaultAction) {}

    static string WideStringToString(const Berkelium::WideString& in) {
        string out; out.resize(in.size());
        for (int i = 0; i < in.size(); i++) out[i] = in.data()[i];     
        return out;
    }
};

BrowserInterface *CreateBerkeliumBrowser(Asset *a, int W, int H) {
    BerkeliumBrowser *browser = new BerkeliumBrowser(a, W, H);
    Berkelium::WideString click = Berkelium::WideString::point_to(L"lfapp_browser_click");
    browser->window->bind(click, Berkelium::Script::Variant::bindFunction(click, true));
    return browser;
}
#else /* LFL_BERKELIUM */
BrowserInterface *CreateBerkeliumBrowser(Asset *a, int W, int H) { return 0; }
#endif /* LFL_BERKELIUM */

}; // namespace LFL
#ifdef LFL_BOOST
#include <boost/graph/fruchterman_reingold.hpp>
#include <boost/graph/random_layout.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/simple_point.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/random/linear_congruential.hpp>
#include <boost/progress.hpp>
#include <boost/shared_ptr.hpp>

using namespace boost;

struct BoostForceDirectedLayout {
    typedef adjacency_list<listS, vecS, undirectedS, property<vertex_name_t, std::string> > Graph;
    typedef graph_traits<Graph>::vertex_descriptor Vertex;

    typedef boost::rectangle_topology<> topology_type;
    typedef topology_type::point_type point_type;
    typedef std::vector<point_type> PositionVec;
    typedef iterator_property_map<PositionVec::iterator, property_map<Graph, vertex_index_t>::type> PositionMap;
    
    Graph g;
    vector<Vertex> v;

    void Clear() { g.clear(); v.clear(); }
    void AssignVertexPositionToTargetCenter(const HelperGUI *gui, PositionMap *position, int vertex_offset) {
        for (int i = 0; i < gui->label.size(); ++i) {
            (*position)[v[i*2+vertex_offset]][0] = gui->label[i].target_center.x;
            (*position)[v[i*2+vertex_offset]][1] = gui->label[i].target_center.y;
        }
    }
    void AssignVertexPositionToLabelCenter(const HelperGUI *gui, PositionMap *position, int vertex_offset) {
        for (int i = 0; i < gui->label.size(); ++i) {
            (*position)[v[i*2+vertex_offset]][0] = gui->label[i].label_center.x;
            (*position)[v[i*2+vertex_offset]][1] = gui->label[i].label_center.y;
        }
    }
    void Layout(HelperGUI *gui) {
        Clear();

        for (int i = 0; i < gui->label.size()*2; ++i) v.push_back(add_vertex(StringPrintf("%d", i), g));
        for (int i = 0; i < gui->label.size()  ; ++i) add_edge(v[i*2], v[i*2+1], g);

        PositionVec position_vec(num_vertices(g));
        PositionMap position(position_vec.begin(), get(vertex_index, g));

        minstd_rand gen;
        topology_type topo(gen, 0, 0, screen->width, screen->height);
        if (0) random_graph_layout(g, position, topo);
        else AssignVertexPositionToLabelCenter(gui, &position, 1);

        for (int i = 0; i < 300; i++) {
            AssignVertexPositionToTargetCenter(gui, &position, 0);
            fruchterman_reingold_force_directed_layout(g, position, topo, cooling(linear_cooling<double>(1)));
        }

        for (int i = 0; i < gui->label.size(); ++i) {
            HelperGUI::Label *l = &gui->label[i];
            l->label_center.x = position[v[i*2+1]][0];
            l->label_center.y = position[v[i*2+1]][1];
            l->AssignLabelBox();
        }
    }
};
#endif // LFL_BOOST
namespace LFL {

void HelperGUI::ForceDirectedLayout() {
#ifdef LFL_BOOST
    BoostForceDirectedLayout().Layout(this);
#endif
}

void HelperGUI::Draw() {
    for (auto i = label.begin(); i != label.end(); ++i) {
        glLine(point(i->label_center.x, i->label_center.y),
               point(i->target_center.x, i->target_center.y), &font->fg);
        screen->gd->FillColor(Color::black);
        Box::AddBorder(i->label, 4, 0).Draw();
        font->Draw(i->description, point(i->label.x, i->label.y));
    }
}

}; // namespace LFL
