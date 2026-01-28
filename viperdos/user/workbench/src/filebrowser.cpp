//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/workbench/src/filebrowser.cpp
// Purpose: File browser window for ViperDOS Workbench.
// Key invariants: Each browser maintains independent path state.
// Ownership/Lifetime: Created by Desktop, destroyed when window closes.
// Links: user/workbench/include/filebrowser.hpp, user/workbench/src/desktop.cpp
//
//===----------------------------------------------------------------------===//

/**
 * @file filebrowser.cpp
 * @brief File browser window implementation for ViperDOS Workbench.
 *
 * @details
 * The FileBrowser class provides a graphical file browser window that displays
 * directory contents with icons. Features include:
 * - Directory navigation via double-click
 * - File/folder icon rendering with appropriate icons per type
 * - Selection highlighting and multi-select support
 * - Parent directory navigation ("..") support
 *
 * Each FileBrowser window maintains its own path state and can display
 * independent views of the filesystem.
 */

#include "../include/filebrowser.hpp"
#include "../include/colors.hpp"
#include "../include/desktop.hpp"
#include "../include/icons.hpp"
#include "../include/utils.hpp"
#include <dirent.h>
#include <gui.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

namespace workbench {

// Global clipboard instance
FileClipboard g_clipboard = {"", ClipboardOp::None, false};

FileBrowser::FileBrowser(Desktop *desktop, const char *initialPath) : m_desktop(desktop) {
    strncpy(m_currentPath, initialPath, MAX_PATH_LEN - 1);
    m_currentPath[MAX_PATH_LEN - 1] = '\0';
}

FileBrowser::~FileBrowser() {
    if (m_window) {
        gui_destroy_window(m_window);
        m_window = nullptr;
    }
}

bool FileBrowser::init() {
    // Create the browser window
    char title[MAX_PATH_LEN + 16];
    snprintf(title, sizeof(title), "Files: %s", m_currentPath);

    m_window = gui_create_window(title, m_width, m_height);
    if (!m_window) {
        return false;
    }

    // Load directory contents
    loadDirectory();
    redraw();

    return true;
}

void FileBrowser::loadDirectory() {
    m_fileCount = 0;
    m_selectedFile = -1;
    m_scrollOffset = 0; // Reset scroll when changing directories

    DIR *dir = opendir(m_currentPath);
    if (!dir) {
        return;
    }

    // First entry: parent directory (if not at root)
    if (strcmp(m_currentPath, "/") != 0) {
        strncpy(m_files[m_fileCount].name, "..", MAX_FILENAME_LEN - 1);
        m_files[m_fileCount].type = FileType::Directory;
        m_files[m_fileCount].size = 0;
        m_files[m_fileCount].selected = false;
        m_fileCount++;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr && m_fileCount < MAX_FILES_PER_DIR) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        strncpy(m_files[m_fileCount].name, entry->d_name, MAX_FILENAME_LEN - 1);
        m_files[m_fileCount].name[MAX_FILENAME_LEN - 1] = '\0';

        // Determine file type
        bool isDir = (entry->d_type == DT_DIR);
        m_files[m_fileCount].type = determineFileType(entry->d_name, isDir);

        // Get actual file size via stat()
        char fullPath[MAX_PATH_LEN];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", m_currentPath, entry->d_name);
        struct stat st;
        if (stat(fullPath, &st) == 0) {
            m_files[m_fileCount].size = st.st_size;
        } else {
            m_files[m_fileCount].size = 0;
        }
        m_files[m_fileCount].selected = false;

        m_fileCount++;
    }

    closedir(dir);

    // Update scrollbar based on content
    updateScrollbar();
}

void FileBrowser::updateScrollbar() {
    if (!m_window)
        return;

    int viewportHeight = m_height - FB_TOOLBAR_HEIGHT - FB_STATUSBAR_HEIGHT;
    int contentHeight = calculateContentHeight();

    // Enable scrollbar if content exceeds viewport
    if (contentHeight > viewportHeight) {
        gui_set_vscrollbar(m_window, contentHeight, viewportHeight, m_scrollOffset);
    } else {
        // Disable scrollbar (set content_height to 0)
        gui_set_vscrollbar(m_window, 0, viewportHeight, 0);
        m_scrollOffset = 0;
    }
}

int FileBrowser::calculateContentHeight() {
    if (m_fileCount == 0)
        return 0;

    // Calculate how many icons fit per row
    int iconsPerRow = (m_width - FB_PADDING) / FB_ICON_GRID_X;
    if (iconsPerRow < 1)
        iconsPerRow = 1;

    // Calculate number of rows needed
    int numRows = (m_fileCount + iconsPerRow - 1) / iconsPerRow;

    // Total content height
    return numRows * FB_ICON_GRID_Y + FB_PADDING * 2;
}

FileType FileBrowser::determineFileType(const char *name, bool isDir) {
    if (isDir) {
        return FileType::Directory;
    }

    // Check extension
    const char *dot = strrchr(name, '.');
    if (!dot) {
        return FileType::Unknown;
    }

    if (strcmp(dot, ".sys") == 0 || strcmp(dot, ".prg") == 0) {
        return FileType::Executable;
    }
    if (strcmp(dot, ".txt") == 0 || strcmp(dot, ".md") == 0 || strcmp(dot, ".c") == 0 ||
        strcmp(dot, ".h") == 0 || strcmp(dot, ".cpp") == 0 || strcmp(dot, ".hpp") == 0) {
        return FileType::Text;
    }
    if (strcmp(dot, ".bmp") == 0) {
        return FileType::Image;
    }

    return FileType::Unknown;
}

const uint32_t *FileBrowser::getIconForType(FileType type) {
    switch (type) {
        case FileType::Directory:
            return icons::folder_24;
        case FileType::Executable:
            return icons::file_exe_24;
        case FileType::Text:
            return icons::file_text_24;
        default:
            return icons::file_24;
    }
}

void FileBrowser::redraw() {
    if (!m_window)
        return;

    // Clear background using theme
    gui_fill_rect(m_window, 0, 0, m_width, m_height, themeWindowBg());

    drawToolbar();
    drawFileList();
    drawStatusBar();

    // Draw rename editor on top of file list if active
    drawRenameEditor();

    // Draw context menu on top if visible
    drawContextMenu();

    gui_present(m_window);
}

