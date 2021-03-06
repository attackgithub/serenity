#include <WindowServer/WSClientConnection.h>
#include <WindowServer/WSMessageLoop.h>
#include <WindowServer/WSMenuBar.h>
#include <WindowServer/WSMenu.h>
#include <WindowServer/WSMenuItem.h>
#include <WindowServer/WSWindow.h>
#include <WindowServer/WSWindowManager.h>
#include <WindowServer/WSAPITypes.h>
#include <WindowServer/WSClipboard.h>
#include <SharedBuffer.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

HashMap<int, WSClientConnection*>* s_connections;

void WSClientConnection::for_each_client(Function<void(WSClientConnection&)> callback)
{
    if (!s_connections)
        return;
    for (auto& it : *s_connections) {
        callback(*it.value);
    }
}

WSClientConnection* WSClientConnection::from_client_id(int client_id)
{
    if (!s_connections)
        return nullptr;
    auto it = s_connections->find(client_id);
    if (it == s_connections->end())
        return nullptr;
    return (*it).value;
}

WSClientConnection::WSClientConnection(int fd)
    : m_fd(fd)
{
    static int s_next_client_id = 0;
    m_client_id = ++s_next_client_id;

    if (!s_connections)
        s_connections = new HashMap<int, WSClientConnection*>;
    s_connections->set(m_client_id, this);

    WSAPI_ServerMessage message;
    message.type = WSAPI_ServerMessage::Type::Greeting;
    message.greeting.server_pid = getpid();
    post_message(message);
}

WSClientConnection::~WSClientConnection()
{
    s_connections->remove(m_client_id);
    int rc = close(m_fd);
    ASSERT(rc == 0);
}

void WSClientConnection::post_error(const String& error_message)
{
    dbgprintf("WSClientConnection::post_error: client_id=%d: %s\n", m_client_id, error_message.characters());
    WSAPI_ServerMessage message;
    message.type = WSAPI_ServerMessage::Type::Error;
    ASSERT(error_message.length() < (ssize_t)sizeof(message.text));
    strcpy(message.text, error_message.characters());
    message.text_length = error_message.length();
    post_message(message);
}

void WSClientConnection::post_message(const WSAPI_ServerMessage& message)
{
    int nwritten = write(m_fd, &message, sizeof(message));
    if (nwritten < 0) {
        if (errno == EPIPE) {
            dbgprintf("WSClientConnection::post_message: Disconnected from peer.\n");
            return;
        }
        perror("WSClientConnection::post_message write");
        ASSERT_NOT_REACHED();
    }

    ASSERT(nwritten == sizeof(message));
}

void WSClientConnection::on_message(WSMessage& message)
{
    if (message.is_client_request()) {
        on_request(static_cast<WSAPIClientRequest&>(message));
        return;
    }

    if (message.type() == WSMessage::WM_ClientDisconnected) {
        int client_id = static_cast<WSClientDisconnectedNotification&>(message).client_id();
        dbgprintf("WSClientConnection: Client disconnected: %d\n", client_id);
        delete this;
        return;
    }
}

void WSClientConnection::handle_request(WSAPICreateMenubarRequest&)
{
    int menubar_id = m_next_menubar_id++;
    auto menubar = make<WSMenuBar>(*this, menubar_id);
    m_menubars.set(menubar_id, move(menubar));
    WSAPI_ServerMessage response;
    response.type = WSAPI_ServerMessage::Type::DidCreateMenubar;
    response.menu.menubar_id = menubar_id;
    post_message(response);
}

void WSClientConnection::handle_request(WSAPIDestroyMenubarRequest& request)
{
    int menubar_id = request.menubar_id();
    auto it = m_menubars.find(menubar_id);
    if (it == m_menubars.end()) {
        post_error("Bad menubar ID");
        return;
    }
    auto& menubar = *(*it).value;
    WSWindowManager::the().close_menubar(menubar);
    m_menubars.remove(it);
    WSAPI_ServerMessage response;
    response.type = WSAPI_ServerMessage::Type::DidDestroyMenubar;
    response.menu.menubar_id = menubar_id;
    post_message(response);
}

void WSClientConnection::handle_request(WSAPICreateMenuRequest& request)
{
    int menu_id = m_next_menu_id++;
    auto menu = make<WSMenu>(this, menu_id, request.text());
    m_menus.set(menu_id, move(menu));
    WSAPI_ServerMessage response;
    response.type = WSAPI_ServerMessage::Type::DidCreateMenu;
    response.menu.menu_id = menu_id;
    post_message(response);
}

