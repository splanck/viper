// vg_scrollview.c - Scroll view widget implementation
#include "../../include/vg_event.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_widgets.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void scrollview_destroy(vg_widget_t *widget);
static void scrollview_measure(vg_widget_t *widget, float available_width, float available_height);
static void scrollview_arrange(vg_widget_t *widget, float x, float y, float width, float height);
static void scrollview_paint(vg_widget_t *widget, void *canvas);
static bool scrollview_handle_event(vg_widget_t *widget, vg_event_t *event);

//=============================================================================
// ScrollView VTable
//=============================================================================

static vg_widget_vtable_t g_scrollview_vtable = {.destroy = scrollview_destroy,
                                                 .measure = scrollview_measure,
                                                 .arrange = scrollview_arrange,
                                                 .paint = scrollview_paint,
                                                 .handle_event = scrollview_handle_event,
                                                 .can_focus = NULL,
                                                 .on_focus = NULL};

//=============================================================================
// Helper Functions
//=============================================================================

static void calculate_content_size(vg_scrollview_t *scroll)
{
    vg_widget_t *base = &scroll->base;

    if (scroll->content_width > 0 && scroll->content_height > 0)
    {
        // Use explicit content size
        return;
    }

    // Calculate from children
    float max_right = 0;
    float max_bottom = 0;

    VG_FOREACH_VISIBLE_CHILD(base, child)
    {
        float right = child->x + child->width;
        float bottom = child->y + child->height;

        if (right > max_right)
            max_right = right;
        if (bottom > max_bottom)
            max_bottom = bottom;
    }

    if (scroll->content_width <= 0)
    {
        scroll->content_width = max_right;
    }
    if (scroll->content_height <= 0)
    {
        scroll->content_height = max_bottom;
    }
}

static void clamp_scroll(vg_scrollview_t *scroll)
{
    vg_widget_t *base = &scroll->base;

    float max_scroll_x = scroll->content_width - base->width;
    float max_scroll_y = scroll->content_height - base->height;

    if (scroll->show_v_scrollbar)
    {
        max_scroll_x += scroll->scrollbar_width;
    }
    if (scroll->show_h_scrollbar)
    {
        max_scroll_y += scroll->scrollbar_width;
    }

    if (max_scroll_x < 0)
        max_scroll_x = 0;
    if (max_scroll_y < 0)
        max_scroll_y = 0;

    if (scroll->scroll_x < 0)
        scroll->scroll_x = 0;
    if (scroll->scroll_y < 0)
        scroll->scroll_y = 0;
    if (scroll->scroll_x > max_scroll_x)
        scroll->scroll_x = max_scroll_x;
    if (scroll->scroll_y > max_scroll_y)
        scroll->scroll_y = max_scroll_y;
}

//=============================================================================
// ScrollView Implementation
//=============================================================================

vg_scrollview_t *vg_scrollview_create(vg_widget_t *parent)
{
    vg_scrollview_t *scroll = calloc(1, sizeof(vg_scrollview_t));
    if (!scroll)
        return NULL;

    // Initialize base widget
    vg_widget_init(&scroll->base, VG_WIDGET_SCROLLVIEW, &g_scrollview_vtable);

    // Get theme
    vg_theme_t *theme = vg_theme_get_current();

    // Initialize scrollview-specific fields
    scroll->scroll_x = 0;
    scroll->scroll_y = 0;
    scroll->content_width = 0;  // Auto
    scroll->content_height = 0; // Auto
    scroll->direction = VG_SCROLL_BOTH;

    // Scrollbars
    scroll->show_h_scrollbar = true;
    scroll->show_v_scrollbar = true;
    scroll->auto_hide_scrollbars = true;
    scroll->scrollbar_width = theme->scrollbar.width;

    // Scrollbar appearance
    scroll->track_color = theme->colors.bg_secondary;
    scroll->thumb_color = theme->colors.bg_tertiary;
    scroll->thumb_hover_color = theme->colors.bg_hover;

    // State
    scroll->h_scrollbar_hovered = false;
    scroll->v_scrollbar_hovered = false;
    scroll->h_scrollbar_dragging = false;
    scroll->v_scrollbar_dragging = false;
    scroll->drag_offset = 0;

    // Add to parent
    if (parent)
    {
        vg_widget_add_child(parent, &scroll->base);
    }

    return scroll;
}