void FileBrowser::drawToolbar() {
    // Toolbar background using theme
    gui_fill_rect(m_window, 0, 0, m_width, FB_TOOLBAR_HEIGHT, themeMenuBg());

    // Parent button
    gui_fill_rect(m_window, 4, 2, 20, 20, themeBorderLight());
    gui_draw_rect(m_window, 4, 2, 20, 20, themeText());
    gui_draw_text(m_window, 9, 6, "^", themeText());

    // Path display
    gui_draw_text(m_window, 30, 6, m_currentPath, themeText());

    // Bottom border
    gui_draw_hline(m_window, 0, m_width - 1, FB_TOOLBAR_HEIGHT - 1, themeBorderDark());
}

void FileBrowser::drawFileList() {
    int listTop = FB_TOOLBAR_HEIGHT;
    int listHeight = m_height - FB_TOOLBAR_HEIGHT - FB_STATUSBAR_HEIGHT;

    // List background using theme desktop color
    gui_fill_rect(m_window, 0, listTop, m_width, listHeight, themeDesktop());

    // Draw files in grid
    int x = FB_PADDING;
    int y = listTop + FB_PADDING - m_scrollOffset;

    for (int i = 0; i < m_fileCount; i++) {
        if (y + FB_ICON_GRID_Y > listTop && y < listTop + listHeight) {
            // Selection highlight using theme
            if (m_files[i].selected) {
                gui_fill_rect(m_window,
                              x - 2,
                              y - 2,
                              FB_ICON_GRID_X - 4,
                              FB_ICON_GRID_Y - 4,
                              themeHighlight());
            }

            // Draw icon
            drawFileIcon(x + (FB_ICON_GRID_X - ICON_SIZE) / 2, y, m_files[i].type);

            // Draw filename (truncate if too long)
            char displayName[16];
            strncpy(displayName, m_files[i].name, 15);
            displayName[15] = '\0';

            int textX = x + (FB_ICON_GRID_X - strlen(displayName) * 8) / 2;
            int textY = y + ICON_SIZE + 4;
            // Draw text with shadow for visibility
            gui_draw_text(m_window, textX + 1, textY + 1, displayName, themeIconShadow());
            gui_draw_text(m_window, textX, textY, displayName, themeIconText());
        }

        x += FB_ICON_GRID_X;
        if (x + FB_ICON_GRID_X > m_width) {
            x = FB_PADDING;
            y += FB_ICON_GRID_Y;
        }
    }
}

void FileBrowser::drawFileIcon(int x, int y, FileType type) {
    const uint32_t *pixels = getIconForType(type);
    uint32_t *fb = gui_get_pixels(m_window);
    uint32_t stride = gui_get_stride(m_window) / 4;

    for (int py = 0; py < ICON_SIZE; py++) {
        for (int px = 0; px < ICON_SIZE; px++) {
            uint32_t color = pixels[py * ICON_SIZE + px];
            if (color != 0) {
                int dx = x + px;
                int dy = y + py;
                if (dx >= 0 && dx < m_width && dy >= 0 && dy < m_height) {
                    fb[dy * stride + dx] = color;
                }
            }
        }
    }
}

void FileBrowser::drawStatusBar() {
    int y = m_height - FB_STATUSBAR_HEIGHT;

    // Status bar background using theme
    gui_fill_rect(m_window, 0, y, m_width, FB_STATUSBAR_HEIGHT, themeMenuBg());

    // Top border
    gui_draw_hline(m_window, 0, m_width - 1, y, themeBorderDark());

    // Show selected file info or item count
    char status[128];
    if (m_selectedFile >= 0 && m_selectedFile < m_fileCount) {
        FileEntry &file = m_files[m_selectedFile];
        if (file.type == FileType::Directory) {
            snprintf(status, sizeof(status), "'%s' - Directory", file.name);
        } else {
            // Show file size in human-readable format
            if (file.size < 1024) {
                snprintf(status,
                         sizeof(status),
                         "'%s' - %llu bytes",
                         file.name,
                         (unsigned long long)file.size);
            } else if (file.size < 1024 * 1024) {
                snprintf(status,
                         sizeof(status),
                         "'%s' - %llu KB",
                         file.name,
                         (unsigned long long)(file.size / 1024));
            } else {
                snprintf(status,
                         sizeof(status),
                         "'%s' - %llu MB",
                         file.name,
                         (unsigned long long)(file.size / (1024 * 1024)));
            }
        }
    } else {
        snprintf(status, sizeof(status), "%d items", m_fileCount);
    }
    gui_draw_text(m_window, 8, y + 4, status, themeText());

    // Show keyboard hints on the right
    gui_draw_text(m_window, m_width - 160, y + 4, "Del:Delete F5:Refresh", themeTextDisabled());
}

int FileBrowser::findFileAt(int x, int y) {
    int listTop = FB_TOOLBAR_HEIGHT;
    int listHeight = m_height - FB_TOOLBAR_HEIGHT - FB_STATUSBAR_HEIGHT;

    if (y < listTop || y >= listTop + listHeight) {
        return -1;
    }

    int gridX = FB_PADDING;
    int gridY = listTop + FB_PADDING - m_scrollOffset;

    for (int i = 0; i < m_fileCount; i++) {
        if (x >= gridX && x < gridX + FB_ICON_GRID_X - 4 && y >= gridY &&
            y < gridY + FB_ICON_GRID_Y - 4) {
            return i;
        }

        gridX += FB_ICON_GRID_X;
        if (gridX + FB_ICON_GRID_X > m_width) {
            gridX = FB_PADDING;
            gridY += FB_ICON_GRID_Y;
        }
    }

    return -1;
}

bool FileBrowser::handleEvent(const gui_event_t &event) {
    switch (event.type) {
        case GUI_EVENT_MOUSE:
            if (event.mouse.event_type == 1) { // Button down
                // Check if clicking on context menu first
                if (m_contextMenu.visible) {
                    if (event.mouse.button == 0) { // Left click
                        handleMenuClick(event.mouse.x, event.mouse.y);
                    } else {
                        hideContextMenu();
                    }
                    return true;
                }
                handleClick(event.mouse.x, event.mouse.y, event.mouse.button);
                return true;
            }
            break;

        case GUI_EVENT_KEY:
            if (event.key.pressed) {
                handleKeyPress(event.key.keycode);
                return true;
            }
            break;

        case GUI_EVENT_SCROLL:
            // Update scroll offset from scrollbar
            m_scrollOffset = event.scroll.position;
            redraw();
            return true;

        case GUI_EVENT_CLOSE:
            // Mark for deferred closing to avoid use-after-free
            // Desktop will clean up after handleEvent returns
            m_closing = true;
            return true;

        default:
            break;
    }

    return false;
}