void WSClientConnection::handle_request(WSAPIDestroyMenuRequest& request)
{
    int menu_id = static_cast<WSAPIDestroyMenuRequest&>(request).menu_id();
    auto it = m_menus.find(menu_id);
    if (it == m_menus.end()) {
        post_error("Bad menu ID");
        return;
    }
    auto& menu = *(*it).value;
    WSWindowManager::the().close_menu(menu);
    m_menus.remove(it);
    WSAPI_ServerMessage response;
    response.type = WSAPI_ServerMessage::Type::DidDestroyMenu;
    response.menu.menu_id = menu_id;
    post_message(response);
}

void WSClientConnection::handle_request(WSAPISetApplicationMenubarRequest& request)
{
    int menubar_id = request.menubar_id();
    auto it = m_menubars.find(menubar_id);
    if (it == m_menubars.end()) {
        post_error("Bad menubar ID");
        return;
    }
    auto& menubar = *(*it).value;
    m_app_menubar = menubar.make_weak_ptr();
    WSWindowManager::the().notify_client_changed_app_menubar(*this);
    WSAPI_ServerMessage response;
    response.type = WSAPI_ServerMessage::Type::DidSetApplicationMenubar;
    response.menu.menubar_id = menubar_id;
    post_message(response);
}

void WSClientConnection::handle_request(WSAPIAddMenuToMenubarRequest& request)
{
    int menubar_id = request.menubar_id();
    int menu_id = request.menu_id();
    auto it = m_menubars.find(menubar_id);
    auto jt = m_menus.find(menu_id);
    if (it == m_menubars.end()) {
        post_error("Bad menubar ID");
        return;
    }
    if (jt == m_menus.end()) {
        post_error("Bad menu ID");
        return;
    }
    auto& menubar = *(*it).value;
    auto& menu = *(*jt).value;
    menubar.add_menu(&menu);
    WSAPI_ServerMessage response;
    response.type = WSAPI_ServerMessage::Type::DidAddMenuToMenubar;
    response.menu.menubar_id = menubar_id;
    response.menu.menu_id = menu_id;
    post_message(response);
}

void WSClientConnection::handle_request(WSAPIAddMenuItemRequest& request)
{
    int menu_id = request.menu_id();
    unsigned identifier = request.identifier();
    auto it = m_menus.find(menu_id);
    if (it == m_menus.end()) {
        post_error("Bad menu ID");
        return;
    }
    auto& menu = *(*it).value;
    menu.add_item(make<WSMenuItem>(identifier, request.text(), request.shortcut_text()));
    WSAPI_ServerMessage response;
    response.type = WSAPI_ServerMessage::Type::DidAddMenuItem;
    response.menu.menu_id = menu_id;
    response.menu.identifier = identifier;
    post_message(response);
}

void WSClientConnection::handle_request(WSAPIAddMenuSeparatorRequest& request)
{
    int menu_id = request.menu_id();
    auto it = m_menus.find(menu_id);
    if (it == m_menus.end()) {
        post_error("Bad menu ID");
        return;
    }
    auto& menu = *(*it).value;
    menu.add_item(make<WSMenuItem>(WSMenuItem::Separator));
    WSAPI_ServerMessage response;
    response.type = WSAPI_ServerMessage::Type::DidAddMenuSeparator;
    response.menu.menu_id = menu_id;
    post_message(response);
}

void WSClientConnection::handle_request(WSAPISetWindowOpacityRequest& request)
{
    int window_id = request.window_id();
    auto it = m_windows.find(window_id);
    if (it == m_windows.end()) {
        post_error("Bad window ID");
        return;
    }
    auto& window = *(*it).value;
    window.set_opacity(request.opacity());
}

void WSClientConnection::handle_request(WSAPISetWallpaperRequest& request)
{
    bool success = WSWindowManager::the().set_wallpaper(request.wallpaper());
    WSAPI_ServerMessage response;
    response.type = WSAPI_ServerMessage::Type::DidSetWallpaper;
    response.value = success;
    post_message(response);
}