static void scrollview_destroy(vg_widget_t *widget)
{
    // No special cleanup needed
    (void)widget;
}

static void scrollview_measure(vg_widget_t *widget, float available_width, float available_height)
{
    // ScrollView takes all available space by default
    widget->measured_width = available_width > 0 ? available_width : 200;
    widget->measured_height = available_height > 0 ? available_height : 200;

    // Apply constraints
    if (widget->constraints.preferred_width > 0)
    {
        widget->measured_width = widget->constraints.preferred_width;
    }
    if (widget->constraints.preferred_height > 0)
    {
        widget->measured_height = widget->constraints.preferred_height;
    }
    if (widget->measured_width < widget->constraints.min_width)
    {
        widget->measured_width = widget->constraints.min_width;
    }
    if (widget->measured_height < widget->constraints.min_height)
    {
        widget->measured_height = widget->constraints.min_height;
    }
}

static void scrollview_arrange(vg_widget_t *widget, float x, float y, float width, float height)
{
    vg_scrollview_t *scroll = (vg_scrollview_t *)widget;

    widget->x = x;
    widget->y = y;
    widget->width = width;
    widget->height = height;

    // Calculate content size
    calculate_content_size(scroll);

    // Determine if scrollbars are needed
    bool needs_h = (scroll->direction & VG_SCROLL_HORIZONTAL) && scroll->content_width > width;
    bool needs_v = (scroll->direction & VG_SCROLL_VERTICAL) && scroll->content_height > height;

    if (scroll->auto_hide_scrollbars)
    {
        scroll->show_h_scrollbar = needs_h;
        scroll->show_v_scrollbar = needs_v;
    }

    // Clamp scroll position
    clamp_scroll(scroll);

    // Arrange children at their positions minus scroll offset
    VG_FOREACH_VISIBLE_CHILD(widget, child)
    {
        // Measure child
        if (child->vtable && child->vtable->measure)
        {
            child->vtable->measure(child, scroll->content_width, scroll->content_height);
        }

        // Position child with scroll offset
        float child_x = child->layout.margin_left - scroll->scroll_x;
        float child_y = child->layout.margin_top - scroll->scroll_y;
        float child_w = child->measured_width;
        float child_h = child->measured_height;

        if (child->vtable && child->vtable->arrange)
        {
            child->vtable->arrange(child, child_x, child_y, child_w, child_h);
        }
        else
        {
            child->x = child_x;
            child->y = child_y;
            child->width = child_w;
            child->height = child_h;
        }
    }
}