void FileBrowser::handleClick(int x, int y, int button) {
    // Hide context menu if clicking elsewhere
    if (m_contextMenu.visible) {
        hideContextMenu();
    }

    // Cancel rename if clicking elsewhere
    if (m_renameEditor.active) {
        cancelRename();
    }

    // Right-click: show context menu
    if (button == 1) {
        int fileIdx = findFileAt(x, y);
        // Select the file if clicking on one
        if (fileIdx >= 0) {
            for (int i = 0; i < m_fileCount; i++) {
                m_files[i].selected = (i == fileIdx);
            }
            m_selectedFile = fileIdx;
        }
        showContextMenu(x, y, fileIdx);
        return;
    }

    // Left click only below
    if (button != 0)
        return;

    // Check toolbar clicks
    if (y < FB_TOOLBAR_HEIGHT) {
        // Parent button
        if (x >= 4 && x < 24 && y >= 2 && y < 22) {
            navigateUp();
        }
        return;
    }

    // Check file list clicks
    int fileIdx = findFileAt(x, y);

    // Double-click detection
    uint64_t now = get_uptime_ms();
    bool isDoubleClick = false;

    if (fileIdx >= 0 && fileIdx == m_lastClickFile) {
        if (now - m_lastClickTime < static_cast<uint64_t>(DOUBLE_CLICK_MS)) {
            isDoubleClick = true;
        }
    }

    m_lastClickFile = fileIdx;
    m_lastClickTime = now;

    if (isDoubleClick && fileIdx >= 0) {
        handleDoubleClick(fileIdx);
        m_lastClickFile = -1;
        m_lastClickTime = 0;
    } else if (fileIdx >= 0) {
        // Single click: select
        for (int i = 0; i < m_fileCount; i++) {
            m_files[i].selected = (i == fileIdx);
        }
        m_selectedFile = fileIdx;
        redraw();
    } else {
        // Click on empty area: deselect all
        for (int i = 0; i < m_fileCount; i++) {
            m_files[i].selected = false;
        }
        m_selectedFile = -1;
        redraw();
    }
}

void FileBrowser::handleDoubleClick(int fileIndex) {
    if (fileIndex < 0 || fileIndex >= m_fileCount)
        return;

    FileEntry &file = m_files[fileIndex];

    // Build full path for non-directory actions
    char fullPath[MAX_PATH_LEN];
    if (strcmp(m_currentPath, "/") == 0) {
        snprintf(fullPath, sizeof(fullPath), "/%s", file.name);
    } else {
        snprintf(fullPath, sizeof(fullPath), "%s/%s", m_currentPath, file.name);
    }

    switch (file.type) {
        case FileType::Directory:
            // Navigate into directory
            if (strcmp(file.name, "..") == 0) {
                navigateUp();
            } else {
                navigateTo(fullPath);
            }
            break;

        case FileType::Executable:
            // Launch executable
            m_desktop->spawnProgram(fullPath);
            break;

        case FileType::Text:
            // Open in VEdit text editor
            m_desktop->spawnProgram("/c/vedit.prg", fullPath);
            break;

        case FileType::Image:
            // Open in image viewer
            m_desktop->spawnProgram("/c/viewer.prg", fullPath);
            break;

        case FileType::Unknown:
        default:
            // Unknown file type - just log it
            debug_serial("[filebrowser] Unknown file type: ");
            debug_serial(fullPath);
            debug_serial("\n");
            break;
    }
}

void FileBrowser::navigateTo(const char *path) {
    strncpy(m_currentPath, path, MAX_PATH_LEN - 1);
    m_currentPath[MAX_PATH_LEN - 1] = '\0';

    m_scrollOffset = 0;
    loadDirectory();

    // Update window title
    char title[MAX_PATH_LEN + 16];
    snprintf(title, sizeof(title), "Files: %s", m_currentPath);
    gui_set_title(m_window, title);

    redraw();
}

void FileBrowser::navigateUp() {
    if (strcmp(m_currentPath, "/") == 0) {
        return; // Already at root
    }

    // Find last slash
    char *lastSlash = strrchr(m_currentPath, '/');
    if (lastSlash == m_currentPath) {
        // Parent is root
        navigateTo("/");
    } else if (lastSlash) {
        *lastSlash = '\0';
        navigateTo(m_currentPath);
    }
}

void FileBrowser::handleKeyPress(int keycode) {
    // If rename editor is active, route all keys there
    if (m_renameEditor.active) {
        // Note: shift state is not easily available here, we'll assume false for now
        // A more complete implementation would track modifier state
        handleRenameKey(keycode, false);
        return;
    }

    // Key codes (evdev keycodes from kernel/input/keycodes.hpp)
    constexpr int KEY_ENTER = 28;     // Enter key
    constexpr int KEY_DELETE = 111;   // Delete key
    constexpr int KEY_BACKSPACE = 14; // Backspace key
    constexpr int KEY_F5 = 63;        // F5 key
    constexpr int KEY_C = 46;         // 'C' key (with Ctrl modifier for copy)
    constexpr int KEY_V = 47;         // 'V' key (with Ctrl modifier for paste)
    constexpr int KEY_N = 49;         // 'N' key (with Ctrl modifier for new folder)
    constexpr int KEY_F2 = 60;        // F2 key (rename)

    switch (keycode) {
        case KEY_ENTER:
            // Open/launch selected file
            if (m_selectedFile >= 0 && m_selectedFile < m_fileCount) {
                handleDoubleClick(m_selectedFile);
            }
            break;

        case KEY_DELETE:
        case KEY_BACKSPACE:
            // Delete selected file
            if (m_selectedFile >= 0 && m_selectedFile < m_fileCount) {
                if (deleteFile(m_selectedFile)) {
                    refreshDirectory();
                }
            }
            break;

        case KEY_F2:
            // Rename selected file
            if (m_selectedFile >= 0 && m_selectedFile < m_fileCount) {
                startRename(m_selectedFile);
            }
            break;

        case KEY_F5:
            // Refresh directory
            refreshDirectory();
            break;

        case KEY_C:
            // Copy (Ctrl+C) - for now just copy without modifier check
            if (m_selectedFile >= 0 && m_selectedFile < m_fileCount) {
                copyFile(m_selectedFile);
            }
            break;

        case KEY_V:
            // Paste (Ctrl+V) - for now just paste without modifier check
            if (pasteFile()) {
                refreshDirectory();
            }
            break;

        case KEY_N:
            // New Folder (Ctrl+N)
            if (createNewFolder()) {
                refreshDirectory();
            }
            break;

        default:
            break;
    }
}