void WSClientConnection::handle_request(WSAPIGetWallpaperRequest& request)
{
    auto path = WSWindowManager::the().wallpaper_path();
    WSAPI_ServerMessage response;
    response.type = WSAPI_ServerMessage::Type::DidGetWallpaper;
    ASSERT(path.length() < (int)sizeof(response.text));
    strncpy(response.text, path.characters(), path.length());
    response.text_length = path.length();
    post_message(response);
}

void WSClientConnection::handle_request(WSAPISetWindowTitleRequest& request)
{
    int window_id = request.window_id();
    auto it = m_windows.find(window_id);
    if (it == m_windows.end()) {
        post_error("Bad window ID");
        return;
    }
    auto& window = *(*it).value;
    window.set_title(request.title());
}

void WSClientConnection::handle_request(WSAPIGetWindowTitleRequest& request)
{
    int window_id = request.window_id();
    auto it = m_windows.find(window_id);
    if (it == m_windows.end()) {
        post_error("Bad window ID");
        return;
    }
    auto& window = *(*it).value;
    WSAPI_ServerMessage response;
    response.type = WSAPI_ServerMessage::Type::DidGetWindowTitle;
    response.window_id = window.window_id();
    ASSERT(window.title().length() < (ssize_t)sizeof(response.text));
    strcpy(response.text, window.title().characters());
    response.text_length = window.title().length();
    post_message(response);
}

void WSClientConnection::handle_request(WSAPISetWindowRectRequest& request)
{
    int window_id = request.window_id();
    auto it = m_windows.find(window_id);
    if (it == m_windows.end()) {
        post_error("Bad window ID");
        return;
    }
    auto& window = *(*it).value;
    window.set_rect(request.rect());
}

void WSClientConnection::handle_request(WSAPIGetWindowRectRequest& request)
{
    int window_id = request.window_id();
    auto it = m_windows.find(window_id);
    if (it == m_windows.end()) {
        post_error("Bad window ID");
        return;
    }
    auto& window = *(*it).value;
    WSAPI_ServerMessage response;
    response.type = WSAPI_ServerMessage::Type::DidGetWindowRect;
    response.window_id = window.window_id();
    response.window.rect = window.rect();
    post_message(response);
}

void WSClientConnection::handle_request(WSAPISetClipboardContentsRequest& request)
{
    auto shared_buffer = SharedBuffer::create_from_shared_buffer_id(request.shared_buffer_id());
    if (!shared_buffer) {
        post_error("Bad shared buffer ID");
        return;
    }
    WSClipboard::the().set_data(*shared_buffer, request.size());
    WSAPI_ServerMessage response;
    response.type = WSAPI_ServerMessage::Type::DidSetClipboardContents;
    response.clipboard.shared_buffer_id = shared_buffer->shared_buffer_id();
    post_message(response);
}

void WSClientConnection::handle_request(WSAPIGetClipboardContentsRequest&)
{
    WSAPI_ServerMessage response;
    response.type = WSAPI_ServerMessage::Type::DidGetClipboardContents;
    response.clipboard.shared_buffer_id = -1;
    response.clipboard.contents_size = 0;
    if (WSClipboard::the().size()) {
        // FIXME: Optimize case where an app is copy/pasting within itself.
        //        We can just reuse the SharedBuffer then, since it will have the same peer PID.
        //        It would be even nicer if a SharedBuffer could have an arbitrary number of clients..
        RetainPtr<SharedBuffer> shared_buffer = SharedBuffer::create(m_pid, WSClipboard::the().size());
        ASSERT(shared_buffer);
        memcpy(shared_buffer->data(), WSClipboard::the().data(), WSClipboard::the().size());
        shared_buffer->seal();
        response.clipboard.shared_buffer_id = shared_buffer->shared_buffer_id();
        response.clipboard.contents_size = WSClipboard::the().size();

        // FIXME: This is a workaround for the fact that SharedBuffers will go away if neither side is retaining them.
        //        After we respond to GetClipboardContents, we have to wait for the client to retain the buffer on his side.
        m_last_sent_clipboard_content = move(shared_buffer);
    }
    post_message(response);
}

