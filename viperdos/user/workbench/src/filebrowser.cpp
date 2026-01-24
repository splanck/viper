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
#include "../include/desktop.hpp"
#include "../include/colors.hpp"
#include "../include/icons.hpp"
#include "../include/utils.hpp"
#include <gui.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>

namespace workbench {

FileBrowser::FileBrowser(Desktop *desktop, const char *initialPath)
    : m_desktop(desktop)
{
    strncpy(m_currentPath, initialPath, MAX_PATH_LEN - 1);
    m_currentPath[MAX_PATH_LEN - 1] = '\0';
}

FileBrowser::~FileBrowser()
{
    if (m_window) {
        gui_destroy_window(m_window);
        m_window = nullptr;
    }
}

bool FileBrowser::init()
{
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

void FileBrowser::loadDirectory()
{
    m_fileCount = 0;
    m_selectedFile = -1;

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
}

FileType FileBrowser::determineFileType(const char *name, bool isDir)
{
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
    if (strcmp(dot, ".txt") == 0 || strcmp(dot, ".md") == 0 ||
        strcmp(dot, ".c") == 0 || strcmp(dot, ".h") == 0 ||
        strcmp(dot, ".cpp") == 0 || strcmp(dot, ".hpp") == 0) {
        return FileType::Text;
    }
    if (strcmp(dot, ".bmp") == 0) {
        return FileType::Image;
    }

    return FileType::Unknown;
}

const uint32_t *FileBrowser::getIconForType(FileType type)
{
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

void FileBrowser::redraw()
{
    if (!m_window) return;

    // Clear background
    gui_fill_rect(m_window, 0, 0, m_width, m_height, WB_GRAY_LIGHT);

    drawToolbar();
    drawFileList();
    drawStatusBar();

    gui_present(m_window);
}

void FileBrowser::drawToolbar()
{
    // Toolbar background
    gui_fill_rect(m_window, 0, 0, m_width, FB_TOOLBAR_HEIGHT, WB_GRAY_LIGHT);

    // Parent button
    gui_fill_rect(m_window, 4, 2, 20, 20, WB_WHITE);
    gui_draw_rect(m_window, 4, 2, 20, 20, WB_BLACK);
    gui_draw_text(m_window, 9, 6, "^", WB_BLACK);

    // Path display
    gui_draw_text(m_window, 30, 6, m_currentPath, WB_BLACK);

    // Bottom border
    gui_draw_hline(m_window, 0, m_width - 1, FB_TOOLBAR_HEIGHT - 1, WB_GRAY_DARK);
}

void FileBrowser::drawFileList()
{
    int listTop = FB_TOOLBAR_HEIGHT;
    int listHeight = m_height - FB_TOOLBAR_HEIGHT - FB_STATUSBAR_HEIGHT;

    // List background - dark blue like desktop
    gui_fill_rect(m_window, 0, listTop, m_width, listHeight, WB_BLUE);

    // Draw files in grid
    int x = FB_PADDING;
    int y = listTop + FB_PADDING - m_scrollOffset;

    for (int i = 0; i < m_fileCount; i++) {
        if (y + FB_ICON_GRID_Y > listTop && y < listTop + listHeight) {
            // Selection highlight
            if (m_files[i].selected) {
                gui_fill_rect(m_window, x - 2, y - 2,
                              FB_ICON_GRID_X - 4, FB_ICON_GRID_Y - 4, WB_ORANGE);
            }

            // Draw icon
            drawFileIcon(x + (FB_ICON_GRID_X - ICON_SIZE) / 2, y, m_files[i].type);

            // Draw filename (truncate if too long)
            char displayName[16];
            strncpy(displayName, m_files[i].name, 15);
            displayName[15] = '\0';

            int textX = x + (FB_ICON_GRID_X - strlen(displayName) * 8) / 2;
            int textY = y + ICON_SIZE + 4;
            // Draw text with shadow for visibility on blue background
            gui_draw_text(m_window, textX + 1, textY + 1, displayName, WB_BLACK);
            gui_draw_text(m_window, textX, textY, displayName, WB_WHITE);
        }

        x += FB_ICON_GRID_X;
        if (x + FB_ICON_GRID_X > m_width) {
            x = FB_PADDING;
            y += FB_ICON_GRID_Y;
        }
    }
}

void FileBrowser::drawFileIcon(int x, int y, FileType type)
{
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

void FileBrowser::drawStatusBar()
{
    int y = m_height - FB_STATUSBAR_HEIGHT;

    // Status bar background
    gui_fill_rect(m_window, 0, y, m_width, FB_STATUSBAR_HEIGHT, WB_GRAY_LIGHT);

    // Top border
    gui_draw_hline(m_window, 0, m_width - 1, y, WB_GRAY_DARK);

    // File count
    char status[64];
    snprintf(status, sizeof(status), "%d items", m_fileCount);
    gui_draw_text(m_window, 8, y + 4, status, WB_BLACK);
}

int FileBrowser::findFileAt(int x, int y)
{
    int listTop = FB_TOOLBAR_HEIGHT;
    int listHeight = m_height - FB_TOOLBAR_HEIGHT - FB_STATUSBAR_HEIGHT;

    if (y < listTop || y >= listTop + listHeight) {
        return -1;
    }

    int gridX = FB_PADDING;
    int gridY = listTop + FB_PADDING - m_scrollOffset;

    for (int i = 0; i < m_fileCount; i++) {
        if (x >= gridX && x < gridX + FB_ICON_GRID_X - 4 &&
            y >= gridY && y < gridY + FB_ICON_GRID_Y - 4) {
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

bool FileBrowser::handleEvent(const gui_event_t &event)
{
    switch (event.type) {
        case GUI_EVENT_MOUSE:
            if (event.mouse.event_type == 1) {  // Button down
                handleClick(event.mouse.x, event.mouse.y, event.mouse.button);
                return true;
            }
            break;

        case GUI_EVENT_CLOSE:
            // Signal desktop to close this browser
            m_desktop->closeFileBrowser(this);
            return true;

        default:
            break;
    }

    return false;
}

void FileBrowser::handleClick(int x, int y, int button)
{
    if (button != 0) return;  // Only handle left button

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

void FileBrowser::handleDoubleClick(int fileIndex)
{
    if (fileIndex < 0 || fileIndex >= m_fileCount) return;

    FileEntry &file = m_files[fileIndex];

    if (file.type == FileType::Directory) {
        // Navigate into directory
        if (strcmp(file.name, "..") == 0) {
            navigateUp();
        } else {
            char newPath[MAX_PATH_LEN];
            if (strcmp(m_currentPath, "/") == 0) {
                snprintf(newPath, sizeof(newPath), "/%s", file.name);
            } else {
                snprintf(newPath, sizeof(newPath), "%s/%s", m_currentPath, file.name);
            }
            navigateTo(newPath);
        }
    } else if (file.type == FileType::Executable) {
        // Launch executable
        char fullPath[MAX_PATH_LEN];
        if (strcmp(m_currentPath, "/") == 0) {
            snprintf(fullPath, sizeof(fullPath), "/%s", file.name);
        } else {
            snprintf(fullPath, sizeof(fullPath), "%s/%s", m_currentPath, file.name);
        }
        m_desktop->spawnProgram(fullPath);
    }
    // TODO: Open text files in editor, images in viewer, etc.
}

void FileBrowser::navigateTo(const char *path)
{
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

void FileBrowser::navigateUp()
{
    if (strcmp(m_currentPath, "/") == 0) {
        return;  // Already at root
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

} // namespace workbench