void FileBrowser::showContextMenu(int x, int y, int fileIndex) {
    m_contextMenuFile = fileIndex;
    m_contextMenu.x = x;
    m_contextMenu.y = y;
    m_contextMenu.itemCount = 0;
    m_contextMenu.hoveredItem = -1;
    m_contextMenu.visible = true;

    // Build menu based on context
    if (fileIndex >= 0 && fileIndex < m_fileCount) {
        FileEntry &file = m_files[fileIndex];

        // "Open" option
        m_contextMenu.items[m_contextMenu.itemCount++] = {"Open", MenuAction::Open, false, true};

        // Separator after Open
        m_contextMenu.items[m_contextMenu.itemCount - 1].separator = true;

        // Copy option (not for "..")
        if (strcmp(file.name, "..") != 0) {
            m_contextMenu.items[m_contextMenu.itemCount++] = {
                "Copy", MenuAction::Copy, false, true};
        }

        // "Rename" option (not for "..")
        if (strcmp(file.name, "..") != 0) {
            m_contextMenu.items[m_contextMenu.itemCount++] = {
                "Rename", MenuAction::Rename, false, true};
        }

        // "Delete" option (not for "..")
        if (strcmp(file.name, "..") != 0) {
            m_contextMenu.items[m_contextMenu.itemCount++] = {
                "Delete", MenuAction::Delete, false, true};
        }

        // Separator before properties
        if (m_contextMenu.itemCount > 0) {
            m_contextMenu.items[m_contextMenu.itemCount - 1].separator = true;
        }

        // "Properties" option
        m_contextMenu.items[m_contextMenu.itemCount++] = {
            "Properties", MenuAction::Properties, false, true};
    } else {
        // Clicked on empty area - show "New Folder" option
        m_contextMenu.items[m_contextMenu.itemCount++] = {
            "New Folder", MenuAction::NewFolder, false, true};

        // Paste option (enabled if clipboard has content)
        m_contextMenu.items[m_contextMenu.itemCount++] = {
            "Paste", MenuAction::Paste, false, g_clipboard.hasContent};
    }

    // Ensure menu stays within window bounds
    int menuHeight = m_contextMenu.itemCount * MENU_ITEM_HEIGHT + 4;
    if (m_contextMenu.x + MENU_WIDTH > m_width) {
        m_contextMenu.x = m_width - MENU_WIDTH - 4;
    }
    if (m_contextMenu.y + menuHeight > m_height) {
        m_contextMenu.y = m_height - menuHeight - 4;
    }

    redraw();
}

void FileBrowser::hideContextMenu() {
    if (m_contextMenu.visible) {
        m_contextMenu.visible = false;
        m_contextMenuFile = -1;
        redraw();
    }
}

void FileBrowser::drawContextMenu() {
    if (!m_contextMenu.visible)
        return;

    int menuHeight = m_contextMenu.itemCount * MENU_ITEM_HEIGHT + 4;
    int x = m_contextMenu.x;
    int y = m_contextMenu.y;

    // Draw menu background with 3D border using theme
    gui_fill_rect(m_window, x, y, MENU_WIDTH, menuHeight, themeMenuBg());

    // Top/left highlight
    gui_draw_hline(m_window, x, x + MENU_WIDTH - 1, y, themeBorderLight());
    gui_draw_vline(m_window, x, y, y + menuHeight - 1, themeBorderLight());

    // Bottom/right shadow
    gui_draw_hline(m_window, x, x + MENU_WIDTH - 1, y + menuHeight - 1, themeBorderDark());
    gui_draw_vline(m_window, x + MENU_WIDTH - 1, y, y + menuHeight - 1, themeBorderDark());

    // Draw items
    int itemY = y + 2;
    for (int i = 0; i < m_contextMenu.itemCount; i++) {
        MenuItem &item = m_contextMenu.items[i];

        // Hover highlight (if enabled) using theme
        if (i == m_contextMenu.hoveredItem && item.enabled) {
            gui_fill_rect(
                m_window, x + 2, itemY, MENU_WIDTH - 4, MENU_ITEM_HEIGHT, themeMenuHighlight());
            gui_draw_text(m_window, x + 8, itemY + 4, item.label, themeMenuHighlightText());
        } else {
            uint32_t textColor = item.enabled ? themeMenuText() : themeTextDisabled();
            gui_draw_text(m_window, x + 8, itemY + 4, item.label, textColor);
        }

        // Draw separator line after this item if specified
        if (item.separator) {
            int sepY = itemY + MENU_ITEM_HEIGHT - 1;
            gui_draw_hline(m_window, x + 4, x + MENU_WIDTH - 5, sepY, themeBorderDark());
        }

        itemY += MENU_ITEM_HEIGHT;
    }
}

void FileBrowser::handleMenuClick(int x, int y) {
    if (!m_contextMenu.visible)
        return;

    // Check if click is within menu bounds
    int menuHeight = m_contextMenu.itemCount * MENU_ITEM_HEIGHT + 4;
    if (x < m_contextMenu.x || x >= m_contextMenu.x + MENU_WIDTH || y < m_contextMenu.y ||
        y >= m_contextMenu.y + menuHeight) {
        hideContextMenu();
        return;
    }

    // Find which item was clicked
    int itemY = m_contextMenu.y + 2;
    for (int i = 0; i < m_contextMenu.itemCount; i++) {
        if (y >= itemY && y < itemY + MENU_ITEM_HEIGHT) {
            MenuItem &item = m_contextMenu.items[i];
            if (item.enabled) {
                hideContextMenu();
                executeMenuAction(item.action);
            }
            return;
        }
        itemY += MENU_ITEM_HEIGHT;
    }

    hideContextMenu();
}