void WSClientConnection::handle_request(WSAPICreateWindowRequest& request)
{
    int window_id = m_next_window_id++;
    auto window = make<WSWindow>(*this, window_id, request.is_modal());
    window->set_has_alpha_channel(request.has_alpha_channel());
    window->set_resizable(request.is_resizable());
    window->set_title(request.title());
    window->set_rect(request.rect());
    window->set_opacity(request.opacity());
    window->set_size_increment(request.size_increment());
    window->set_base_size(request.base_size());
    window->invalidate();
    m_windows.set(window_id, move(window));
    WSAPI_ServerMessage response;
    response.type = WSAPI_ServerMessage::Type::DidCreateWindow;
    response.window_id = window_id;
    post_message(response);
}

void WSClientConnection::handle_request(WSAPIDestroyWindowRequest& request)
{
    int window_id = request.window_id();
    auto it = m_windows.find(window_id);
    if (it == m_windows.end()) {
        post_error("Bad window ID");
        return;
    }
    auto& window = *(*it).value;
    WSWindowManager::the().invalidate(window);
    m_windows.remove(it);
}

void WSClientConnection::handle_request(WSAPIInvalidateRectRequest& request)
{
    int window_id = request.window_id();
    auto it = m_windows.find(window_id);
    if (it == m_windows.end()) {
        post_error("Bad window ID");
        return;
    }
    auto& window = *(*it).value;
    WSAPI_ServerMessage response;
    response.type = WSAPI_ServerMessage::Type::Paint;
    response.window_id = window_id;
    response.paint.rect = request.rect();
    response.paint.window_size = window.size();
    post_message(response);
}

void WSClientConnection::handle_request(WSAPIDidFinishPaintingNotification& request)
{
    int window_id = request.window_id();
    auto it = m_windows.find(window_id);
    if (it == m_windows.end()) {
        post_error("Bad window ID");
        return;
    }
    auto& window = *(*it).value;

    if (!window.has_painted_since_last_resize()) {
        if (window.last_lazy_resize_rect().size() == request.rect().size()) {
            window.set_has_painted_since_last_resize(true);
            WSMessageLoop::the().post_message(window, make<WSResizeEvent>(window.last_lazy_resize_rect(), window.rect()));
        }
    }
    WSWindowManager::the().invalidate(window, request.rect());
}

void WSClientConnection::handle_request(WSAPIGetWindowBackingStoreRequest& request)
{
    int window_id = request.window_id();
    auto it = m_windows.find(window_id);
    if (it == m_windows.end()) {
        post_error("Bad window ID");
        return;
    }
    auto& window = *(*it).value;
    auto* backing_store = window.backing_store();

    WSAPI_ServerMessage response;
    response.type = WSAPI_ServerMessage::Type::DidGetWindowBackingStore;
    response.window_id = window_id;
    response.backing.bpp = sizeof(RGBA32);
    response.backing.pitch = backing_store->pitch();
    response.backing.size = backing_store->size();
    response.backing.has_alpha_channel = backing_store->has_alpha_channel();
    response.backing.shared_buffer_id = backing_store->shared_buffer_id();
    post_message(response);
}

void WSClientConnection::handle_request(WSAPISetWindowBackingStoreRequest& request)
{
    int window_id = request.window_id();
    auto it = m_windows.find(window_id);
    if (it == m_windows.end()) {
        post_error("Bad window ID");
        return;
    }
    auto& window = *(*it).value;
    if (window.last_backing_store() && window.last_backing_store()->shared_buffer_id() == request.shared_buffer_id()) {
        window.swap_backing_stores();
    } else {
        auto shared_buffer = SharedBuffer::create_from_shared_buffer_id(request.shared_buffer_id());
        if (!shared_buffer)
            return;
        auto backing_store = GraphicsBitmap::create_with_shared_buffer(
            request.has_alpha_channel() ? GraphicsBitmap::Format::RGBA32 : GraphicsBitmap::Format::RGB32,
            *shared_buffer,
            request.size());
        window.set_backing_store(move(backing_store));
    }

    if (request.flush_immediately())
        window.invalidate();

    WSAPI_ServerMessage response;
    response.type = WSAPI_ServerMessage::Type::DidSetWindowBackingStore;
    response.window_id = window_id;
    response.backing.shared_buffer_id = request.shared_buffer_id();
    post_message(response);
}

void WSClientConnection::handle_request(WSAPISetGlobalCursorTrackingRequest& request)
{
    int window_id = request.window_id();
    auto it = m_windows.find(window_id);
    if (it == m_windows.end()) {
        post_error("Bad window ID");
        return;
    }
    auto& window = *(*it).value;
    window.set_global_cursor_tracking_enabled(request.value());
}

