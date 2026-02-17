# Viper Web Engine & Browser Control Implementation Plan

**Status**: Draft
**Author**: Development Team
**Created**: 2026-01-20
**Last Updated**: 2026-01-20

---

## Executive Summary

This document outlines the implementation plan for a custom web rendering engine built from scratch for the Viper project. Unlike traditional approaches that embed existing browsers (WebView2, CEF, WKWebView), we will develop our own HTML/CSS parser, layout engine, and renderer.

**Goals:**
1. Build a standalone web engine library (`libviperweb`)
2. Integrate it into `Viper.GUI.WebBrowser` control
3. Start with static HTML rendering, incrementally adding features
4. Full control over rendering pipeline for customization and learning

**Non-Goals (Initially):**
- Full HTML5/CSS3 compliance (progressive enhancement)
- JavaScript execution (Phase 4+)
- Media playback (audio/video)
- Network stack (use existing HTTP libraries)

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Phase 1: HTML Parsing & DOM](#2-phase-1-html-parsing--dom)
3. [Phase 2: CSS Parsing & CSSOM](#3-phase-2-css-parsing--cssom)
4. [Phase 3: Style Resolution](#4-phase-3-style-resolution)
5. [Phase 4: Layout Engine](#5-phase-4-layout-engine)
6. [Phase 5: Rendering/Painting](#6-phase-5-renderingpainting)
7. [Phase 6: Interactivity](#7-phase-6-interactivity)
8. [Phase 7: GUI Integration](#8-phase-7-gui-integration)
9. [Phase 8: Advanced Features](#9-phase-8-advanced-features)
10. [Data Structures](#10-data-structures)
11. [File Organization](#11-file-organization)
12. [Testing Strategy](#12-testing-strategy)
13. [Milestones & Timeline](#13-milestones--timeline)
14. [References](#14-references)

---

## 1. Architecture Overview

### High-Level Pipeline

```
                    libviperweb Architecture
┌─────────────────────────────────────────────────────────────────┐
│                                                                 │
│   HTML Source          CSS Source         User Events           │
│       │                    │                   │                │
│       ▼                    ▼                   │                │
│  ┌─────────┐         ┌─────────┐              │                │
│  │  HTML   │         │   CSS   │              │                │
│  │ Lexer   │         │  Lexer  │              │                │
│  └────┬────┘         └────┬────┘              │                │
│       ▼                   ▼                   │                │
│  ┌─────────┐         ┌─────────┐              │                │
│  │  HTML   │         │   CSS   │              │                │
│  │ Parser  │         │ Parser  │              │                │
│  └────┬────┘         └────┬────┘              │                │
│       ▼                   ▼                   │                │
│  ┌─────────┐         ┌─────────┐              │                │
│  │   DOM   │◄────────│  CSSOM  │              │                │
│  │  Tree   │         │ (Rules) │              │                │
│  └────┬────┘         └────┬────┘              │                │
│       │                   │                   │                │
│       └───────┬───────────┘                   │                │
│               ▼                               │                │
│        ┌────────────┐                         │                │
│        │   Style    │                         │                │
│        │ Resolution │                         │                │
│        └─────┬──────┘                         │                │
│              ▼                                │                │
│        ┌────────────┐                         │                │
│        │  Render    │ (Styled DOM)            │                │
│        │   Tree     │                         │                │
│        └─────┬──────┘                         │                │
│              ▼                                │                │
│        ┌────────────┐                         │                │
│        │   Layout   │ ◄───────────────────────┘                │
│        │  (Boxing)  │   (reflow on interaction)                │
│        └─────┬──────┘                                          │
│              ▼                                                 │
│        ┌────────────┐                                          │
│        │   Paint    │                                          │
│        │  (Render)  │                                          │
│        └─────┬──────┘                                          │
│              ▼                                                 │
│        ┌────────────┐                                          │
│        │   Canvas   │ (Viper.Graphics)                         │
│        │   Output   │                                          │
│        └────────────┘                                          │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Layer Separation

```
┌────────────────────────────────────────────────────────────────┐
│                     Zia Application Layer                       │
│  (Viper.GUI.WebBrowser class usage)                            │
├────────────────────────────────────────────────────────────────┤
│                     Runtime Bindings (rt_web.h)                │
│  (C ABI: rt_web_*, opaque handles)                             │
├────────────────────────────────────────────────────────────────┤
│                     GUI Integration (vg_webbrowser.c)          │
│  (Widget vtable, event routing, repaint triggers)              │
├────────────────────────────────────────────────────────────────┤
│                     libviperweb (Pure C Library)               │
│  (HTML/CSS parsing, DOM, layout, rendering)                    │
├────────────────────────────────────────────────────────────────┤
│                     Viper.Graphics (Canvas)                    │
│  (Drawing primitives: boxes, text, images)                     │
└────────────────────────────────────────────────────────────────┘
```

---

## 2. Phase 1: HTML Parsing & DOM

### 2.1 HTML Lexer (Tokenizer)

The lexer converts raw HTML text into a stream of tokens.

**Token Types:**
```c
typedef enum {
    HTML_TOKEN_DOCTYPE,           // <!DOCTYPE html>
    HTML_TOKEN_START_TAG,         // <div>
    HTML_TOKEN_END_TAG,           // </div>
    HTML_TOKEN_SELF_CLOSING_TAG,  // <br/> or <img .../>
    HTML_TOKEN_TEXT,              // Text content
    HTML_TOKEN_COMMENT,           // <!-- comment -->
    HTML_TOKEN_CDATA,             // <![CDATA[...]]>
    HTML_TOKEN_EOF,               // End of input
    HTML_TOKEN_ERROR,             // Malformed input
} vw_html_token_type_t;

typedef struct {
    vw_html_token_type_t type;
    const char* start;            // Pointer into source
    size_t length;                // Token length

    // For tags:
    const char* tag_name;
    size_t tag_name_len;
    vw_html_attr_t* attributes;   // Linked list
    size_t attr_count;

    // Location for error reporting
    size_t line;
    size_t column;
} vw_html_token_t;

typedef struct vw_html_attr {
    const char* name;
    size_t name_len;
    const char* value;            // NULL if boolean attribute
    size_t value_len;
    struct vw_html_attr* next;
} vw_html_attr_t;
```

**Lexer API:**
```c
typedef struct vw_html_lexer vw_html_lexer_t;

vw_html_lexer_t* vw_html_lexer_create(const char* html, size_t len);
void vw_html_lexer_destroy(vw_html_lexer_t* lexer);

// Returns next token (caller does NOT own memory; valid until next call)
const vw_html_token_t* vw_html_lexer_next(vw_html_lexer_t* lexer);

// Peek without consuming
const vw_html_token_t* vw_html_lexer_peek(vw_html_lexer_t* lexer);
```

**Lexer State Machine:**
```
States:
- DATA: Normal text content
- TAG_OPEN: After '<'
- END_TAG_OPEN: After '</'
- TAG_NAME: Reading tag name
- BEFORE_ATTR_NAME: Whitespace before attribute
- ATTR_NAME: Reading attribute name
- AFTER_ATTR_NAME: After attribute name, before '='
- BEFORE_ATTR_VALUE: After '='
- ATTR_VALUE_DOUBLE_QUOTED: Inside "..."
- ATTR_VALUE_SINGLE_QUOTED: Inside '...'
- ATTR_VALUE_UNQUOTED: No quotes
- SELF_CLOSING_START_TAG: After '/' in tag
- COMMENT_START: After '<!--'
- COMMENT: Inside comment
- COMMENT_END: After '-->'
- DOCTYPE: Inside <!DOCTYPE ...>
```

### 2.2 HTML Parser

The parser builds a DOM tree from tokens using a modified tree-building algorithm.

**Insertion Modes (Simplified):**
```c
typedef enum {
    HTML_MODE_INITIAL,
    HTML_MODE_BEFORE_HTML,
    HTML_MODE_BEFORE_HEAD,
    HTML_MODE_IN_HEAD,
    HTML_MODE_AFTER_HEAD,
    HTML_MODE_IN_BODY,
    HTML_MODE_TEXT,               // <script>, <style> raw text
    HTML_MODE_IN_TABLE,
    HTML_MODE_IN_SELECT,
    HTML_MODE_AFTER_BODY,
    HTML_MODE_AFTER_AFTER_BODY,
} vw_html_insertion_mode_t;
```

**Parser API:**
```c
typedef struct vw_html_parser vw_html_parser_t;

vw_html_parser_t* vw_html_parser_create(void);
void vw_html_parser_destroy(vw_html_parser_t* parser);

// Parse HTML string, returns DOM document (caller owns)
vw_dom_document_t* vw_html_parser_parse(vw_html_parser_t* parser,
                                         const char* html,
                                         size_t len);

// Parse fragment (for innerHTML)
vw_dom_node_t* vw_html_parser_parse_fragment(vw_html_parser_t* parser,
                                              const char* html,
                                              size_t len,
                                              vw_dom_element_t* context);

// Error handling
size_t vw_html_parser_error_count(vw_html_parser_t* parser);
const char* vw_html_parser_error_at(vw_html_parser_t* parser, size_t index);
```

### 2.3 DOM Tree

**Node Types:**
```c
typedef enum {
    VW_DOM_NODE_DOCUMENT,         // Root document
    VW_DOM_NODE_DOCTYPE,          // <!DOCTYPE>
    VW_DOM_NODE_ELEMENT,          // <tag>...</tag>
    VW_DOM_NODE_TEXT,             // Text content
    VW_DOM_NODE_COMMENT,          // <!-- ... -->
    VW_DOM_NODE_CDATA,            // <![CDATA[...]]>
    VW_DOM_NODE_FRAGMENT,         // Document fragment
} vw_dom_node_type_t;

// Base node structure (all nodes inherit from this)
typedef struct vw_dom_node {
    vw_dom_node_type_t type;

    // Tree structure
    struct vw_dom_node* parent;
    struct vw_dom_node* first_child;
    struct vw_dom_node* last_child;
    struct vw_dom_node* prev_sibling;
    struct vw_dom_node* next_sibling;

    // Owner document
    struct vw_dom_document* owner_document;

    // For layout/render tree linkage
    void* layout_node;            // Points to vw_layout_box_t
    void* render_data;            // Cached computed styles
} vw_dom_node_t;

// Element node
typedef struct vw_dom_element {
    vw_dom_node_t base;

    // Tag info
    const char* tag_name;         // Interned string (e.g., "div", "span")
    const char* namespace_uri;    // NULL for HTML namespace

    // Attributes (hash map for fast lookup)
    vw_string_map_t* attributes;

    // Classes (parsed from class attribute)
    const char** class_list;
    size_t class_count;

    // ID (cached from id attribute)
    const char* id;

    // Inline styles (parsed from style attribute)
    struct vw_css_declaration_block* inline_style;
} vw_dom_element_t;

// Text node
typedef struct vw_dom_text {
    vw_dom_node_t base;
    char* data;                   // Text content (owned)
    size_t length;
} vw_dom_text_t;

// Document node
typedef struct vw_dom_document {
    vw_dom_node_t base;

    // Document-level info
    const char* title;
    const char* base_url;
    const char* content_type;     // "text/html"

    // Quick access
    vw_dom_element_t* document_element;  // <html>
    vw_dom_element_t* head;
    vw_dom_element_t* body;

    // Style sheets attached to document
    struct vw_css_stylesheet** stylesheets;
    size_t stylesheet_count;

    // String interning for tag names, attribute names
    vw_string_interner_t* interner;

    // Memory arena for nodes
    vw_arena_t* arena;
} vw_dom_document_t;
```

**DOM API:**
```c
// Document creation
vw_dom_document_t* vw_dom_document_create(void);
void vw_dom_document_destroy(vw_dom_document_t* doc);

// Node creation
vw_dom_element_t* vw_dom_create_element(vw_dom_document_t* doc,
                                         const char* tag_name);
vw_dom_text_t* vw_dom_create_text(vw_dom_document_t* doc,
                                   const char* data);
vw_dom_node_t* vw_dom_create_comment(vw_dom_document_t* doc,
                                      const char* data);

// Tree manipulation
void vw_dom_append_child(vw_dom_node_t* parent, vw_dom_node_t* child);
void vw_dom_insert_before(vw_dom_node_t* parent,
                          vw_dom_node_t* new_child,
                          vw_dom_node_t* ref_child);
void vw_dom_remove_child(vw_dom_node_t* parent, vw_dom_node_t* child);
vw_dom_node_t* vw_dom_clone_node(vw_dom_node_t* node, bool deep);

// Element attributes
void vw_dom_set_attribute(vw_dom_element_t* elem,
                          const char* name,
                          const char* value);
const char* vw_dom_get_attribute(vw_dom_element_t* elem,
                                  const char* name);
bool vw_dom_has_attribute(vw_dom_element_t* elem, const char* name);
void vw_dom_remove_attribute(vw_dom_element_t* elem, const char* name);

// Queries
vw_dom_element_t* vw_dom_get_element_by_id(vw_dom_document_t* doc,
                                            const char* id);
vw_dom_node_list_t* vw_dom_get_elements_by_tag(vw_dom_element_t* root,
                                                const char* tag);
vw_dom_node_list_t* vw_dom_get_elements_by_class(vw_dom_element_t* root,
                                                  const char* class_name);
vw_dom_element_t* vw_dom_query_selector(vw_dom_element_t* root,
                                         const char* selector);
vw_dom_node_list_t* vw_dom_query_selector_all(vw_dom_element_t* root,
                                               const char* selector);

// Text content
const char* vw_dom_get_text_content(vw_dom_node_t* node);
void vw_dom_set_text_content(vw_dom_node_t* node, const char* text);

// innerHTML/outerHTML
const char* vw_dom_get_inner_html(vw_dom_element_t* elem);
void vw_dom_set_inner_html(vw_dom_element_t* elem, const char* html);
```

### 2.4 Supported HTML Elements (Initial)

**Phase 1 Elements:**
```
Document structure: html, head, body, title
Sections: div, span, p, h1-h6, header, footer, main, section, article, aside, nav
Text: a, strong, em, b, i, u, s, mark, small, sub, sup, br, hr
Lists: ul, ol, li
Tables: table, thead, tbody, tfoot, tr, th, td
Forms (display only): form, input, button, label, textarea, select, option
Media: img (display), figure, figcaption
Semantic: blockquote, pre, code, address
```

**Void Elements (self-closing):**
```
area, base, br, col, embed, hr, img, input, link, meta, param, source, track, wbr
```

---

## 3. Phase 2: CSS Parsing & CSSOM

### 3.1 CSS Lexer

**Token Types:**
```c
typedef enum {
    CSS_TOKEN_IDENT,              // identifier
    CSS_TOKEN_FUNCTION,           // function-name(
    CSS_TOKEN_AT_KEYWORD,         // @import, @media, etc.
    CSS_TOKEN_HASH,               // #id or #fff
    CSS_TOKEN_STRING,             // "string" or 'string'
    CSS_TOKEN_URL,                // url(...)
    CSS_TOKEN_NUMBER,             // 123, 1.5
    CSS_TOKEN_DIMENSION,          // 10px, 2em
    CSS_TOKEN_PERCENTAGE,         // 50%
    CSS_TOKEN_WHITESPACE,
    CSS_TOKEN_DELIM,              // Single character: . > + ~ * : ,
    CSS_TOKEN_COLON,              // :
    CSS_TOKEN_SEMICOLON,          // ;
    CSS_TOKEN_COMMA,              // ,
    CSS_TOKEN_OPEN_PAREN,         // (
    CSS_TOKEN_CLOSE_PAREN,        // )
    CSS_TOKEN_OPEN_BRACKET,       // [
    CSS_TOKEN_CLOSE_BRACKET,      // ]
    CSS_TOKEN_OPEN_BRACE,         // {
    CSS_TOKEN_CLOSE_BRACE,        // }
    CSS_TOKEN_CDC,                // -->
    CSS_TOKEN_CDO,                // <!--
    CSS_TOKEN_COMMENT,            // /* ... */
    CSS_TOKEN_EOF,
    CSS_TOKEN_ERROR,
} vw_css_token_type_t;

typedef struct {
    vw_css_token_type_t type;
    const char* start;
    size_t length;

    union {
        double number;
        struct {
            double value;
            const char* unit;
            size_t unit_len;
        } dimension;
        struct {
            const char* value;
            size_t value_len;
        } string;
    };

    size_t line;
    size_t column;
} vw_css_token_t;
```

### 3.2 CSS Parser

**Stylesheet Structure:**
```c
// A complete stylesheet
typedef struct vw_css_stylesheet {
    const char* href;             // Source URL (if external)
    vw_css_rule_t** rules;        // Array of rules
    size_t rule_count;
    size_t rule_capacity;

    // Media query (for @import/@media)
    vw_css_media_query_t* media;
} vw_css_stylesheet_t;

// Rule types
typedef enum {
    CSS_RULE_STYLE,               // selector { declarations }
    CSS_RULE_IMPORT,              // @import url(...);
    CSS_RULE_MEDIA,               // @media (...) { rules }
    CSS_RULE_FONT_FACE,           // @font-face { ... }
    CSS_RULE_KEYFRAMES,           // @keyframes name { ... }
} vw_css_rule_type_t;

typedef struct vw_css_rule {
    vw_css_rule_type_t type;
} vw_css_rule_t;

// Style rule: selector { property: value; ... }
typedef struct vw_css_style_rule {
    vw_css_rule_t base;
    vw_css_selector_t* selector;  // Selector chain
    vw_css_declaration_block_t* declarations;
} vw_css_style_rule_t;

// Media rule: @media (...) { rules }
typedef struct vw_css_media_rule {
    vw_css_rule_t base;
    vw_css_media_query_t* query;
    vw_css_rule_t** rules;
    size_t rule_count;
} vw_css_media_rule_t;
```

### 3.3 Selectors

**Selector Components:**
```c
typedef enum {
    CSS_SELECTOR_UNIVERSAL,       // *
    CSS_SELECTOR_TYPE,            // div, span
    CSS_SELECTOR_CLASS,           // .class
    CSS_SELECTOR_ID,              // #id
    CSS_SELECTOR_ATTRIBUTE,       // [attr], [attr=val], [attr~=val]
    CSS_SELECTOR_PSEUDO_CLASS,    // :hover, :first-child
    CSS_SELECTOR_PSEUDO_ELEMENT,  // ::before, ::after
} vw_css_selector_type_t;

typedef enum {
    CSS_COMBINATOR_NONE,          // Single selector
    CSS_COMBINATOR_DESCENDANT,    // A B (space)
    CSS_COMBINATOR_CHILD,         // A > B
    CSS_COMBINATOR_NEXT_SIBLING,  // A + B
    CSS_COMBINATOR_SUBSEQUENT,    // A ~ B
} vw_css_combinator_t;

typedef struct vw_css_simple_selector {
    vw_css_selector_type_t type;

    union {
        const char* name;         // For TYPE, CLASS, ID, PSEUDO_*
        struct {
            const char* attr;
            const char* value;
            enum { ATTR_EXISTS, ATTR_EQUALS, ATTR_CONTAINS, ATTR_STARTS, ATTR_ENDS, ATTR_DASH } op;
        } attribute;
    };

    struct vw_css_simple_selector* next;  // Chain: div.class#id
} vw_css_simple_selector_t;

typedef struct vw_css_selector {
    vw_css_simple_selector_t* simple;     // Compound selector
    vw_css_combinator_t combinator;
    struct vw_css_selector* next;         // Next in chain: A > B + C

    // Precomputed specificity
    uint32_t specificity;                 // (a << 16) | (b << 8) | c
} vw_css_selector_t;
```

**Specificity Calculation:**
```
(inline, id, class, type)
- Inline style:           (1, 0, 0, 0) = 0x1000000
- ID selector (#id):      (0, 1, 0, 0) = 0x10000
- Class/attr/pseudo-class:(0, 0, 1, 0) = 0x100
- Type/pseudo-element:    (0, 0, 0, 1) = 0x1
```

### 3.4 CSS Values

**Value Types:**
```c
typedef enum {
    CSS_VALUE_KEYWORD,            // inherit, initial, unset, auto, none, etc.
    CSS_VALUE_NUMBER,             // Unitless number
    CSS_VALUE_LENGTH,             // px, em, rem, vw, vh, %, etc.
    CSS_VALUE_COLOR,              // Color value
    CSS_VALUE_STRING,             // "string"
    CSS_VALUE_URL,                // url(...)
    CSS_VALUE_FUNCTION,           // calc(), var(), etc.
    CSS_VALUE_LIST,               // Multiple values
} vw_css_value_type_t;

typedef enum {
    CSS_UNIT_PX,                  // Pixels (absolute)
    CSS_UNIT_EM,                  // Relative to font-size
    CSS_UNIT_REM,                 // Relative to root font-size
    CSS_UNIT_PERCENT,             // Percentage
    CSS_UNIT_VW, CSS_UNIT_VH,     // Viewport units
    CSS_UNIT_VMIN, CSS_UNIT_VMAX,
    CSS_UNIT_PT, CSS_UNIT_CM, CSS_UNIT_MM, CSS_UNIT_IN,  // Absolute
    CSS_UNIT_CH, CSS_UNIT_EX,     // Character units
} vw_css_unit_t;

typedef struct vw_css_value {
    vw_css_value_type_t type;

    union {
        int keyword;              // CSS_KEYWORD_* enum
        double number;
        struct {
            double value;
            vw_css_unit_t unit;
        } length;
        uint32_t color;           // ARGB
        const char* string;
        struct {
            const char* name;
            struct vw_css_value** args;
            size_t arg_count;
        } function;
        struct {
            struct vw_css_value** items;
            size_t count;
        } list;
    };
} vw_css_value_t;
```

### 3.5 Declarations

```c
typedef struct vw_css_declaration {
    int property;                 // CSS_PROP_* enum
    vw_css_value_t* value;
    bool important;
    struct vw_css_declaration* next;
} vw_css_declaration_t;

typedef struct vw_css_declaration_block {
    vw_css_declaration_t* first;
    vw_css_declaration_t* last;
    size_t count;
} vw_css_declaration_block_t;
```

### 3.6 Supported CSS Properties (Initial)

**Phase 2 Properties:**
```
Display:        display, visibility
Position:       position, top, right, bottom, left, z-index
Box Model:      width, height, min-*, max-*, margin, padding, border
Flex:           flex-direction, flex-wrap, justify-content, align-items, flex-grow, etc.
Colors:         color, background-color, border-color, opacity
Typography:     font-family, font-size, font-weight, font-style, line-height
Text:           text-align, text-decoration, text-transform, white-space, overflow
```

---

## 4. Phase 3: Style Resolution

### 4.1 Overview

Style resolution determines the final computed style for each element by:

1. **Collecting applicable rules** - Find all rules whose selector matches
2. **Sorting by specificity** - Higher specificity wins
3. **Cascading** - Apply in origin order (user-agent < user < author)
4. **Inheriting** - Inherit inherited properties from parent
5. **Computing** - Resolve relative values to absolute values

### 4.2 Style Matching

```c
// Match a selector against an element
bool vw_css_selector_matches(vw_css_selector_t* selector,
                              vw_dom_element_t* element);

// Collect all matching rules for an element
typedef struct vw_css_match {
    vw_css_style_rule_t* rule;
    uint32_t specificity;
    uint32_t source_order;        // Position in stylesheet
    uint8_t origin;               // UA, user, author
} vw_css_match_t;

void vw_css_collect_matches(vw_dom_element_t* element,
                            vw_css_stylesheet_t** sheets,
                            size_t sheet_count,
                            vw_css_match_t** out_matches,
                            size_t* out_count);
```

### 4.3 Computed Styles

```c
// Complete computed style for an element
typedef struct vw_computed_style {
    // Display
    int display;                  // CSS_DISPLAY_BLOCK, _INLINE, _FLEX, _NONE, etc.
    int visibility;               // CSS_VISIBLE, CSS_HIDDEN, CSS_COLLAPSE

    // Position
    int position;                 // CSS_POSITION_STATIC, _RELATIVE, _ABSOLUTE, _FIXED
    float top, right, bottom, left;  // Computed lengths (px)
    int z_index;

    // Box model (all computed to px)
    float width, height;
    float min_width, min_height;
    float max_width, max_height;  // FLT_MAX if none

    struct { float top, right, bottom, left; } margin;
    struct { float top, right, bottom, left; } padding;
    struct {
        float width;
        uint32_t color;
        int style;                // CSS_BORDER_SOLID, _DASHED, etc.
    } border_top, border_right, border_bottom, border_left;
    float border_radius[4];       // TL, TR, BR, BL

    // Flex
    int flex_direction;           // CSS_FLEX_ROW, _COLUMN
    int flex_wrap;
    int justify_content;
    int align_items;
    int align_self;
    float flex_grow, flex_shrink;
    float flex_basis;

    // Colors
    uint32_t color;               // Foreground (ARGB)
    uint32_t background_color;
    float opacity;

    // Typography
    const char* font_family;
    float font_size;              // Computed px
    int font_weight;              // 100-900
    int font_style;               // CSS_FONT_NORMAL, _ITALIC
    float line_height;            // Computed px (or multiplier if unitless)

    // Text
    int text_align;               // CSS_TEXT_LEFT, _CENTER, _RIGHT, _JUSTIFY
    int text_decoration;          // Flags: UNDERLINE, OVERLINE, LINE_THROUGH
    int text_transform;           // CSS_TEXT_NONE, _UPPERCASE, _LOWERCASE, _CAPITALIZE
    int white_space;              // CSS_WS_NORMAL, _NOWRAP, _PRE, etc.
    int overflow_x, overflow_y;   // CSS_OVERFLOW_VISIBLE, _HIDDEN, _SCROLL, _AUTO

    // Cursor (for hit testing)
    int cursor;                   // CSS_CURSOR_DEFAULT, _POINTER, _TEXT, etc.
} vw_computed_style_t;
```

### 4.4 Style Resolution API

```c
// Create style resolver
typedef struct vw_style_resolver vw_style_resolver_t;

vw_style_resolver_t* vw_style_resolver_create(vw_dom_document_t* doc);
void vw_style_resolver_destroy(vw_style_resolver_t* resolver);

// Add stylesheet
void vw_style_resolver_add_sheet(vw_style_resolver_t* resolver,
                                  vw_css_stylesheet_t* sheet,
                                  uint8_t origin);  // UA, USER, AUTHOR

// Resolve styles for entire tree (builds render tree)
void vw_style_resolver_resolve_all(vw_style_resolver_t* resolver);

// Get computed style for element
const vw_computed_style_t* vw_style_get_computed(vw_dom_element_t* element);

// Invalidation (after DOM/style changes)
void vw_style_invalidate_subtree(vw_dom_element_t* element);
void vw_style_invalidate_all(vw_dom_document_t* doc);
```

### 4.5 Value Resolution

**Inheritance:**
- Inherited: color, font-*, line-height, text-*, visibility, cursor
- Non-inherited: display, position, margin, padding, border, width, height, etc.

**Relative Value Computation:**
```c
// Resolve percentage/em/rem to px
float vw_css_resolve_length(vw_css_value_t* value,
                            vw_computed_style_t* parent_style,
                            float containing_block_size,
                            float root_font_size);

// Resolve color (handles currentColor, inherit)
uint32_t vw_css_resolve_color(vw_css_value_t* value,
                               vw_computed_style_t* parent_style);
```

---

## 5. Phase 4: Layout Engine

### 5.1 Box Model

Every element generates zero or more boxes for layout.

```c
typedef enum {
    VW_BOX_BLOCK,                 // Block-level box
    VW_BOX_INLINE,                // Inline-level box
    VW_BOX_INLINE_BLOCK,          // Inline-level block container
    VW_BOX_FLEX,                  // Flex container
    VW_BOX_FLEX_ITEM,             // Flex item
    VW_BOX_TABLE,                 // Table boxes...
    VW_BOX_TABLE_ROW,
    VW_BOX_TABLE_CELL,
    VW_BOX_TEXT,                  // Text run (anonymous inline)
    VW_BOX_LINE,                  // Line box (inline formatting context)
    VW_BOX_ANONYMOUS_BLOCK,       // Generated block wrapper
} vw_box_type_t;

typedef struct vw_layout_box {
    vw_box_type_t type;

    // DOM linkage (NULL for anonymous boxes)
    vw_dom_element_t* element;
    const vw_computed_style_t* style;

    // Tree structure
    struct vw_layout_box* parent;
    struct vw_layout_box* first_child;
    struct vw_layout_box* last_child;
    struct vw_layout_box* prev_sibling;
    struct vw_layout_box* next_sibling;

    // Content dimensions (from layout)
    float content_width;
    float content_height;

    // Box dimensions (including padding, border)
    float box_width;              // content + padding + border
    float box_height;

    // Margin box
    float margin_top, margin_right, margin_bottom, margin_left;

    // Position relative to containing block
    float x, y;

    // Absolute position (document coordinates)
    float abs_x, abs_y;

    // For text boxes
    struct {
        const char* text;
        size_t length;
        float* glyph_positions;   // X offset of each glyph
        size_t glyph_count;
    } text;

    // For scrollable boxes
    struct {
        float scroll_x, scroll_y;
        float scroll_width, scroll_height;  // Content size
    } scroll;

    // Paint order (stacking context)
    int z_index;
    bool creates_stacking_context;
} vw_layout_box_t;
```

### 5.2 Layout Algorithm

**Block Formatting Context (BFC):**
```
1. For each child box:
   a. If block-level:
      - Start new line
      - Calculate width (containing block width - margins)
      - Layout children recursively
      - Stack vertically, collapse margins
   b. If inline-level:
      - Add to current line
      - When line full, start new line
      - Handle word wrap, text shaping
```

**Inline Formatting Context (IFC):**
```
1. Build line boxes
2. For each inline item:
   - Measure text/inline-block
   - Add to current line if fits
   - Otherwise, wrap to new line
3. Align line boxes (text-align)
4. Vertical align inline items (vertical-align)
```

**Flex Layout:**
```
1. Collect flex items
2. Determine main axis and cross axis
3. Resolve flex-basis for each item
4. Calculate free space
5. Distribute space per flex-grow/shrink
6. Position items per justify-content, align-items
7. Handle flex-wrap
```

### 5.3 Layout API

```c
typedef struct vw_layout_context {
    vw_dom_document_t* document;
    float viewport_width;
    float viewport_height;
    float root_font_size;         // For rem units (default 16px)

    // Memory arena for boxes
    vw_arena_t* arena;

    // Root box
    vw_layout_box_t* root_box;
} vw_layout_context_t;

// Create layout context
vw_layout_context_t* vw_layout_create(vw_dom_document_t* doc,
                                       float viewport_width,
                                       float viewport_height);
void vw_layout_destroy(vw_layout_context_t* ctx);

// Perform layout
void vw_layout_compute(vw_layout_context_t* ctx);

// Update viewport (triggers relayout)
void vw_layout_set_viewport(vw_layout_context_t* ctx,
                            float width,
                            float height);

// Get box at point (for hit testing)
vw_layout_box_t* vw_layout_hit_test(vw_layout_context_t* ctx,
                                     float x, float y);

// Get box for element
vw_layout_box_t* vw_layout_get_box(vw_layout_context_t* ctx,
                                    vw_dom_element_t* element);
```

### 5.4 Text Layout

```c
typedef struct vw_text_shaper vw_text_shaper_t;

// Create shaper with font
vw_text_shaper_t* vw_text_shaper_create(const char* font_family,
                                         float font_size,
                                         int font_weight,
                                         int font_style);
void vw_text_shaper_destroy(vw_text_shaper_t* shaper);

// Shape text (returns glyph positions)
typedef struct vw_shaped_text {
    float width;
    float height;
    float baseline;               // Distance from top to baseline

    // Glyph info (for cursor positioning, selection)
    struct {
        uint32_t codepoint;
        float x_advance;
        float x_offset;
        float y_offset;
    }* glyphs;
    size_t glyph_count;

    // Line break opportunities
    size_t* break_points;
    size_t break_count;
} vw_shaped_text_t;

vw_shaped_text_t* vw_text_shape(vw_text_shaper_t* shaper,
                                 const char* text,
                                 size_t length);

// Measure without full shaping
float vw_text_measure_width(vw_text_shaper_t* shaper,
                             const char* text,
                             size_t length);
```

---

## 6. Phase 5: Rendering/Painting

### 6.1 Paint Order

Following CSS stacking context rules:
```
1. Background and borders of root element
2. Descendants with negative z-index (in z-index order)
3. Block-level descendants in tree order (non-positioned)
4. Floating descendants
5. Inline-level descendants
6. Positioned descendants with z-index: auto or 0
7. Descendants with positive z-index (in z-index order)
```

### 6.2 Display List

Build a display list for efficient painting:

```c
typedef enum {
    VW_PAINT_SOLID_COLOR,         // Fill rectangle
    VW_PAINT_BORDER,              // Draw border
    VW_PAINT_TEXT,                // Draw text
    VW_PAINT_IMAGE,               // Draw image
    VW_PAINT_GRADIENT,            // Draw gradient
    VW_PAINT_SHADOW,              // Box shadow
    VW_PAINT_CLIP_PUSH,           // Push clip rect
    VW_PAINT_CLIP_POP,            // Pop clip rect
    VW_PAINT_OPACITY_PUSH,        // Push opacity layer
    VW_PAINT_OPACITY_POP,         // Pop opacity layer
} vw_paint_cmd_type_t;

typedef struct vw_paint_cmd {
    vw_paint_cmd_type_t type;

    union {
        struct {
            float x, y, width, height;
            uint32_t color;
            float border_radius[4];
        } solid;

        struct {
            float x, y, width, height;
            uint32_t color;
            float widths[4];      // top, right, bottom, left
            int styles[4];        // border style
            float radius[4];
        } border;

        struct {
            float x, y;
            const char* text;
            size_t length;
            const char* font_family;
            float font_size;
            int font_weight;
            uint32_t color;
        } text;

        struct {
            float x, y, width, height;
            void* image_data;     // Pixels handle
        } image;

        struct {
            float x, y, width, height;
        } clip;

        struct {
            float opacity;
        } opacity;
    };

    struct vw_paint_cmd* next;
} vw_paint_cmd_t;

typedef struct vw_display_list {
    vw_paint_cmd_t* first;
    vw_paint_cmd_t* last;
    size_t count;
    vw_arena_t* arena;
} vw_display_list_t;
```

### 6.3 Painter

```c
typedef struct vw_painter vw_painter_t;

// Create painter
vw_painter_t* vw_painter_create(vw_layout_context_t* layout);
void vw_painter_destroy(vw_painter_t* painter);

// Build display list from layout tree
vw_display_list_t* vw_painter_paint(vw_painter_t* painter);

// Execute display list on canvas
void vw_paint_execute(vw_display_list_t* list, void* canvas);

// Partial repaint (dirty rect)
void vw_paint_execute_rect(vw_display_list_t* list,
                            void* canvas,
                            float x, float y,
                            float width, float height);
```

### 6.4 Canvas Bridge

Interface to Viper.Graphics:

```c
// Low-level canvas operations
void vw_canvas_fill_rect(void* canvas,
                          float x, float y,
                          float width, float height,
                          uint32_t color);

void vw_canvas_fill_rounded_rect(void* canvas,
                                  float x, float y,
                                  float width, float height,
                                  float radius[4],
                                  uint32_t color);

void vw_canvas_draw_text(void* canvas,
                          float x, float y,
                          const char* text,
                          size_t length,
                          const char* font,
                          float size,
                          uint32_t color);

void vw_canvas_draw_image(void* canvas,
                           float x, float y,
                           float width, float height,
                           void* image);

void vw_canvas_push_clip(void* canvas,
                          float x, float y,
                          float width, float height);

void vw_canvas_pop_clip(void* canvas);

void vw_canvas_set_opacity(void* canvas, float opacity);
```

---

## 7. Phase 6: Interactivity

### 7.1 Event Handling

**Mouse Events:**
```c
typedef struct vw_web_event {
    enum {
        VW_EVENT_MOUSE_MOVE,
        VW_EVENT_MOUSE_DOWN,
        VW_EVENT_MOUSE_UP,
        VW_EVENT_CLICK,
        VW_EVENT_SCROLL,
        VW_EVENT_KEY_DOWN,
        VW_EVENT_KEY_UP,
        VW_EVENT_KEY_CHAR,
        VW_EVENT_FOCUS,
        VW_EVENT_BLUR,
    } type;

    float x, y;                   // Mouse position (document coords)
    int button;                   // Mouse button
    int modifiers;                // Shift, Ctrl, Alt
    float scroll_x, scroll_y;     // Scroll delta
    int key_code;                 // Virtual key code
    uint32_t char_code;           // Unicode character
} vw_web_event_t;

// Dispatch event to document
bool vw_dispatch_event(vw_dom_document_t* doc,
                        vw_layout_context_t* layout,
                        vw_web_event_t* event);
```

### 7.2 Hover/Active States

```c
// Track current element states
typedef struct vw_interaction_state {
    vw_dom_element_t* hovered;    // Element under mouse
    vw_dom_element_t* active;     // Element being clicked
    vw_dom_element_t* focused;    // Focused element

    // Selection
    vw_dom_node_t* selection_anchor;
    size_t selection_anchor_offset;
    vw_dom_node_t* selection_focus;
    size_t selection_focus_offset;
} vw_interaction_state_t;

// Update hover state (returns true if changed)
bool vw_update_hover(vw_interaction_state_t* state,
                     vw_layout_context_t* layout,
                     float x, float y);
```

### 7.3 Links and Navigation

```c
// Check if element is a link
bool vw_is_link(vw_dom_element_t* element);

// Get link URL
const char* vw_get_link_url(vw_dom_element_t* element);

// Navigation callback
typedef void (*vw_navigate_callback_t)(const char* url,
                                        const char* target,
                                        void* user_data);

void vw_set_navigate_callback(vw_dom_document_t* doc,
                               vw_navigate_callback_t callback,
                               void* user_data);
```

### 7.4 Scrolling

```c
// Scroll document
void vw_scroll_to(vw_layout_context_t* layout,
                   float x, float y);

void vw_scroll_by(vw_layout_context_t* layout,
                   float dx, float dy);

void vw_scroll_into_view(vw_layout_context_t* layout,
                          vw_dom_element_t* element);

// Get scroll position
float vw_get_scroll_x(vw_layout_context_t* layout);
float vw_get_scroll_y(vw_layout_context_t* layout);

// Get content size
float vw_get_scroll_width(vw_layout_context_t* layout);
float vw_get_scroll_height(vw_layout_context_t* layout);
```

---

## 8. Phase 7: GUI Integration

### 8.1 WebBrowser Widget

```c
// vg_webbrowser.h

typedef struct vg_webbrowser {
    vg_widget_t base;

    // Web engine state
    vw_dom_document_t* document;
    vw_style_resolver_t* style_resolver;
    vw_layout_context_t* layout;
    vw_painter_t* painter;
    vw_display_list_t* display_list;
    vw_interaction_state_t interaction;

    // Navigation
    char* current_url;
    char** history;
    int history_index;
    int history_count;

    // Loading state
    bool is_loading;
    float load_progress;

    // Scroll position
    float scroll_x, scroll_y;

    // Callbacks
    void (*on_navigate)(vg_widget_t* w, const char* url, void* data);
    void (*on_title_change)(vg_widget_t* w, const char* title, void* data);
    void (*on_load_start)(vg_widget_t* w, void* data);
    void (*on_load_finish)(vg_widget_t* w, void* data);
    void* callback_data;
} vg_webbrowser_t;
```

### 8.2 Widget Implementation

```c
// Create WebBrowser widget
vg_widget_t* vg_webbrowser_create(vg_widget_t* parent);

// Navigation
void vg_webbrowser_load_url(vg_webbrowser_t* browser, const char* url);
void vg_webbrowser_load_html(vg_webbrowser_t* browser, const char* html);
void vg_webbrowser_go_back(vg_webbrowser_t* browser);
void vg_webbrowser_go_forward(vg_webbrowser_t* browser);
void vg_webbrowser_reload(vg_webbrowser_t* browser);
void vg_webbrowser_stop(vg_webbrowser_t* browser);

// Query
const char* vg_webbrowser_get_url(vg_webbrowser_t* browser);
const char* vg_webbrowser_get_title(vg_webbrowser_t* browser);
bool vg_webbrowser_can_go_back(vg_webbrowser_t* browser);
bool vg_webbrowser_can_go_forward(vg_webbrowser_t* browser);

// Vtable implementation
static const vg_widget_vtable_t webbrowser_vtable = {
    .destroy = webbrowser_destroy,
    .measure = webbrowser_measure,
    .arrange = webbrowser_arrange,
    .paint = webbrowser_paint,
    .handle_event = webbrowser_handle_event,
    .can_focus = webbrowser_can_focus,
    .on_focus = webbrowser_on_focus,
};
```

### 8.3 Runtime Bindings

```c
// rt_web.h - Runtime bindings for Zia

// WebBrowser Widget
void* rt_webbrowser_new(void* parent);

// Navigation
void rt_webbrowser_load_url(void* browser, rt_string url);
void rt_webbrowser_load_html(void* browser, rt_string html);
void rt_webbrowser_go_back(void* browser);
void rt_webbrowser_go_forward(void* browser);
void rt_webbrowser_reload(void* browser);
void rt_webbrowser_stop(void* browser);

// Query state
rt_string rt_webbrowser_get_url(void* browser);
rt_string rt_webbrowser_get_title(void* browser);
int64_t rt_webbrowser_is_loading(void* browser);
double rt_webbrowser_get_load_progress(void* browser);
int64_t rt_webbrowser_can_go_back(void* browser);
int64_t rt_webbrowser_can_go_forward(void* browser);

// Scroll
void rt_webbrowser_scroll_to(void* browser, double x, double y);
void rt_webbrowser_scroll_by(void* browser, double dx, double dy);
double rt_webbrowser_get_scroll_x(void* browser);
double rt_webbrowser_get_scroll_y(void* browser);

// DOM access (for advanced use)
void* rt_webbrowser_get_document(void* browser);
rt_string rt_webbrowser_get_element_text(void* browser, rt_string selector);
void rt_webbrowser_set_element_text(void* browser, rt_string selector, rt_string text);
```

### 8.4 Zia API Example

```zia
module WebDemo;

import Viper.GUI.*;

var app: App = nil;
var browser: Widget = nil;
var urlBar: Widget = nil;
var backBtn: Widget = nil;
var fwdBtn: Widget = nil;

func main() {
    app = App.New("Viper Browser", 1024, 768);
    let root = App.GetRoot(app);

    // Toolbar
    let toolbar = Container.New(root);
    Widget.SetLayout(toolbar, Layout.HBox);
    Widget.SetSize(toolbar, -1, 40);  // -1 = fill width

    backBtn = Button.New(toolbar, "<");
    Widget.SetSize(backBtn, 40, 30);

    fwdBtn = Button.New(toolbar, ">");
    Widget.SetSize(fwdBtn, 40, 30);

    let reloadBtn = Button.New(toolbar, "R");
    Widget.SetSize(reloadBtn, 40, 30);

    urlBar = TextInput.New(toolbar);
    Widget.SetSize(urlBar, -1, 30);  // Fill remaining
    TextInput.SetText(urlBar, "https://example.com");

    let goBtn = Button.New(toolbar, "Go");
    Widget.SetSize(goBtn, 60, 30);

    // Browser
    browser = WebBrowser.New(root);
    Widget.SetSize(browser, -1, -1);  // Fill remaining space

    // Load initial page
    WebBrowser.LoadURL(browser, "https://example.com");

    // Event loop
    while App.IsRunning(app) {
        App.PollEvents(app);

        if Button.WasClicked(backBtn) != 0 {
            WebBrowser.GoBack(browser);
        }
        if Button.WasClicked(fwdBtn) != 0 {
            WebBrowser.GoForward(browser);
        }
        if Button.WasClicked(reloadBtn) != 0 {
            WebBrowser.Reload(browser);
        }
        if Button.WasClicked(goBtn) != 0 or TextInput.WasSubmitted(urlBar) != 0 {
            let url = TextInput.GetText(urlBar);
            WebBrowser.LoadURL(browser, url);
        }

        // Update navigation buttons
        Button.SetEnabled(backBtn, WebBrowser.CanGoBack(browser));
        Button.SetEnabled(fwdBtn, WebBrowser.CanGoForward(browser));

        // Update URL bar when navigation occurs
        let currentUrl = WebBrowser.GetURL(browser);
        if currentUrl != TextInput.GetText(urlBar) {
            TextInput.SetText(urlBar, currentUrl);
        }

        App.Render(app);
    }

    App.Destroy(app);
}
```

---

## 9. Phase 8: Advanced Features

### 9.1 Future Phases

**Phase 8a: Network Layer**
- HTTP/HTTPS client (use libcurl or custom)
- Resource loading (scripts, styles, images)
- Cache management
- Cookie handling

**Phase 8b: Images**
- PNG, JPEG, GIF, WebP decoding
- SVG parsing and rendering
- Image caching
- Lazy loading

**Phase 8c: Forms**
- Form element state management
- Form submission (GET/POST)
- Input validation
- File inputs

**Phase 8d: JavaScript Engine**
- Lexer/parser for ECMAScript subset
- Bytecode compiler
- VM execution
- DOM bindings
- Event handlers

**Phase 8e: Advanced CSS**
- Transitions and animations
- Grid layout
- Transforms (2D/3D)
- Filters and blend modes
- CSS variables

**Phase 8f: Developer Tools**
- DOM inspector
- Style inspector
- Console
- Network monitor

---

## 10. Data Structures

### 10.1 Memory Management

```c
// Arena allocator for DOM/Layout nodes
typedef struct vw_arena {
    char* base;
    size_t size;
    size_t used;
    struct vw_arena* next;        // Chain for growth
} vw_arena_t;

vw_arena_t* vw_arena_create(size_t initial_size);
void* vw_arena_alloc(vw_arena_t* arena, size_t size);
void vw_arena_reset(vw_arena_t* arena);
void vw_arena_destroy(vw_arena_t* arena);
```

### 10.2 String Interning

```c
// Intern strings for fast comparison
typedef struct vw_string_interner vw_string_interner_t;

vw_string_interner_t* vw_interner_create(void);
void vw_interner_destroy(vw_string_interner_t* interner);

// Intern a string (returns pointer valid for interner lifetime)
const char* vw_intern(vw_string_interner_t* interner,
                       const char* str,
                       size_t len);

// Check if two interned strings are equal (pointer comparison)
static inline bool vw_interned_eq(const char* a, const char* b) {
    return a == b;
}
```

### 10.3 Hash Maps

```c
// Generic hash map
typedef struct vw_hash_map vw_hash_map_t;

vw_hash_map_t* vw_hash_map_create(size_t initial_capacity);
void vw_hash_map_destroy(vw_hash_map_t* map);

void vw_hash_map_set(vw_hash_map_t* map, const char* key, void* value);
void* vw_hash_map_get(vw_hash_map_t* map, const char* key);
bool vw_hash_map_has(vw_hash_map_t* map, const char* key);
void vw_hash_map_remove(vw_hash_map_t* map, const char* key);

// String-to-string map for attributes
typedef struct vw_string_map vw_string_map_t;
```

---

## 11. File Organization

```
src/lib/web/                          # libviperweb - Web Engine Library
├── CMakeLists.txt
├── include/
│   ├── viperweb.h                    # Main public header
│   ├── vw_html.h                     # HTML parser API
│   ├── vw_css.h                      # CSS parser API
│   ├── vw_dom.h                      # DOM API
│   ├── vw_style.h                    # Style resolution API
│   ├── vw_layout.h                   # Layout engine API
│   ├── vw_paint.h                    # Painting API
│   ├── vw_event.h                    # Event handling
│   └── vw_types.h                    # Common types
│
├── src/
│   ├── html/
│   │   ├── lexer.c                   # HTML tokenizer
│   │   ├── parser.c                  # HTML parser
│   │   ├── entities.c                # HTML entity decoding
│   │   └── tree_builder.c            # DOM tree construction
│   │
│   ├── css/
│   │   ├── lexer.c                   # CSS tokenizer
│   │   ├── parser.c                  # CSS parser
│   │   ├── selector.c                # Selector parsing & matching
│   │   ├── value.c                   # CSS value parsing
│   │   └── properties.c              # Property definitions
│   │
│   ├── dom/
│   │   ├── document.c                # Document implementation
│   │   ├── element.c                 # Element implementation
│   │   ├── text.c                    # Text node implementation
│   │   └── query.c                   # querySelector implementation
│   │
│   ├── style/
│   │   ├── resolver.c                # Style resolution
│   │   ├── cascade.c                 # Cascade algorithm
│   │   ├── computed.c                # Computed value calculation
│   │   └── inherited.c               # Inheritance handling
│   │
│   ├── layout/
│   │   ├── box.c                     # Box model
│   │   ├── block.c                   # Block formatting context
│   │   ├── inline.c                  # Inline formatting context
│   │   ├── flex.c                    # Flexbox layout
│   │   ├── text.c                    # Text layout
│   │   └── table.c                   # Table layout
│   │
│   ├── paint/
│   │   ├── painter.c                 # Display list builder
│   │   ├── display_list.c            # Display list implementation
│   │   ├── background.c              # Background painting
│   │   ├── border.c                  # Border painting
│   │   └── text.c                    # Text painting
│   │
│   ├── event/
│   │   ├── dispatch.c                # Event dispatching
│   │   ├── mouse.c                   # Mouse event handling
│   │   └── keyboard.c                # Keyboard event handling
│   │
│   └── util/
│       ├── arena.c                   # Arena allocator
│       ├── interner.c                # String interning
│       ├── hash_map.c                # Hash map
│       └── color.c                   # Color parsing
│
└── tests/
    ├── test_html_lexer.c
    ├── test_html_parser.c
    ├── test_css_lexer.c
    ├── test_css_parser.c
    ├── test_css_selector.c
    ├── test_dom.c
    ├── test_style.c
    ├── test_layout.c
    └── test_paint.c

src/lib/gui/
├── include/
│   └── vg_webbrowser.h               # WebBrowser widget header
└── src/
    └── vg_webbrowser.c               # WebBrowser widget implementation

src/runtime/
├── rt_web.h                          # Zia runtime bindings header
└── rt_web.c                          # Zia runtime bindings implementation

demos/zia/webbrowser_demo/
├── main.zia                          # Demo application
└── README.md
```

---

## 12. Testing Strategy

### 12.1 Unit Tests

**HTML Lexer Tests:**
- Tokenize basic tags: `<div>`, `</div>`, `<br/>`
- Attributes: `<a href="url" class="cls">`
- Entities: `&amp;`, `&#65;`, `&#x41;`
- Comments: `<!-- comment -->`
- Edge cases: malformed HTML, unclosed tags

**HTML Parser Tests:**
- Parse simple documents
- Implicit tag closing (e.g., `<p>` before another `<p>`)
- Nested structures
- Parse fragments

**CSS Lexer Tests:**
- Tokenize selectors: `.class`, `#id`, `div`
- Properties and values
- Units: `px`, `em`, `%`
- Colors: `#fff`, `rgb(...)`, named colors

**CSS Parser Tests:**
- Parse rule sets
- Selector specificity calculation
- @media rules
- Shorthand expansion

**DOM Tests:**
- Tree manipulation
- Attribute handling
- Query selectors

**Style Tests:**
- Selector matching
- Cascade ordering
- Inheritance
- Computed value resolution

**Layout Tests:**
- Block layout
- Inline layout
- Flexbox
- Margin collapsing

### 12.2 Integration Tests

**Render Tests:**
- Render HTML documents to images
- Compare with reference images (golden tests)
- Test responsive layouts at different sizes

**Event Tests:**
- Click handling
- Hover states
- Scroll behavior

### 12.3 Conformance Tests

Use subsets of web-platform-tests:
- HTML parsing tests
- CSS selector tests
- Layout tests (CSS WG test suite)

---

## 13. Milestones & Timeline

### Milestone 1: HTML Parsing (2-3 weeks)
- [ ] HTML lexer with all token types
- [ ] HTML parser with tree building
- [ ] DOM API implementation
- [ ] Unit tests passing

### Milestone 2: CSS Parsing (2-3 weeks)
- [ ] CSS lexer
- [ ] CSS parser (rules, selectors, values)
- [ ] Selector specificity
- [ ] Unit tests passing

### Milestone 3: Style Resolution (2 weeks)
- [ ] Selector matching
- [ ] Cascade algorithm
- [ ] Computed style calculation
- [ ] Inheritance

### Milestone 4: Basic Layout (3-4 weeks)
- [ ] Block formatting context
- [ ] Box model (margin, padding, border)
- [ ] Simple inline layout (text)
- [ ] Width/height calculation

### Milestone 5: Rendering (2-3 weeks)
- [ ] Display list generation
- [ ] Background/border painting
- [ ] Text rendering
- [ ] Integration with Viper.Graphics

### Milestone 6: GUI Integration (2 weeks)
- [ ] WebBrowser widget
- [ ] Runtime bindings
- [ ] Demo application
- [ ] Documentation

### Milestone 7: Interactivity (2-3 weeks)
- [ ] Mouse events
- [ ] Link clicking
- [ ] Hover states
- [ ] Scrolling

### Milestone 8: Advanced Layout (3-4 weeks)
- [ ] Flexbox layout
- [ ] Proper inline layout with wrapping
- [ ] Overflow handling
- [ ] Positioned elements

### Total Estimated Time: 18-24 weeks

---

## 14. References

### Specifications
- [CSS 2.2 Specification](https://www.w3.org/TR/CSS22/)
- [CSS Flexbox](https://www.w3.org/TR/css-flexbox-1/)
- [CSS Selectors Level 4](https://www.w3.org/TR/selectors-4/)
- [DOM Living Standard](https://dom.spec.whatwg.org/)
- [HTML Living Standard](https://html.spec.whatwg.org/)

### Existing Implementations (for reference)
- [Blink](https://www.chromium.org/blink/) - Chrome's engine
- [litehtml](https://github.com/litehtml/litehtml) - Lightweight C++ HTML engine
- [Robinson](https://limpet.net/mbrubeck/2014/08/08/toy-layout-engine-1.html) - Toy browser tutorial
- [Servo](https://github.com/servo/servo) - Rust browser engine
- [WebKit](https://webkit.org/) - Safari's engine

### Books
- "Web Browser Engineering" by Pavel Panchekha & Chris Harrelson
- "Let's Build a Browser Engine" series by Matt Brubeck

---

## Appendix A: User-Agent Stylesheet

Default styles for HTML elements:

```css
/* Block elements */
html, body, div, section, article, aside, nav, header, footer, main,
h1, h2, h3, h4, h5, h6, p, ul, ol, li, dl, dt, dd,
table, thead, tbody, tfoot, tr, form, fieldset, blockquote, pre,
figure, figcaption, address {
    display: block;
}

/* Headings */
h1 { font-size: 2em; margin: 0.67em 0; font-weight: bold; }
h2 { font-size: 1.5em; margin: 0.83em 0; font-weight: bold; }
h3 { font-size: 1.17em; margin: 1em 0; font-weight: bold; }
h4 { font-size: 1em; margin: 1.33em 0; font-weight: bold; }
h5 { font-size: 0.83em; margin: 1.67em 0; font-weight: bold; }
h6 { font-size: 0.67em; margin: 2.33em 0; font-weight: bold; }

/* Paragraphs */
p { margin: 1em 0; }

/* Lists */
ul, ol { padding-left: 40px; margin: 1em 0; }
ul { list-style-type: disc; }
ol { list-style-type: decimal; }
li { display: list-item; }

/* Links */
a { color: #0000EE; text-decoration: underline; cursor: pointer; }
a:visited { color: #551A8B; }
a:hover { text-decoration: underline; }
a:active { color: #FF0000; }

/* Inline elements */
strong, b { font-weight: bold; }
em, i { font-style: italic; }
u { text-decoration: underline; }
s, strike { text-decoration: line-through; }
code, kbd, samp { font-family: monospace; }
sub { vertical-align: sub; font-size: smaller; }
sup { vertical-align: super; font-size: smaller; }

/* Preformatted */
pre { white-space: pre; font-family: monospace; margin: 1em 0; }

/* Tables */
table { border-collapse: separate; border-spacing: 2px; }
th, td { padding: 1px; }
th { font-weight: bold; text-align: center; }

/* Forms */
input, button, select, textarea {
    font-family: inherit;
    font-size: inherit;
}
button, input[type="button"], input[type="submit"] {
    cursor: pointer;
}

/* Hidden */
[hidden], template, script, style, head, title, meta, link {
    display: none;
}

/* Body defaults */
body {
    margin: 8px;
    font-family: sans-serif;
    font-size: 16px;
    line-height: 1.2;
    color: #000000;
    background-color: #FFFFFF;
}
```

---

## Appendix B: CSS Property Support Matrix

| Property | Phase | Notes |
|----------|-------|-------|
| display | 2 | block, inline, none, flex |
| position | 4 | static, relative, absolute, fixed |
| width, height | 2 | px, %, auto |
| margin | 2 | px, %, auto |
| padding | 2 | px, % |
| border | 2 | width, style, color |
| color | 2 | hex, rgb, named |
| background-color | 2 | hex, rgb, named |
| font-family | 2 | named fonts |
| font-size | 2 | px, em, rem, % |
| font-weight | 2 | normal, bold, 100-900 |
| font-style | 2 | normal, italic |
| line-height | 2 | px, unitless |
| text-align | 2 | left, center, right |
| text-decoration | 2 | none, underline |
| white-space | 4 | normal, nowrap, pre |
| overflow | 4 | visible, hidden, scroll |
| flex-* | 8 | Full flexbox |
| opacity | 5 | 0-1 |
| z-index | 5 | integers |
| cursor | 6 | default, pointer |
| visibility | 2 | visible, hidden |

---

*Document Version: 1.0*
*Last Updated: 2026-01-20*
