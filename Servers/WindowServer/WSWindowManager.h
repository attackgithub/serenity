#pragma once

#include <SharedGraphics/Rect.h>
#include <SharedGraphics/Color.h>
#include <SharedGraphics/Painter.h>
#include <SharedGraphics/DisjointRectSet.h>
#include <AK/HashTable.h>
#include <AK/InlineLinkedList.h>
#include <AK/WeakPtr.h>
#include <AK/HashMap.h>
#include "WSMessageReceiver.h"
#include "WSMenuBar.h"
#include <WindowServer/WSWindowSwitcher.h>
#include <WindowServer/WSWindowType.h>
#include <WindowServer/WSWindow.h>
#include <WindowServer/WSCursor.h>
#include <AK/CircularQueue.h>

class WSAPIClientRequest;
class WSScreen;
class WSMenuBar;
class WSMouseEvent;
class WSClientWantsToPaintMessage;
class WSWindow;
class WSClientConnection;
class WSWindowSwitcher;
class GraphicsBitmap;

enum class ResizeDirection { None, Left, UpLeft, Up, UpRight, Right, DownRight, Down, DownLeft };

class WSWindowManager : public WSMessageReceiver {
    friend class WSWindowSwitcher;
public:
    static WSWindowManager& the();

    WSWindowManager();
    virtual ~WSWindowManager() override;

    void add_window(WSWindow&);
    void remove_window(WSWindow&);

    void notify_title_changed(WSWindow&);
    void notify_rect_changed(WSWindow&, const Rect& oldRect, const Rect& newRect);
    void notify_client_changed_app_menubar(WSClientConnection&);

    WSWindow* active_window() { return m_active_window.ptr(); }
    const WSClientConnection* active_client() const;

    WSWindow* highlight_window() { return m_highlight_window.ptr(); }
    void set_highlight_window(WSWindow*);

    void move_to_front_and_make_active(WSWindow&);

    void invalidate_cursor();
    void draw_cursor();
    void draw_menubar();
    void draw_window_switcher();

    Rect menubar_rect() const;
    WSMenuBar* current_menubar() { return m_current_menubar.ptr(); }
    void set_current_menubar(WSMenuBar*);
    WSMenu* current_menu() { return m_current_menu.ptr(); }
    void set_current_menu(WSMenu*);

    void invalidate(const WSWindow&);
    void invalidate(const WSWindow&, const Rect&);
    void invalidate(const Rect&, bool should_schedule_compose_event = true);
    void invalidate();
    void recompose_immediately();
    void flush(const Rect&);

    const Font& font() const;
    const Font& window_title_font() const;
    const Font& menu_font() const;
    const Font& app_menu_font() const;

    void close_menu(WSMenu&);
    void close_menubar(WSMenuBar&);
    Color menu_selection_color() const { return m_menu_selection_color; }
    int menubar_menu_margin() const;

    void set_resolution(int width, int height);

    bool set_wallpaper(const String& path);
    String wallpaper_path() const { return m_wallpaper_path; }

    const WSCursor& active_cursor() const;
    Rect current_cursor_rect() const;

private:
    void process_mouse_event(WSMouseEvent&, WSWindow*& event_window);
    bool process_ongoing_window_resize(WSMouseEvent&, WSWindow*& event_window);
    bool process_ongoing_window_drag(WSMouseEvent&, WSWindow*& event_window);
    void handle_menu_mouse_event(WSMenu&, WSMouseEvent&);
    void handle_menubar_mouse_event(WSMouseEvent&);
    void handle_close_button_mouse_event(WSWindow&, WSMouseEvent&);
    void start_window_resize(WSWindow&, WSMouseEvent&);
    void start_window_drag(WSWindow&, WSMouseEvent&);
    void handle_client_request(WSAPIClientRequest&);
    void set_active_window(WSWindow*);
    void set_hovered_window(WSWindow*);
    template<typename Callback> IterationDecision for_each_visible_window_of_type_from_back_to_front(WSWindowType, Callback);
    template<typename Callback> IterationDecision for_each_visible_window_of_type_from_front_to_back(WSWindowType, Callback);
    template<typename Callback> IterationDecision for_each_visible_window_from_front_to_back(Callback);
    template<typename Callback> IterationDecision for_each_visible_window_from_back_to_front(Callback);
    template<typename Callback> void for_each_active_menubar_menu(Callback);
    void close_current_menu();
    virtual void on_message(WSMessage&) override;
    void compose();
    void paint_window_frame(WSWindow&);
    void flip_buffers();
    void tick_clock();

    WSScreen& m_screen;
    Rect m_screen_rect;

    Color m_background_color;
    Color m_active_window_border_color;
    Color m_active_window_border_color2;
    Color m_active_window_title_color;
    Color m_inactive_window_border_color;
    Color m_inactive_window_border_color2;
    Color m_inactive_window_title_color;
    Color m_dragging_window_border_color;
    Color m_dragging_window_border_color2;
    Color m_dragging_window_title_color;
    Color m_highlight_window_border_color;
    Color m_highlight_window_border_color2;
    Color m_highlight_window_title_color;