void WSClientConnection::handle_request(WSAPISetWindowOverrideCursorRequest& request)
{
    int window_id = request.window_id();
    auto it = m_windows.find(window_id);
    if (it == m_windows.end()) {
        post_error("Bad window ID");
        return;
    }
    auto& window = *(*it).value;
    window.set_override_cursor(WSCursor::create(request.cursor()));
}

void WSClientConnection::on_request(WSAPIClientRequest& request)
{
    switch (request.type()) {
    case WSMessage::APICreateMenubarRequest:
        return handle_request(static_cast<WSAPICreateMenubarRequest&>(request));
    case WSMessage::APIDestroyMenubarRequest:
        return handle_request(static_cast<WSAPIDestroyMenubarRequest&>(request));
    case WSMessage::APICreateMenuRequest:
        return handle_request(static_cast<WSAPICreateMenuRequest&>(request));
    case WSMessage::APIDestroyMenuRequest:
        return handle_request(static_cast<WSAPIDestroyMenuRequest&>(request));
    case WSMessage::APISetApplicationMenubarRequest:
        return handle_request(static_cast<WSAPISetApplicationMenubarRequest&>(request));
    case WSMessage::APIAddMenuToMenubarRequest:
        return handle_request(static_cast<WSAPIAddMenuToMenubarRequest&>(request));
    case WSMessage::APIAddMenuItemRequest:
        return handle_request(static_cast<WSAPIAddMenuItemRequest&>(request));
    case WSMessage::APIAddMenuSeparatorRequest:
        return handle_request(static_cast<WSAPIAddMenuSeparatorRequest&>(request));
    case WSMessage::APISetWindowTitleRequest:
        return handle_request(static_cast<WSAPISetWindowTitleRequest&>(request));
    case WSMessage::APIGetWindowTitleRequest:
        return handle_request(static_cast<WSAPIGetWindowTitleRequest&>(request));
    case WSMessage::APISetWindowRectRequest:
        return handle_request(static_cast<WSAPISetWindowRectRequest&>(request));
    case WSMessage::APIGetWindowRectRequest:
        return handle_request(static_cast<WSAPIGetWindowRectRequest&>(request));
    case WSMessage::APISetClipboardContentsRequest:
        return handle_request(static_cast<WSAPISetClipboardContentsRequest&>(request));
    case WSMessage::APIGetClipboardContentsRequest:
        return handle_request(static_cast<WSAPIGetClipboardContentsRequest&>(request));
    case WSMessage::APICreateWindowRequest:
        return handle_request(static_cast<WSAPICreateWindowRequest&>(request));
    case WSMessage::APIDestroyWindowRequest:
        return handle_request(static_cast<WSAPIDestroyWindowRequest&>(request));
    case WSMessage::APIInvalidateRectRequest:
        return handle_request(static_cast<WSAPIInvalidateRectRequest&>(request));
    case WSMessage::APIDidFinishPaintingNotification:
        return handle_request(static_cast<WSAPIDidFinishPaintingNotification&>(request));
    case WSMessage::APIGetWindowBackingStoreRequest:
        return handle_request(static_cast<WSAPIGetWindowBackingStoreRequest&>(request));
    case WSMessage::APISetGlobalCursorTrackingRequest:
        return handle_request(static_cast<WSAPISetGlobalCursorTrackingRequest&>(request));
    case WSMessage::APISetWindowOpacityRequest:
        return handle_request(static_cast<WSAPISetWindowOpacityRequest&>(request));
    case WSMessage::APISetWindowBackingStoreRequest:
        return handle_request(static_cast<WSAPISetWindowBackingStoreRequest&>(request));
    case WSMessage::APISetWallpaperRequest:
        return handle_request(static_cast<WSAPISetWallpaperRequest&>(request));
    case WSMessage::APIGetWallpaperRequest:
        return handle_request(static_cast<WSAPIGetWallpaperRequest&>(request));
    case WSMessage::APISetWindowOverrideCursorRequest:
        return handle_request(static_cast<WSAPISetWindowOverrideCursorRequest&>(request));
    default:
        break;
    }
}

bool WSClientConnection::is_showing_modal_window() const
{
    for (auto& it : m_windows) {
        auto& window = *it.value;
        if (window.is_visible() && window.is_modal())
            return true;
    }
    return false;
}