void FileBrowser::executeMenuAction(MenuAction action) {
    switch (action) {
        case MenuAction::Open:
            if (m_contextMenuFile >= 0) {
                handleDoubleClick(m_contextMenuFile);
            }
            break;

        case MenuAction::Delete:
            if (m_contextMenuFile >= 0) {
                if (deleteFile(m_contextMenuFile)) {
                    refreshDirectory();
                }
            }
            break;

        case MenuAction::Rename:
            if (m_contextMenuFile >= 0) {
                startRename(m_contextMenuFile);
            }
            break;

        case MenuAction::NewFolder:
            if (createNewFolder()) {
                refreshDirectory();
            }
            break;

        case MenuAction::Properties:
            if (m_contextMenuFile >= 0) {
                showProperties(m_contextMenuFile);
            }
            break;

        case MenuAction::Copy:
            if (m_contextMenuFile >= 0) {
                copyFile(m_contextMenuFile);
            }
            break;

        case MenuAction::Paste:
            if (pasteFile()) {
                refreshDirectory();
            }
            break;

        default:
            break;
    }
}

bool FileBrowser::deleteFile(int fileIndex) {
    if (fileIndex < 0 || fileIndex >= m_fileCount) {
        return false;
    }

    FileEntry &file = m_files[fileIndex];

    // Don't delete ".." entry
    if (strcmp(file.name, "..") == 0) {
        return false;
    }

    // Build full path
    char fullPath[MAX_PATH_LEN];
    if (strcmp(m_currentPath, "/") == 0) {
        snprintf(fullPath, sizeof(fullPath), "/%s", file.name);
    } else {
        snprintf(fullPath, sizeof(fullPath), "%s/%s", m_currentPath, file.name);
    }

    debug_serial("[filebrowser] Deleting: ");
    debug_serial(fullPath);
    debug_serial("\n");

    // Use unlink syscall for files, rmdir for directories
    int result;
    if (file.type == FileType::Directory) {
        result = rmdir(fullPath);
    } else {
        result = unlink(fullPath);
    }

    if (result == 0) {
        debug_serial("[filebrowser] Delete successful\n");
        return true;
    } else {
        debug_serial("[filebrowser] Delete failed\n");
        return false;
    }
}

bool FileBrowser::renameFile(int fileIndex, const char *newName) {
    if (fileIndex < 0 || fileIndex >= m_fileCount || !newName) {
        return false;
    }

    FileEntry &file = m_files[fileIndex];

    // Don't rename ".." entry
    if (strcmp(file.name, "..") == 0) {
        return false;
    }

    // Build full paths
    char oldPath[MAX_PATH_LEN];
    char newPath[MAX_PATH_LEN];

    if (strcmp(m_currentPath, "/") == 0) {
        snprintf(oldPath, sizeof(oldPath), "/%s", file.name);
        snprintf(newPath, sizeof(newPath), "/%s", newName);
    } else {
        snprintf(oldPath, sizeof(oldPath), "%s/%s", m_currentPath, file.name);
        snprintf(newPath, sizeof(newPath), "%s/%s", m_currentPath, newName);
    }

    debug_serial("[filebrowser] Renaming: ");
    debug_serial(oldPath);
    debug_serial(" -> ");
    debug_serial(newPath);
    debug_serial("\n");

    int result = rename(oldPath, newPath);
    return result == 0;
}

void FileBrowser::refreshDirectory() {
    // Remember selection if possible
    char selectedName[MAX_FILENAME_LEN] = "";
    if (m_selectedFile >= 0 && m_selectedFile < m_fileCount) {
        strncpy(selectedName, m_files[m_selectedFile].name, MAX_FILENAME_LEN - 1);
    }

    // Reload directory
    loadDirectory();

    // Try to restore selection
    m_selectedFile = -1;
    if (selectedName[0] != '\0') {
        for (int i = 0; i < m_fileCount; i++) {
            if (strcmp(m_files[i].name, selectedName) == 0) {
                m_files[i].selected = true;
                m_selectedFile = i;
                break;
            }
        }
    }

    redraw();
}

void FileBrowser::copyFile(int fileIndex) {
    if (fileIndex < 0 || fileIndex >= m_fileCount) {
        return;
    }

    FileEntry &file = m_files[fileIndex];

    // Don't copy ".." entry
    if (strcmp(file.name, "..") == 0) {
        return;
    }

    // Build full path and store in clipboard
    if (strcmp(m_currentPath, "/") == 0) {
        snprintf(g_clipboard.path, MAX_PATH_LEN, "/%s", file.name);
    } else {
        snprintf(g_clipboard.path, MAX_PATH_LEN, "%s/%s", m_currentPath, file.name);
    }

    g_clipboard.operation = ClipboardOp::Copy;
    g_clipboard.hasContent = true;

    debug_serial("[filebrowser] Copied to clipboard: ");
    debug_serial(g_clipboard.path);
    debug_serial("\n");
}

void FileBrowser::cutFile(int fileIndex) {
    if (fileIndex < 0 || fileIndex >= m_fileCount) {
        return;
    }

    FileEntry &file = m_files[fileIndex];

    // Don't cut ".." entry
    if (strcmp(file.name, "..") == 0) {
        return;
    }

    // Build full path and store in clipboard
    if (strcmp(m_currentPath, "/") == 0) {
        snprintf(g_clipboard.path, MAX_PATH_LEN, "/%s", file.name);
    } else {
        snprintf(g_clipboard.path, MAX_PATH_LEN, "%s/%s", m_currentPath, file.name);
    }

    g_clipboard.operation = ClipboardOp::Cut;
    g_clipboard.hasContent = true;

    debug_serial("[filebrowser] Cut to clipboard: ");
    debug_serial(g_clipboard.path);
    debug_serial("\n");
}