    HashMap<int, OwnPtr<WSWindow>> m_windows_by_id;
    HashTable<WSWindow*> m_windows;
    InlineLinkedList<WSWindow> m_windows_in_order;

    WeakPtr<WSWindow> m_active_window;
    WeakPtr<WSWindow> m_hovered_window;
    WeakPtr<WSWindow> m_highlight_window;

    WeakPtr<WSWindow> m_drag_window;
    Point m_drag_origin;
    Point m_drag_window_origin;

    WeakPtr<WSWindow> m_resize_window;
    Rect m_resize_window_original_rect;
    Point m_resize_origin;
    ResizeDirection m_resize_direction { ResizeDirection::None };

    Rect m_last_cursor_rect;

    unsigned m_compose_count { 0 };
    unsigned m_flush_count { 0 };

    RetainPtr<GraphicsBitmap> m_front_bitmap;
    RetainPtr<GraphicsBitmap> m_back_bitmap;

    DisjointRectSet m_dirty_rects;

    bool m_pending_compose_event { false };

    RetainPtr<WSCursor> m_arrow_cursor;
    RetainPtr<WSCursor> m_resize_horizontally_cursor;
    RetainPtr<WSCursor> m_resize_vertically_cursor;
    RetainPtr<WSCursor> m_resize_diagonally_tlbr_cursor;
    RetainPtr<WSCursor> m_resize_diagonally_bltr_cursor;
    RetainPtr<WSCursor> m_i_beam_cursor;
    RetainPtr<WSCursor> m_disallowed_cursor;
    RetainPtr<WSCursor> m_move_cursor;

    OwnPtr<Painter> m_back_painter;
    OwnPtr<Painter> m_front_painter;

    String m_wallpaper_path;
    RetainPtr<GraphicsBitmap> m_wallpaper;

    bool m_flash_flush { false };
    bool m_buffers_are_flipped { false };

    byte m_keyboard_modifiers { 0 };

    OwnPtr<WSMenu> m_system_menu;
    Color m_menu_selection_color;
    WeakPtr<WSMenuBar> m_current_menubar;
    WeakPtr<WSMenu> m_current_menu;

    WSWindowSwitcher m_switcher;

    CircularQueue<float, 30> m_cpu_history;

    String m_username;
};

template<typename Callback>
IterationDecision WSWindowManager::for_each_visible_window_of_type_from_back_to_front(WSWindowType type, Callback callback)
{
    bool do_highlight_window_at_end = false;
    for (auto* window = m_windows_in_order.head(); window; window = window->next()) {
        if (!window->is_visible())
            continue;
        if (window->type() != type)
            continue;
        if (m_highlight_window.ptr() == window) {
            do_highlight_window_at_end = true;
            continue;
        }
        if (callback(*window) == IterationDecision::Abort)
            return IterationDecision::Abort;
    }
    if (do_highlight_window_at_end) {
        if (callback(*m_highlight_window) == IterationDecision::Abort)
            return IterationDecision::Abort;
    }
    return IterationDecision::Continue;
}

template<typename Callback>
IterationDecision WSWindowManager::for_each_visible_window_from_back_to_front(Callback callback)
{
    if (for_each_visible_window_of_type_from_back_to_front(WSWindowType::Normal, callback) == IterationDecision::Abort)
        return IterationDecision::Abort;
    if (for_each_visible_window_of_type_from_back_to_front(WSWindowType::Menu, callback) == IterationDecision::Abort)
        return IterationDecision::Abort;
    return for_each_visible_window_of_type_from_back_to_front(WSWindowType::WindowSwitcher, callback);
}

template<typename Callback>
IterationDecision WSWindowManager::for_each_visible_window_of_type_from_front_to_back(WSWindowType type, Callback callback)
{
    if (m_highlight_window && m_highlight_window->type() == type && m_highlight_window->is_visible()) {
        if (callback(*m_highlight_window) == IterationDecision::Abort)
            return IterationDecision::Abort;
    }

    for (auto* window = m_windows_in_order.tail(); window; window = window->prev()) {
        if (!window->is_visible())
            continue;
        if (window->type() != type)
            continue;
        if (window == m_highlight_window.ptr())
            continue;
        if (callback(*window) == IterationDecision::Abort)
            return IterationDecision::Abort;
    }
    return IterationDecision::Continue;
}

template<typename Callback>
IterationDecision WSWindowManager::for_each_visible_window_from_front_to_back(Callback callback)
{
    if (for_each_visible_window_of_type_from_front_to_back(WSWindowType::Menu, callback) == IterationDecision::Abort)
        return IterationDecision::Abort;
    if (for_each_visible_window_of_type_from_front_to_back(WSWindowType::Normal, callback) == IterationDecision::Abort)
        return IterationDecision::Abort;
    return for_each_visible_window_of_type_from_front_to_back(WSWindowType::WindowSwitcher, callback);
}