static void scrollview_paint(vg_widget_t *widget, void *canvas)
{
    vg_scrollview_t *scroll = (vg_scrollview_t *)widget;
    vg_theme_t *theme = vg_theme_get_current();

    // Draw children (clipping would be done by canvas)
    // Children are already positioned with scroll offset
    VG_FOREACH_VISIBLE_CHILD(widget, child)
    {
        if (child->vtable && child->vtable->paint)
        {
            child->vtable->paint(child, canvas);
        }
    }

    // Draw scrollbars
    float content_area_width =
        widget->width - (scroll->show_v_scrollbar ? scroll->scrollbar_width : 0);
    float content_area_height =
        widget->height - (scroll->show_h_scrollbar ? scroll->scrollbar_width : 0);

    // Vertical scrollbar
    if (scroll->show_v_scrollbar && scroll->content_height > content_area_height)
    {
        float track_x = widget->x + widget->width - scroll->scrollbar_width;
        float track_y = widget->y;
        float track_height = content_area_height;

        // Draw track
        // TODO: Use vgfx primitives
        (void)track_x;
        (void)track_y;

        // Calculate thumb size and position
        float visible_ratio = content_area_height / scroll->content_height;
        if (visible_ratio > 1.0f)
            visible_ratio = 1.0f;

        float thumb_height = track_height * visible_ratio;
        if (thumb_height < theme->scrollbar.min_thumb_size)
        {
            thumb_height = theme->scrollbar.min_thumb_size;
        }

        float scroll_ratio = scroll->scroll_y / (scroll->content_height - content_area_height);
        float thumb_y = track_y + scroll_ratio * (track_height - thumb_height);

        // Draw thumb
        uint32_t thumb_color = scroll->v_scrollbar_hovered || scroll->v_scrollbar_dragging
                                   ? scroll->thumb_hover_color
                                   : scroll->thumb_color;
        // TODO: Use vgfx primitives
        (void)thumb_height;
        (void)thumb_y;
        (void)thumb_color;
    }

    // Horizontal scrollbar
    if (scroll->show_h_scrollbar && scroll->content_width > content_area_width)
    {
        float track_x = widget->x;
        float track_y = widget->y + widget->height - scroll->scrollbar_width;
        float track_width = content_area_width;

        // Draw track
        // TODO: Use vgfx primitives
        (void)track_x;
        (void)track_y;

        // Calculate thumb size and position
        float visible_ratio = content_area_width / scroll->content_width;
        if (visible_ratio > 1.0f)
            visible_ratio = 1.0f;

        float thumb_width = track_width * visible_ratio;
        if (thumb_width < theme->scrollbar.min_thumb_size)
        {
            thumb_width = theme->scrollbar.min_thumb_size;
        }

        float scroll_ratio = scroll->scroll_x / (scroll->content_width - content_area_width);
        float thumb_x = track_x + scroll_ratio * (track_width - thumb_width);

        // Draw thumb
        uint32_t thumb_color = scroll->h_scrollbar_hovered || scroll->h_scrollbar_dragging
                                   ? scroll->thumb_hover_color
                                   : scroll->thumb_color;
        // TODO: Use vgfx primitives
        (void)thumb_width;
        (void)thumb_x;
        (void)thumb_color;
    }
}

static bool scrollview_handle_event(vg_widget_t *widget, vg_event_t *event)
{
    vg_scrollview_t *scroll = (vg_scrollview_t *)widget;

    if (event->type == VG_EVENT_MOUSE_WHEEL)
    {
        // Scroll content
        float delta_x = event->wheel.delta_x * 20.0f;
        float delta_y = event->wheel.delta_y * 20.0f;

        if (scroll->direction & VG_SCROLL_HORIZONTAL)
        {
            scroll->scroll_x -= delta_x;
        }
        if (scroll->direction & VG_SCROLL_VERTICAL)
        {
            scroll->scroll_y -= delta_y;
        }

        clamp_scroll(scroll);
        widget->needs_layout = true;
        widget->needs_paint = true;
        return true;
    }

    if (event->type == VG_EVENT_MOUSE_DOWN)
    {
        // Check if clicking on scrollbar
        float content_area_width =
            widget->width - (scroll->show_v_scrollbar ? scroll->scrollbar_width : 0);
        float content_area_height =
            widget->height - (scroll->show_h_scrollbar ? scroll->scrollbar_width : 0);

        // Vertical scrollbar area
        if (scroll->show_v_scrollbar && event->mouse.x >= widget->width - scroll->scrollbar_width)
        {
            scroll->v_scrollbar_dragging = true;
            scroll->drag_offset = event->mouse.y;
            return true;
        }

        // Horizontal scrollbar area
        if (scroll->show_h_scrollbar && event->mouse.y >= widget->height - scroll->scrollbar_width)
        {
            scroll->h_scrollbar_dragging = true;
            scroll->drag_offset = event->mouse.x;
            return true;
        }

        (void)content_area_width;
        (void)content_area_height;
    }

    if (event->type == VG_EVENT_MOUSE_UP)
    {
        scroll->v_scrollbar_dragging = false;
        scroll->h_scrollbar_dragging = false;
    }

    if (event->type == VG_EVENT_MOUSE_MOVE)
    {
        float content_area_height =
            widget->height - (scroll->show_h_scrollbar ? scroll->scrollbar_width : 0);
        float content_area_width =
            widget->width - (scroll->show_v_scrollbar ? scroll->scrollbar_width : 0);

        if (scroll->v_scrollbar_dragging)
        {
            float delta = event->mouse.y - scroll->drag_offset;
            float track_height = content_area_height;
            float scroll_range = scroll->content_height - content_area_height;
            if (scroll_range > 0)
            {
                scroll->scroll_y += delta * (scroll_range / track_height);
                clamp_scroll(scroll);
                scroll->drag_offset = event->mouse.y;
                widget->needs_layout = true;
                widget->needs_paint = true;
            }
            return true;
        }

        if (scroll->h_scrollbar_dragging)
        {
            float delta = event->mouse.x - scroll->drag_offset;
            float track_width = content_area_width;
            float scroll_range = scroll->content_width - content_area_width;
            if (scroll_range > 0)
            {
                scroll->scroll_x += delta * (scroll_range / track_width);
                clamp_scroll(scroll);
                scroll->drag_offset = event->mouse.x;
                widget->needs_layout = true;
                widget->needs_paint = true;
            }
            return true;
        }

        // Update hover state
        bool was_h_hover = scroll->h_scrollbar_hovered;
        bool was_v_hover = scroll->v_scrollbar_hovered;

        scroll->v_scrollbar_hovered =
            scroll->show_v_scrollbar && event->mouse.x >= widget->width - scroll->scrollbar_width;
        scroll->h_scrollbar_hovered =
            scroll->show_h_scrollbar && event->mouse.y >= widget->height - scroll->scrollbar_width;

        if (was_h_hover != scroll->h_scrollbar_hovered ||
            was_v_hover != scroll->v_scrollbar_hovered)
        {
            widget->needs_paint = true;
        }
    }

    return false;
}