bool FileBrowser::pasteFile() {
    if (!g_clipboard.hasContent) {
        return false;
    }

    // Extract filename from clipboard path
    const char *srcFilename = strrchr(g_clipboard.path, '/');
    if (!srcFilename) {
        return false;
    }
    srcFilename++; // Skip the '/'

    // Build destination path
    char destPath[MAX_PATH_LEN];
    if (strcmp(m_currentPath, "/") == 0) {
        snprintf(destPath, MAX_PATH_LEN, "/%s", srcFilename);
    } else {
        snprintf(destPath, MAX_PATH_LEN, "%s/%s", m_currentPath, srcFilename);
    }

    // Don't paste to same location
    if (strcmp(g_clipboard.path, destPath) == 0) {
        debug_serial("[filebrowser] Cannot paste to same location\n");
        return false;
    }

    debug_serial("[filebrowser] Pasting: ");
    debug_serial(g_clipboard.path);
    debug_serial(" -> ");
    debug_serial(destPath);
    debug_serial("\n");

    // Open source file
    FILE *src = fopen(g_clipboard.path, "rb");
    if (!src) {
        debug_serial("[filebrowser] Failed to open source file\n");
        return false;
    }

    // Create destination file
    FILE *dst = fopen(destPath, "wb");
    if (!dst) {
        fclose(src);
        debug_serial("[filebrowser] Failed to create destination file\n");
        return false;
    }

    // Copy data in chunks
    char buffer[4096];
    size_t bytesRead;
    bool success = true;

    while ((bytesRead = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        if (fwrite(buffer, 1, bytesRead, dst) != bytesRead) {
            success = false;
            break;
        }
    }

    fclose(src);
    fclose(dst);

    if (!success) {
        unlink(destPath); // Clean up partial file
        debug_serial("[filebrowser] Copy failed\n");
        return false;
    }

    // If it was a cut operation, delete the source
    if (g_clipboard.operation == ClipboardOp::Cut) {
        unlink(g_clipboard.path);
        g_clipboard.hasContent = false;
    }

    debug_serial("[filebrowser] Paste successful\n");
    return true;
}

bool FileBrowser::createNewFolder() {
    // Generate a unique folder name
    char folderName[MAX_FILENAME_LEN];
    char fullPath[MAX_PATH_LEN];
    int counter = 1;

    while (counter < 100) {
        if (counter == 1) {
            strcpy(folderName, "New Folder");
        } else {
            snprintf(folderName, sizeof(folderName), "New Folder %d", counter);
        }

        // Build full path
        if (strcmp(m_currentPath, "/") == 0) {
            snprintf(fullPath, MAX_PATH_LEN, "/%s", folderName);
        } else {
            snprintf(fullPath, MAX_PATH_LEN, "%s/%s", m_currentPath, folderName);
        }

        // Check if it already exists
        struct stat st;
        if (stat(fullPath, &st) != 0) {
            // Doesn't exist, we can use this name
            break;
        }
        counter++;
    }

    debug_serial("[filebrowser] Creating folder: ");
    debug_serial(fullPath);
    debug_serial("\n");

    int result = mkdir(fullPath, 0755);
    if (result == 0) {
        debug_serial("[filebrowser] Folder created successfully\n");
        return true;
    } else {
        debug_serial("[filebrowser] Failed to create folder\n");
        return false;
    }
}

//------------------------------------------------------------------------------
// Inline Rename Editor
//------------------------------------------------------------------------------

void FileBrowser::startRename(int fileIndex) {
    if (fileIndex < 0 || fileIndex >= m_fileCount) {
        return;
    }

    FileEntry &file = m_files[fileIndex];

    // Don't allow renaming ".."
    if (strcmp(file.name, "..") == 0) {
        return;
    }

    // Initialize rename editor
    m_renameEditor.fileIndex = fileIndex;
    strncpy(m_renameEditor.buffer, file.name, MAX_FILENAME_LEN - 1);
    m_renameEditor.buffer[MAX_FILENAME_LEN - 1] = '\0';
    m_renameEditor.cursorPos = static_cast<int>(strlen(m_renameEditor.buffer));
    m_renameEditor.selStart = 0; // Select all initially
    m_renameEditor.active = true;

    debug_serial("[filebrowser] Started rename for: ");
    debug_serial(file.name);
    debug_serial("\n");

    redraw();
}

void FileBrowser::cancelRename() {
    if (!m_renameEditor.active) {
        return;
    }

    m_renameEditor.active = false;
    m_renameEditor.fileIndex = -1;
    debug_serial("[filebrowser] Rename cancelled\n");
    redraw();
}

void FileBrowser::commitRename() {
    if (!m_renameEditor.active) {
        return;
    }

    int fileIndex = m_renameEditor.fileIndex;
    m_renameEditor.active = false;

    // Validate the new name
    if (m_renameEditor.buffer[0] == '\0') {
        debug_serial("[filebrowser] Empty name, rename cancelled\n");
        redraw();
        return;
    }

    // Check if name actually changed
    if (strcmp(m_files[fileIndex].name, m_renameEditor.buffer) == 0) {
        debug_serial("[filebrowser] Name unchanged\n");
        redraw();
        return;
    }

    // Perform the rename
    if (renameFile(fileIndex, m_renameEditor.buffer)) {
        // Update the file entry with new name
        strncpy(m_files[fileIndex].name, m_renameEditor.buffer, MAX_FILENAME_LEN - 1);
        m_files[fileIndex].name[MAX_FILENAME_LEN - 1] = '\0';
        debug_serial("[filebrowser] Rename committed\n");
    } else {
        debug_serial("[filebrowser] Rename failed\n");
    }

    redraw();
}

void FileBrowser::handleRenameKey(int keycode, bool shift) {
    if (!m_renameEditor.active) {
        return;
    }

    // Key codes (evdev keycodes from kernel/input/keycodes.hpp)
    constexpr int KEY_ENTER = 28;
    constexpr int KEY_ESCAPE = 1;
    constexpr int KEY_BACKSPACE = 14;
    constexpr int KEY_DELETE = 111;
    constexpr int KEY_LEFT = 105;
    constexpr int KEY_RIGHT = 106;
    constexpr int KEY_HOME = 102;
    constexpr int KEY_END = 107;

    int len = static_cast<int>(strlen(m_renameEditor.buffer));

    switch (keycode) {
        case KEY_ENTER:
            commitRename();
            return;

        case KEY_ESCAPE:
            cancelRename();
            return;

        case KEY_BACKSPACE:
            // Delete selection if any, else delete char before cursor
            if (m_renameEditor.selStart >= 0 &&
                m_renameEditor.selStart != m_renameEditor.cursorPos) {
                // Delete selection
                int start = m_renameEditor.selStart < m_renameEditor.cursorPos
                                ? m_renameEditor.selStart
                                : m_renameEditor.cursorPos;
                int end = m_renameEditor.selStart > m_renameEditor.cursorPos
                              ? m_renameEditor.selStart
                              : m_renameEditor.cursorPos;
                memmove(&m_renameEditor.buffer[start], &m_renameEditor.buffer[end], len - end + 1);
                m_renameEditor.cursorPos = start;
                m_renameEditor.selStart = -1;
            } else if (m_renameEditor.cursorPos > 0) {
                memmove(&m_renameEditor.buffer[m_renameEditor.cursorPos - 1],
                        &m_renameEditor.buffer[m_renameEditor.cursorPos],
                        len - m_renameEditor.cursorPos + 1);
                m_renameEditor.cursorPos--;
            }
            m_renameEditor.selStart = -1;
            break;

        case KEY_DELETE:
            if (m_renameEditor.selStart >= 0 &&
                m_renameEditor.selStart != m_renameEditor.cursorPos) {
                // Delete selection
                int start = m_renameEditor.selStart < m_renameEditor.cursorPos
                                ? m_renameEditor.selStart
                                : m_renameEditor.cursorPos;
                int end = m_renameEditor.selStart > m_renameEditor.cursorPos
                              ? m_renameEditor.selStart
                              : m_renameEditor.cursorPos;
                memmove(&m_renameEditor.buffer[start], &m_renameEditor.buffer[end], len - end + 1);
                m_renameEditor.cursorPos = start;
            } else if (m_renameEditor.cursorPos < len) {
                memmove(&m_renameEditor.buffer[m_renameEditor.cursorPos],
                        &m_renameEditor.buffer[m_renameEditor.cursorPos + 1],
                        len - m_renameEditor.cursorPos);
            }
            m_renameEditor.selStart = -1;
            break;

        case KEY_LEFT:
            if (m_renameEditor.cursorPos > 0) {
                if (shift && m_renameEditor.selStart < 0) {
                    m_renameEditor.selStart = m_renameEditor.cursorPos;
                }
                m_renameEditor.cursorPos--;
                if (!shift) {
                    m_renameEditor.selStart = -1;
                }
            }
            break;

        case KEY_RIGHT:
            if (m_renameEditor.cursorPos < len) {
                if (shift && m_renameEditor.selStart < 0) {
                    m_renameEditor.selStart = m_renameEditor.cursorPos;
                }
                m_renameEditor.cursorPos++;
                if (!shift) {
                    m_renameEditor.selStart = -1;
                }
            }
            break;

        case KEY_HOME:
            if (shift && m_renameEditor.selStart < 0) {
                m_renameEditor.selStart = m_renameEditor.cursorPos;
            }
            m_renameEditor.cursorPos = 0;
            if (!shift) {
                m_renameEditor.selStart = -1;
            }
            break;

        case KEY_END:
            if (shift && m_renameEditor.selStart < 0) {
                m_renameEditor.selStart = m_renameEditor.cursorPos;
            }
            m_renameEditor.cursorPos = len;
            if (!shift) {
                m_renameEditor.selStart = -1;
            }
            break;

        default:
            // Check for printable character input (A-Z, a-z, 0-9, etc.)
            // HID key codes: A=0x04, Z=0x1D, 1=0x1E, 0=0x27
            char ch = 0;
            if (keycode >= 0x04 && keycode <= 0x1D) {
                // A-Z
                ch = shift ? ('A' + keycode - 0x04) : ('a' + keycode - 0x04);
            } else if (keycode >= 0x1E && keycode <= 0x26) {
                // 1-9
                ch = '1' + keycode - 0x1E;
            } else if (keycode == 0x27) {
                // 0
                ch = '0';
            } else if (keycode == 0x2D) {
                // - or _
                ch = shift ? '_' : '-';
            } else if (keycode == 0x2E) {
                // = or +
                ch = shift ? '+' : '=';
            } else if (keycode == 0x36) {
                // , or <
                ch = shift ? '<' : ',';
            } else if (keycode == 0x37) {
                // . or >
                ch = shift ? '>' : '.';
            } else if (keycode == 0x2C) {
                // space
                ch = ' ';
            }

            if (ch != 0 && len < MAX_FILENAME_LEN - 1) {
                // Delete selection first if any
                if (m_renameEditor.selStart >= 0 &&
                    m_renameEditor.selStart != m_renameEditor.cursorPos) {
                    int start = m_renameEditor.selStart < m_renameEditor.cursorPos
                                    ? m_renameEditor.selStart
                                    : m_renameEditor.cursorPos;
                    int end = m_renameEditor.selStart > m_renameEditor.cursorPos
                                  ? m_renameEditor.selStart
                                  : m_renameEditor.cursorPos;
                    memmove(
                        &m_renameEditor.buffer[start], &m_renameEditor.buffer[end], len - end + 1);
                    m_renameEditor.cursorPos = start;
                    len = static_cast<int>(strlen(m_renameEditor.buffer));
                }
                m_renameEditor.selStart = -1;

                // Insert character
                if (len < MAX_FILENAME_LEN - 1) {
                    memmove(&m_renameEditor.buffer[m_renameEditor.cursorPos + 1],
                            &m_renameEditor.buffer[m_renameEditor.cursorPos],
                            len - m_renameEditor.cursorPos + 1);
                    m_renameEditor.buffer[m_renameEditor.cursorPos++] = ch;
                }
            }
            break;
    }

    redraw();
}

void FileBrowser::drawRenameEditor() {
    if (!m_renameEditor.active) {
        return;
    }

    // Calculate the position of the file icon being renamed
    int contentY = FB_TOOLBAR_HEIGHT;
    int iconsPerRow = (m_width - 2 * FB_PADDING) / FB_ICON_GRID_X;

    int fileIdx = m_renameEditor.fileIndex;
    int row = fileIdx / iconsPerRow;
    int col = fileIdx % iconsPerRow;

    int iconX = FB_PADDING + col * FB_ICON_GRID_X + FB_ICON_GRID_X / 2;
    int iconY = contentY + FB_PADDING + (row - m_scrollOffset) * FB_ICON_GRID_Y;

    // Position the text editor below the icon
    int editorX = iconX - 50; // Center roughly
    int editorY = iconY + ICON_SIZE + 4;
    int editorW = 100;
    int editorH = 16;

    // Keep editor within window bounds
    if (editorX < 4)
        editorX = 4;
    if (editorX + editorW > m_width - 4)
        editorX = m_width - 4 - editorW;

    // Draw editor background
    gui_fill_rect(m_window, editorX, editorY, editorW, editorH, WB_WHITE);

    // Draw border
    gui_draw_hline(m_window, editorX, editorX + editorW - 1, editorY, WB_GRAY_DARK);
    gui_draw_hline(m_window, editorX, editorX + editorW - 1, editorY + editorH - 1, WB_GRAY_DARK);
    gui_draw_vline(m_window, editorX, editorY, editorY + editorH - 1, WB_GRAY_DARK);
    gui_draw_vline(m_window, editorX + editorW - 1, editorY, editorY + editorH - 1, WB_GRAY_DARK);

    // Draw selection highlight if any
    int textX = editorX + 4;
    int textY = editorY + 3;

    if (m_renameEditor.selStart >= 0 && m_renameEditor.selStart != m_renameEditor.cursorPos) {
        int selStartPx = m_renameEditor.selStart * 8;
        int selEndPx = m_renameEditor.cursorPos * 8;
        if (selStartPx > selEndPx) {
            int tmp = selStartPx;
            selStartPx = selEndPx;
            selEndPx = tmp;
        }
        gui_fill_rect(
            m_window, textX + selStartPx, editorY + 2, selEndPx - selStartPx, editorH - 4, WB_BLUE);
    }

    // Draw text
    gui_draw_text(m_window, textX, textY, m_renameEditor.buffer, WB_BLACK);

    // Draw cursor (blinking could be added later)
    int cursorX = textX + m_renameEditor.cursorPos * 8;
    gui_draw_vline(m_window, cursorX, editorY + 2, editorY + editorH - 3, WB_BLACK);
}

//------------------------------------------------------------------------------
// Properties Dialog
//------------------------------------------------------------------------------

void FileBrowser::showProperties(int fileIndex) {
    if (fileIndex < 0 || fileIndex >= m_fileCount) {
        return;
    }

    FileEntry &file = m_files[fileIndex];

    // Don't show properties for ".."
    if (strcmp(file.name, "..") == 0) {
        return;
    }

    // Build full path
    char fullPath[MAX_PATH_LEN];
    if (strcmp(m_currentPath, "/") == 0) {
        snprintf(fullPath, sizeof(fullPath), "/%s", file.name);
    } else {
        snprintf(fullPath, sizeof(fullPath), "%s/%s", m_currentPath, file.name);
    }

    // Create properties dialog
    gui_window_t *dialog = gui_create_window("Properties", 280, 200);
    if (!dialog) {
        debug_serial("[filebrowser] Failed to create properties dialog\n");
        return;
    }

    // Draw dialog content
    gui_fill_rect(dialog, 0, 0, 280, 200, themeWindowBg());

    // File name (bold/highlighted)
    gui_draw_text(dialog, 15, 15, "Name:", themeText());
    gui_draw_text(dialog, 80, 15, file.name, themeHighlight());

    // File type
    gui_draw_text(dialog, 15, 40, "Type:", themeText());
    const char *typeStr = "Unknown";
    switch (file.type) {
        case FileType::Directory:
            typeStr = "Directory";
            break;
        case FileType::Executable:
            typeStr = "Executable";
            break;
        case FileType::Text:
            typeStr = "Text File";
            break;
        case FileType::Image:
            typeStr = "Image";
            break;
        default:
            typeStr = "File";
            break;
    }
    gui_draw_text(dialog, 80, 40, typeStr, themeTextDisabled());

    // File size
    gui_draw_text(dialog, 15, 65, "Size:", themeText());
    char sizeStr[64];
    if (file.type == FileType::Directory) {
        strcpy(sizeStr, "(directory)");
    } else if (file.size < 1024) {
        snprintf(sizeStr, sizeof(sizeStr), "%llu bytes", (unsigned long long)file.size);
    } else if (file.size < 1024 * 1024) {
        snprintf(sizeStr,
                 sizeof(sizeStr),
                 "%llu KB (%llu bytes)",
                 (unsigned long long)(file.size / 1024),
                 (unsigned long long)file.size);
    } else {
        snprintf(sizeStr,
                 sizeof(sizeStr),
                 "%llu MB (%llu bytes)",
                 (unsigned long long)(file.size / (1024 * 1024)),
                 (unsigned long long)file.size);
    }
    gui_draw_text(dialog, 80, 65, sizeStr, themeTextDisabled());

    // Location
    gui_draw_text(dialog, 15, 90, "Location:", themeText());
    gui_draw_text(dialog, 80, 90, m_currentPath, themeTextDisabled());

    // Full path
    gui_draw_text(dialog, 15, 115, "Path:", themeText());
    // Truncate if too long
    char pathDisplay[32];
    if (strlen(fullPath) > 28) {
        strncpy(pathDisplay, fullPath, 25);
        strcpy(pathDisplay + 25, "...");
    } else {
        strcpy(pathDisplay, fullPath);
    }
    gui_draw_text(dialog, 80, 115, pathDisplay, themeTextDisabled());

    // Separator line
    gui_draw_hline(dialog, 15, 265, 145, themeBorderDark());

    // OK button
    int btnX = 100;
    int btnY = 160;
    int btnW = 80;
    int btnH = 24;
    gui_fill_rect(dialog, btnX, btnY, btnW, btnH, themeMenuBg());
    gui_draw_hline(dialog, btnX, btnX + btnW - 1, btnY, themeBorderLight());
    gui_draw_vline(dialog, btnX, btnY, btnY + btnH - 1, themeBorderLight());
    gui_draw_hline(dialog, btnX, btnX + btnW - 1, btnY + btnH - 1, themeBorderDark());
    gui_draw_vline(dialog, btnX + btnW - 1, btnY, btnY + btnH - 1, themeBorderDark());
    gui_draw_text(dialog, btnX + 30, btnY + 6, "OK", themeText());

    gui_present(dialog);

    // Simple modal loop - wait for close or click
    bool dialogOpen = true;
    while (dialogOpen) {
        gui_event_t event;
        if (gui_poll_event(dialog, &event) == 0) {
            switch (event.type) {
                case GUI_EVENT_CLOSE:
                    dialogOpen = false;
                    break;
                case GUI_EVENT_MOUSE:
                    if (event.mouse.event_type == 1 && event.mouse.button == 0) {
                        // Check OK button click
                        if (event.mouse.x >= btnX && event.mouse.x < btnX + btnW &&
                            event.mouse.y >= btnY && event.mouse.y < btnY + btnH) {
                            dialogOpen = false;
                        }
                    }
                    break;
                case GUI_EVENT_KEY:
                    // Enter or Escape closes dialog
                    if (event.key.pressed &&
                        (event.key.keycode == 0x28 || event.key.keycode == 0x29)) {
                        dialogOpen = false;
                    }
                    break;
                default:
                    break;
            }
        }
        // Yield
        __asm__ volatile("mov x8, #0x0E\n\tsvc #0" ::: "x8");
    }

    gui_destroy_window(dialog);
    debug_serial("[filebrowser] Closed properties dialog\n");
}

} // namespace workbench