//=============================================================================
// ScrollView API
//=============================================================================

void vg_scrollview_set_scroll(vg_scrollview_t *scroll, float x, float y)
{
    if (!scroll)
        return;

    scroll->scroll_x = x;
    scroll->scroll_y = y;
    clamp_scroll(scroll);
    scroll->base.needs_layout = true;
    scroll->base.needs_paint = true;
}

void vg_scrollview_get_scroll(vg_scrollview_t *scroll, float *out_x, float *out_y)
{
    if (!scroll)
        return;

    if (out_x)
        *out_x = scroll->scroll_x;
    if (out_y)
        *out_y = scroll->scroll_y;
}

void vg_scrollview_set_content_size(vg_scrollview_t *scroll, float width, float height)
{
    if (!scroll)
        return;

    scroll->content_width = width;
    scroll->content_height = height;
    clamp_scroll(scroll);
    scroll->base.needs_layout = true;
    scroll->base.needs_paint = true;
}

void vg_scrollview_scroll_to_widget(vg_scrollview_t *scroll, vg_widget_t *child)
{
    if (!scroll || !child)
        return;

    vg_widget_t *base = &scroll->base;

    // Check if child is descendant
    vg_widget_t *p = child->parent;
    while (p && p != base)
        p = p->parent;
    if (p != base)
        return; // Not a descendant

    float content_area_width =
        base->width - (scroll->show_v_scrollbar ? scroll->scrollbar_width : 0);
    float content_area_height =
        base->height - (scroll->show_h_scrollbar ? scroll->scrollbar_width : 0);

    // Get child position relative to scrollview
    float child_x = child->x + scroll->scroll_x;
    float child_y = child->y + scroll->scroll_y;

    // Scroll to make child visible
    if (child_x < scroll->scroll_x)
    {
        scroll->scroll_x = child_x;
    }
    else if (child_x + child->width > scroll->scroll_x + content_area_width)
    {
        scroll->scroll_x = child_x + child->width - content_area_width;
    }

    if (child_y < scroll->scroll_y)
    {
        scroll->scroll_y = child_y;
    }
    else if (child_y + child->height > scroll->scroll_y + content_area_height)
    {
        scroll->scroll_y = child_y + child->height - content_area_height;
    }

    clamp_scroll(scroll);
    base->needs_layout = true;
    base->needs_paint = true;
}

void vg_scrollview_set_direction(vg_scrollview_t *scroll, vg_scroll_direction_t direction)
{
    if (!scroll)
        return;

    scroll->direction = direction;
    scroll->base.needs_layout = true;
    scroll->base.needs_paint = true;
}
