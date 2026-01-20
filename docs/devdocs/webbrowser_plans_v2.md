# Viper Web Engine & Browser Control Implementation Plan

**Status**: Draft v2.0
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
5. Reusable as the basis for a ViperDOS web browser

**Non-Goals (Initially):**
- JavaScript execution (Phase 10+)
- Full HTML5/CSS3 compliance (progressive enhancement)
- Media playback (audio/video)
- WebGL/Canvas 2D API

**Design Principles:**
- Zero external dependencies (except C standard library)
- Arena-based memory management for predictable performance
- Incremental layout/paint for interactive responsiveness
- Clean separation between parsing, layout, and rendering
- Testable components with clear interfaces

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [Phase 1: Foundation & Encoding](#2-phase-1-foundation--encoding)
3. [Phase 2: HTML Parsing & DOM](#3-phase-2-html-parsing--dom)
4. [Phase 3: CSS Parsing & CSSOM](#4-phase-3-css-parsing--cssom)
5. [Phase 4: Style Resolution](#5-phase-4-style-resolution)
6. [Phase 5: Block Layout](#6-phase-5-block-layout)
7. [Phase 6: Inline Layout & Text](#7-phase-6-inline-layout--text)
8. [Phase 7: Rendering/Painting](#8-phase-7-renderingpainting)
9. [Phase 8: Resource Loading](#9-phase-8-resource-loading)
10. [Phase 9: Interactivity](#10-phase-9-interactivity)
11. [Phase 10: GUI Integration](#11-phase-10-gui-integration)
12. [Phase 11: Advanced Layout](#12-phase-11-advanced-layout)
13. [Phase 12: Advanced Features](#13-phase-12-advanced-features)
14. [Data Structures](#14-data-structures)
15. [File Organization](#15-file-organization)
16. [Testing Strategy](#16-testing-strategy)
17. [Milestones & Timeline](#17-milestones--timeline)
18. [References](#18-references)

---

## 1. Architecture Overview

### 1.1 High-Level Pipeline

```
                    libviperweb Architecture
┌─────────────────────────────────────────────────────────────────────────┐
│                                                                         │
│   Raw Bytes ──► Encoding Detection ──► Unicode Text                     │
│                                             │                           │
│                         ┌───────────────────┴───────────────────┐       │
│                         ▼                                       ▼       │
│                   ┌──────────┐                            ┌──────────┐  │
│                   │   HTML   │                            │   CSS    │  │
│                   │  Lexer   │                            │  Lexer   │  │
│                   └────┬─────┘                            └────┬─────┘  │
│                        ▼                                       ▼        │
│                   ┌──────────┐                            ┌──────────┐  │
│                   │   HTML   │                            │   CSS    │  │
│                   │  Parser  │                            │  Parser  │  │
│                   └────┬─────┘                            └────┬─────┘  │
│                        ▼                                       ▼        │
│                   ┌──────────┐         Resource           ┌──────────┐  │
│                   │   DOM    │◄────── Loading ──────────► │  CSSOM   │  │
│                   │   Tree   │         Manager            │ (Sheets) │  │
│                   └────┬─────┘                            └────┬─────┘  │
│                        │                                       │        │
│                        └───────────────┬───────────────────────┘        │
│                                        ▼                                │
│                                 ┌────────────┐                          │
│                                 │   Style    │                          │
│                                 │ Resolution │                          │
│                                 └─────┬──────┘                          │
│                                       ▼                                 │
│                                 ┌────────────┐                          │
│                                 │   Render   │ (Styled Boxes)           │
│                                 │    Tree    │                          │
│                                 └─────┬──────┘                          │
│                                       ▼                                 │
│   User Events ──────────────►  ┌────────────┐                          │
│   (mouse, keyboard, scroll)    │   Layout   │ ◄── Dirty Tracking       │
│                                │  (Boxing)  │                          │
│                                └─────┬──────┘                          │
│                                      ▼                                  │
│                                ┌────────────┐                          │
│                                │   Paint    │ ◄── Dirty Rects          │
│                                │  (Render)  │                          │
│                                └─────┬──────┘                          │
│                                      ▼                                  │
│                                ┌────────────┐                          │
│                                │  Display   │                          │
│                                │   List     │                          │
│                                └─────┬──────┘                          │
│                                      ▼                                  │
│                                ┌────────────┐                          │
│                                │   Canvas   │ (Viper.Graphics)         │
│                                │   Output   │                          │
│                                └────────────┘                          │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 1.2 Layer Separation

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
│  ┌──────────────┬──────────────┬──────────────┬─────────────┐ │
│  │   Parsing    │    Style     │    Layout    │   Paint     │ │
│  │  HTML/CSS    │  Resolution  │   Engine     │   System    │ │
│  └──────────────┴──────────────┴──────────────┴─────────────┘ │
│  ┌──────────────┬──────────────┬──────────────┬─────────────┐ │
│  │  Encoding    │   Resource   │    Text      │   Events    │ │
│  │  Detection   │   Loader     │   Shaping    │   Dispatch  │ │
│  └──────────────┴──────────────┴──────────────┴─────────────┘ │
├────────────────────────────────────────────────────────────────┤
│                     Viper.Graphics (Canvas)                    │
│  (Drawing primitives: boxes, text, images)                     │
├────────────────────────────────────────────────────────────────┤
│                     Viper.Net (HTTP Client)                    │
│  (HTTP/HTTPS requests, already implemented)                    │
└────────────────────────────────────────────────────────────────┘
```

### 1.3 Document Lifecycle

Understanding document states is critical for correct rendering:

```
┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐
│   LOADING   │───►│   PARSING   │───►│ INTERACTIVE │───►│  COMPLETE   │
└─────────────┘    └─────────────┘    └─────────────┘    └─────────────┘
       │                  │                  │                  │
   Fetching          Building DOM       Blocking CSS       All resources
   HTML bytes        & discovering      loaded, can        loaded, layout
                     resources          layout/paint       stable
```

```c
typedef enum {
    VW_DOC_STATE_UNINITIALIZED,   // Not yet started
    VW_DOC_STATE_LOADING,         // Fetching HTML
    VW_DOC_STATE_PARSING,         // Building DOM
    VW_DOC_STATE_INTERACTIVE,     // DOM ready, may still load resources
    VW_DOC_STATE_COMPLETE,        // Fully loaded
    VW_DOC_STATE_ERROR,           // Fatal error occurred
} vw_doc_state_t;
```

### 1.4 Document Mode (Quirks)

Real-world HTML requires quirks mode support:

```c
typedef enum {
    VW_DOC_MODE_QUIRKS,           // Pre-HTML5 quirky behavior
    VW_DOC_MODE_LIMITED_QUIRKS,   // Almost standards (affects table cell height)
    VW_DOC_MODE_STANDARDS,        // Full standards mode
} vw_doc_mode_t;
```

Mode is determined by DOCTYPE:
- `<!DOCTYPE html>` → Standards
- `<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01//EN" "...strict.dtd">` → Standards
- `<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN">` → Limited Quirks
- No DOCTYPE or unknown → Quirks

Quirks mode affects:
- Box sizing (width includes padding/border in quirks)
- Table cell vertical alignment
- Font size in tables
- Margin collapsing behavior

---

## 2. Phase 1: Foundation & Encoding

### 2.1 Character Encoding Detection

Before any parsing, raw bytes must be converted to Unicode text. The HTML5 encoding sniffing algorithm:

```c
typedef enum {
    VW_ENCODING_UNKNOWN,
    VW_ENCODING_UTF8,
    VW_ENCODING_UTF16_LE,
    VW_ENCODING_UTF16_BE,
    VW_ENCODING_ISO_8859_1,       // Latin-1 (very common legacy)
    VW_ENCODING_WINDOWS_1252,     // Superset of Latin-1 (most common legacy)
    VW_ENCODING_ASCII,
    // Add others as needed
} vw_encoding_t;

typedef struct vw_encoding_detector vw_encoding_detector_t;

// Create detector
vw_encoding_detector_t* vw_encoding_detector_create(void);
void vw_encoding_detector_destroy(vw_encoding_detector_t* det);

// Detection sources (in priority order)
void vw_encoding_set_from_http_header(vw_encoding_detector_t* det,
                                       const char* content_type);
void vw_encoding_set_from_bom(vw_encoding_detector_t* det,
                               const uint8_t* data,
                               size_t len);
void vw_encoding_set_from_meta(vw_encoding_detector_t* det,
                                const uint8_t* data,
                                size_t len);  // Pre-scan for <meta charset>

// Get final encoding
vw_encoding_t vw_encoding_detect(vw_encoding_detector_t* det,
                                  const uint8_t* data,
                                  size_t len);
```

**Encoding Detection Algorithm:**
```
1. Check for BOM (Byte Order Mark):
   - EF BB BF → UTF-8
   - FF FE → UTF-16 LE
   - FE FF → UTF-16 BE

2. Check HTTP Content-Type header charset parameter

3. Pre-scan first 1024 bytes for:
   - <meta charset="...">
   - <meta http-equiv="Content-Type" content="...; charset=...">

4. Use encoding from step 1-3 if found

5. Default to Windows-1252 (for maximum compatibility)
   (HTML5 spec says UTF-8, but real-world legacy...)
```

### 2.2 Encoding Conversion

```c
typedef struct vw_text_decoder vw_text_decoder_t;

// Create decoder for specific encoding
vw_text_decoder_t* vw_text_decoder_create(vw_encoding_t encoding);
void vw_text_decoder_destroy(vw_text_decoder_t* dec);

// Decode bytes to UTF-8
size_t vw_text_decode(vw_text_decoder_t* dec,
                       const uint8_t* input,
                       size_t input_len,
                       char* output,
                       size_t output_capacity,
                       size_t* bytes_consumed);

// Check for decoding errors
bool vw_text_decoder_had_errors(vw_text_decoder_t* dec);
```

### 2.3 URL Handling

```c
typedef struct vw_url {
    char* scheme;        // "https"
    char* username;      // Optional
    char* password;      // Optional
    char* host;          // "example.com"
    uint16_t port;       // 443 (0 = default for scheme)
    char* path;          // "/page/file.html"
    char* query;         // "?foo=bar" (without ?)
    char* fragment;      // "#section" (without #)
    char* href;          // Full reconstructed URL
} vw_url_t;

// Parse URL
vw_url_t* vw_url_parse(const char* url_string);
void vw_url_destroy(vw_url_t* url);

// Resolve relative URL against base
vw_url_t* vw_url_resolve(const vw_url_t* base, const char* relative);

// Serialize back to string
char* vw_url_to_string(const vw_url_t* url);

// Check if URL is same-origin
bool vw_url_same_origin(const vw_url_t* a, const vw_url_t* b);
```

**URL Resolution Cases:**
```
Base: https://example.com/path/page.html

Relative URL          → Resolved URL
---------------------------------------------------------
"other.html"          → https://example.com/path/other.html
"../other.html"       → https://example.com/other.html
"/absolute.html"      → https://example.com/absolute.html
"//other.com/page"    → https://other.com/page
"https://foo.com/"    → https://foo.com/
"#anchor"             → https://example.com/path/page.html#anchor
"?query"              → https://example.com/path/page.html?query
""                    → https://example.com/path/page.html
```

### 2.4 Memory Arenas

Foundation data structure for all allocations:

```c
typedef struct vw_arena {
    uint8_t* base;
    size_t capacity;
    size_t used;
    struct vw_arena* next;    // Chain for growth
} vw_arena_t;

vw_arena_t* vw_arena_create(size_t initial_capacity);
void vw_arena_destroy(vw_arena_t* arena);
void* vw_arena_alloc(vw_arena_t* arena, size_t size);
void* vw_arena_alloc_zero(vw_arena_t* arena, size_t size);
char* vw_arena_strdup(vw_arena_t* arena, const char* str);
void vw_arena_reset(vw_arena_t* arena);
```

### 2.5 String Interning

Essential for fast tag/attribute name comparison:

```c
typedef struct vw_string_interner vw_string_interner_t;

vw_string_interner_t* vw_interner_create(void);
void vw_interner_destroy(vw_string_interner_t* interner);

// Intern a string (returns pointer valid for interner lifetime)
const char* vw_intern(vw_string_interner_t* interner,
                       const char* str,
                       size_t len);

// Case-insensitive intern (for HTML tag/attribute names)
const char* vw_intern_lower(vw_string_interner_t* interner,
                             const char* str,
                             size_t len);

// Fast equality check for interned strings
static inline bool vw_interned_eq(const char* a, const char* b) {
    return a == b;
}
```

---

## 3. Phase 2: HTML Parsing & DOM

### 3.1 HTML Lexer (Tokenizer)

```c
typedef enum {
    HTML_TOKEN_DOCTYPE,
    HTML_TOKEN_START_TAG,
    HTML_TOKEN_END_TAG,
    HTML_TOKEN_SELF_CLOSING_TAG,
    HTML_TOKEN_TEXT,
    HTML_TOKEN_COMMENT,
    HTML_TOKEN_CDATA,
    HTML_TOKEN_EOF,
    HTML_TOKEN_ERROR,
} vw_html_token_type_t;

typedef struct vw_html_attr {
    const char* name;             // Interned
    size_t name_len;
    const char* value;
    size_t value_len;
    struct vw_html_attr* next;
} vw_html_attr_t;

typedef struct {
    vw_html_token_type_t type;
    const char* start;
    size_t length;

    // For DOCTYPE:
    const char* doctype_name;
    const char* doctype_public;
    const char* doctype_system;
    bool doctype_force_quirks;

    // For tags:
    const char* tag_name;         // Interned, lowercase
    size_t tag_name_len;
    vw_html_attr_t* attributes;
    size_t attr_count;
    bool self_closing;

    // Location
    size_t line;
    size_t column;
} vw_html_token_t;
```

### 3.2 HTML Parser

```c
typedef struct vw_html_parser vw_html_parser_t;

vw_html_parser_t* vw_html_parser_create(vw_html_parse_options_t* options);
void vw_html_parser_destroy(vw_html_parser_t* parser);

// Parse complete HTML string
vw_dom_document_t* vw_html_parser_parse(vw_html_parser_t* parser,
                                         const char* html,
                                         size_t len);

// Parse fragment (for innerHTML)
vw_dom_node_t* vw_html_parser_parse_fragment(vw_html_parser_t* parser,
                                              const char* html,
                                              size_t len,
                                              vw_dom_element_t* context);
```

### 3.3 DOM Tree

```c
typedef enum {
    VW_DOM_NODE_DOCUMENT,
    VW_DOM_NODE_DOCTYPE,
    VW_DOM_NODE_ELEMENT,
    VW_DOM_NODE_TEXT,
    VW_DOM_NODE_COMMENT,
    VW_DOM_NODE_CDATA,
    VW_DOM_NODE_FRAGMENT,
} vw_dom_node_type_t;

typedef struct vw_dom_node {
    vw_dom_node_type_t type;
    struct vw_dom_node* parent;
    struct vw_dom_node* first_child;
    struct vw_dom_node* last_child;
    struct vw_dom_node* prev_sibling;
    struct vw_dom_node* next_sibling;
    struct vw_dom_document* owner_document;
    struct vw_layout_box* layout_box;
    struct vw_computed_style* computed;
    uint32_t dirty_flags;
} vw_dom_node_t;

// Dirty flags
#define VW_DIRTY_STYLE      (1 << 0)
#define VW_DIRTY_LAYOUT     (1 << 1)
#define VW_DIRTY_PAINT      (1 << 2)
#define VW_DIRTY_CHILDREN   (1 << 3)

typedef struct vw_dom_element {
    vw_dom_node_t base;
    const char* tag_name;         // Interned
    const char* namespace_uri;
    vw_string_map_t* attributes;
    const char* id;               // Cached
    const char** class_list;
    size_t class_count;
    struct vw_css_declaration_block* inline_style;
    uint32_t state_flags;         // hover, active, focus
} vw_dom_element_t;

typedef struct vw_dom_text {
    vw_dom_node_t base;
    char* data;
    size_t length;
} vw_dom_text_t;

typedef struct vw_dom_document {
    vw_dom_node_t base;
    vw_doc_state_t state;
    vw_doc_mode_t mode;
    char* title;
    vw_url_t* url;
    vw_url_t* base_url;
    vw_dom_element_t* document_element;
    vw_dom_element_t* head;
    vw_dom_element_t* body;
    struct vw_css_stylesheet** stylesheets;
    size_t stylesheet_count;
    vw_string_interner_t* interner;
    vw_arena_t* arena;
    vw_hash_map_t* id_map;
} vw_dom_document_t;
```

---

## 4. Phase 3: CSS Parsing & CSSOM

### 4.1 CSS Lexer

```c
typedef enum {
    CSS_TOKEN_IDENT,
    CSS_TOKEN_FUNCTION,
    CSS_TOKEN_AT_KEYWORD,
    CSS_TOKEN_HASH,
    CSS_TOKEN_STRING,
    CSS_TOKEN_BAD_STRING,
    CSS_TOKEN_URL,
    CSS_TOKEN_BAD_URL,
    CSS_TOKEN_NUMBER,
    CSS_TOKEN_DIMENSION,
    CSS_TOKEN_PERCENTAGE,
    CSS_TOKEN_WHITESPACE,
    CSS_TOKEN_CDO,
    CSS_TOKEN_CDC,
    CSS_TOKEN_COLON,
    CSS_TOKEN_SEMICOLON,
    CSS_TOKEN_COMMA,
    CSS_TOKEN_OPEN_PAREN,
    CSS_TOKEN_CLOSE_PAREN,
    CSS_TOKEN_OPEN_BRACKET,
    CSS_TOKEN_CLOSE_BRACKET,
    CSS_TOKEN_OPEN_BRACE,
    CSS_TOKEN_CLOSE_BRACE,
    CSS_TOKEN_DELIM,
    CSS_TOKEN_EOF,
} vw_css_token_type_t;
```

### 4.2 CSS Parser with Error Recovery

```c
typedef struct vw_css_parser vw_css_parser_t;

vw_css_parser_t* vw_css_parser_create(vw_string_interner_t* interner,
                                       vw_arena_t* arena);
void vw_css_parser_destroy(vw_css_parser_t* parser);

// Parse complete stylesheet
vw_css_stylesheet_t* vw_css_parser_parse_stylesheet(vw_css_parser_t* parser,
                                                      const char* css,
                                                      size_t len);

// Parse inline style attribute
vw_css_declaration_block_t* vw_css_parser_parse_inline(vw_css_parser_t* parser,
                                                         const char* css,
                                                         size_t len);
```

**Error Recovery Rules:**
- Invalid declaration: Skip to next `;` or `}`
- Invalid selector: Skip entire rule
- Invalid at-rule: Skip to `;` or matching `{}`

### 4.3 Selectors

```c
typedef enum {
    CSS_SELECTOR_UNIVERSAL,       // *
    CSS_SELECTOR_TYPE,            // div
    CSS_SELECTOR_CLASS,           // .class
    CSS_SELECTOR_ID,              // #id
    CSS_SELECTOR_ATTRIBUTE,       // [attr=val]
    CSS_SELECTOR_PSEUDO_CLASS,    // :hover
    CSS_SELECTOR_PSEUDO_ELEMENT,  // ::before
} vw_css_selector_type_t;

typedef enum {
    CSS_COMBINATOR_NONE,
    CSS_COMBINATOR_DESCENDANT,    // A B
    CSS_COMBINATOR_CHILD,         // A > B
    CSS_COMBINATOR_NEXT_SIBLING,  // A + B
    CSS_COMBINATOR_SUBSEQUENT,    // A ~ B
} vw_css_combinator_t;
```

**Specificity Calculation:**
```
Format: (a, b, c) packed into uint32_t
a = ID selectors count
b = class + attribute + pseudo-class count
c = type + pseudo-element count

Specificity = (a << 20) | (b << 10) | c
```

### 4.4 CSS Values

```c
typedef enum {
    CSS_VALUE_KEYWORD,
    CSS_VALUE_NUMBER,
    CSS_VALUE_INTEGER,
    CSS_VALUE_LENGTH,
    CSS_VALUE_ANGLE,
    CSS_VALUE_TIME,
    CSS_VALUE_COLOR,
    CSS_VALUE_STRING,
    CSS_VALUE_URL,
    CSS_VALUE_FUNCTION,
    CSS_VALUE_LIST,
} vw_css_value_type_t;

typedef enum {
    CSS_UNIT_NONE,
    CSS_UNIT_PX, CSS_UNIT_PT, CSS_UNIT_PC, CSS_UNIT_CM, CSS_UNIT_MM, CSS_UNIT_IN,
    CSS_UNIT_EM, CSS_UNIT_REM, CSS_UNIT_EX, CSS_UNIT_CH,
    CSS_UNIT_VW, CSS_UNIT_VH, CSS_UNIT_VMIN, CSS_UNIT_VMAX,
    CSS_UNIT_PERCENT,
} vw_css_unit_t;
```

---

## 5. Phase 4: Style Resolution

### 5.1 Computed Styles

```c
typedef struct vw_computed_style {
    // Display & Box
    uint8_t display;
    uint8_t visibility;
    uint8_t box_sizing;
    float opacity;

    // Position
    uint8_t position;
    float top, right, bottom, left;
    int32_t z_index;

    // Dimensions (px, NAN for auto)
    float width, height;
    float min_width, min_height;
    float max_width, max_height;

    // Box model (px)
    struct { float top, right, bottom, left; } margin;
    struct { float top, right, bottom, left; } padding;
    struct {
        float width;
        uint32_t color;
        uint8_t style;
    } border_top, border_right, border_bottom, border_left;
    float border_radius[4];

    // Flexbox
    uint8_t flex_direction;
    uint8_t flex_wrap;
    uint8_t justify_content;
    uint8_t align_items;
    uint8_t align_self;
    float flex_grow, flex_shrink, flex_basis;

    // Typography
    const char* font_family;
    float font_size;
    uint16_t font_weight;
    uint8_t font_style;
    float line_height;

    // Text
    uint32_t color;
    uint8_t text_align;
    uint8_t text_decoration;
    uint8_t white_space;
    uint8_t word_break;
    uint8_t overflow_wrap;

    // Background
    uint32_t background_color;

    // Overflow
    uint8_t overflow_x, overflow_y;

    // UI
    uint8_t cursor;
} vw_computed_style_t;
```

### 5.2 Style Resolution API

```c
typedef struct vw_style_resolver vw_style_resolver_t;

vw_style_resolver_t* vw_style_resolver_create(vw_dom_document_t* doc);
void vw_style_resolver_destroy(vw_style_resolver_t* resolver);

void vw_style_resolver_add_sheet(vw_style_resolver_t* resolver,
                                  vw_css_stylesheet_t* sheet,
                                  uint8_t origin);

void vw_style_resolver_resolve_all(vw_style_resolver_t* resolver);
const vw_computed_style_t* vw_style_get_computed(vw_dom_element_t* element);

void vw_style_invalidate_element(vw_dom_element_t* element);
void vw_style_invalidate_subtree(vw_dom_element_t* element);
```

---

## 6. Phase 5: Block Layout

### 6.1 Layout Box

```c
typedef enum {
    VW_BOX_BLOCK,
    VW_BOX_INLINE,
    VW_BOX_INLINE_BLOCK,
    VW_BOX_FLEX,
    VW_BOX_FLEX_ITEM,
    VW_BOX_TABLE,
    VW_BOX_TABLE_ROW,
    VW_BOX_TABLE_CELL,
    VW_BOX_LINE,
    VW_BOX_TEXT,
    VW_BOX_REPLACED,
    VW_BOX_ANONYMOUS_BLOCK,
} vw_box_type_t;

typedef struct vw_layout_box {
    vw_box_type_t type;
    vw_dom_element_t* element;
    const vw_computed_style_t* style;

    // Tree structure
    struct vw_layout_box* parent;
    struct vw_layout_box* first_child;
    struct vw_layout_box* last_child;
    struct vw_layout_box* prev_sibling;
    struct vw_layout_box* next_sibling;

    // Dimensions
    float content_width, content_height;
    float margin_top, margin_right, margin_bottom, margin_left;

    // Position
    float x, y;
    float abs_x, abs_y;

    // Scrolling
    float scroll_x, scroll_y;
    float scroll_width, scroll_height;

    // Dirty tracking
    uint32_t dirty_flags;

    // Type-specific data
    union {
        struct {
            const char* text;
            size_t byte_offset;
            size_t byte_length;
            struct vw_shaped_text* shaped;
        } text;

        struct {
            void* image_data;
            float intrinsic_width;
            float intrinsic_height;
            float intrinsic_ratio;
        } replaced;

        struct {
            float baseline;
            float ascent, descent;
        } line;
    };

    bool creates_stacking_context;
    int32_t z_index;
} vw_layout_box_t;
```

### 6.2 Layout Context

```c
typedef struct vw_layout_context {
    vw_dom_document_t* document;
    float viewport_width;
    float viewport_height;
    float root_font_size;
    vw_layout_box_t* root_box;
    vw_arena_t* arena;
    bool needs_full_layout;
} vw_layout_context_t;

vw_layout_context_t* vw_layout_create(vw_dom_document_t* doc,
                                       float viewport_width,
                                       float viewport_height);
void vw_layout_destroy(vw_layout_context_t* ctx);
void vw_layout_compute(vw_layout_context_t* ctx);
void vw_layout_compute_incremental(vw_layout_context_t* ctx);
void vw_layout_mark_dirty(vw_layout_context_t* ctx, vw_layout_box_t* box);
vw_layout_box_t* vw_layout_hit_test(vw_layout_context_t* ctx, float x, float y);
```

### 6.3 Margin Collapsing

**Margin Collapsing Rules:**
- Adjacent block-level siblings: margins collapse
- Parent + first/last child: collapse if no border/padding/content
- Empty block: top and bottom margins collapse

**Margins DO NOT collapse when:**
- Boxes with clearance
- Root element
- Floats
- Absolutely positioned boxes
- Inline-block boxes
- Flex items

---

## 7. Phase 6: Inline Layout & Text

### 7.1 Text Shaping

```c
typedef struct vw_text_shaper vw_text_shaper_t;

vw_text_shaper_t* vw_text_shaper_create(void);
void vw_text_shaper_destroy(vw_text_shaper_t* shaper);

void vw_text_shaper_set_font(vw_text_shaper_t* shaper,
                              const char* family,
                              float size,
                              int weight,
                              int style);

typedef struct vw_glyph {
    uint32_t codepoint;
    uint16_t glyph_id;
    float x_offset, y_offset;
    float x_advance;
    size_t cluster;
} vw_glyph_t;

typedef struct vw_shaped_text {
    float width, height;
    float ascent, descent, baseline;
    vw_glyph_t* glyphs;
    size_t glyph_count;
    size_t* break_points;
    size_t break_count;
} vw_shaped_text_t;

vw_shaped_text_t* vw_text_shape(vw_text_shaper_t* shaper,
                                 const char* text,
                                 size_t byte_length);
```

### 7.2 Font Resolution

```c
typedef struct vw_font_resolver vw_font_resolver_t;

vw_font_resolver_t* vw_font_resolver_create(void);
void vw_font_resolver_destroy(vw_font_resolver_t* resolver);

void vw_font_resolver_add_system_fonts(vw_font_resolver_t* resolver);
void* vw_font_resolve(vw_font_resolver_t* resolver,
                       const char* family_list,
                       int weight, int style);
void* vw_font_get_fallback(vw_font_resolver_t* resolver, uint32_t codepoint);
```

### 7.3 White-Space Handling

```c
typedef enum {
    VW_WS_NORMAL,
    VW_WS_NOWRAP,
    VW_WS_PRE,
    VW_WS_PRE_WRAP,
    VW_WS_PRE_LINE,
} vw_white_space_t;

char* vw_text_collapse_whitespace(vw_arena_t* arena,
                                   const char* text,
                                   size_t length,
                                   vw_white_space_t mode,
                                   bool at_line_start,
                                   bool at_line_end);
```

### 7.4 Line Breaking

```c
typedef enum {
    VW_BREAK_ALLOWED,
    VW_BREAK_MANDATORY,
    VW_BREAK_PROHIBITED,
} vw_break_type_t;

void vw_text_find_breaks(const char* text,
                          size_t length,
                          vw_white_space_t ws_mode,
                          uint8_t word_break,
                          uint8_t overflow_wrap,
                          vw_break_type_t* out_breaks);

size_t vw_text_find_break_point(vw_text_shaper_t* shaper,
                                 const char* text,
                                 size_t length,
                                 float available_width,
                                 vw_break_type_t* breaks);
```

---

## 8. Phase 7: Rendering/Painting

### 8.1 Stacking Contexts

```c
typedef struct vw_stacking_context {
    vw_layout_box_t* root;
    int32_t z_index;
    struct vw_stacking_context** children;
    size_t child_count;
} vw_stacking_context_t;
```

**Paint Order:**
1. Background and borders of stacking context root
2. Child stacking contexts with negative z-index
3. Block-level descendants (non-positioned)
4. Float descendants
5. Inline-level descendants
6. Positioned descendants with z-index: auto or 0
7. Child stacking contexts with positive z-index

### 8.2 Display List

```c
typedef enum {
    VW_PAINT_FILL_RECT,
    VW_PAINT_FILL_ROUNDED_RECT,
    VW_PAINT_STROKE_RECT,
    VW_PAINT_TEXT,
    VW_PAINT_IMAGE,
    VW_PAINT_PUSH_CLIP,
    VW_PAINT_POP_CLIP,
    VW_PAINT_PUSH_OPACITY,
    VW_PAINT_POP_OPACITY,
} vw_paint_cmd_type_t;

typedef struct vw_paint_cmd {
    vw_paint_cmd_type_t type;
    float x, y, width, height;
    // ... type-specific data
    struct vw_paint_cmd* next;
} vw_paint_cmd_t;

typedef struct vw_display_list {
    vw_paint_cmd_t* first;
    vw_paint_cmd_t* last;
    size_t count;
    vw_arena_t* arena;
} vw_display_list_t;
```

### 8.3 Painter

```c
typedef struct vw_painter vw_painter_t;

vw_painter_t* vw_painter_create(vw_layout_context_t* layout);
void vw_painter_destroy(vw_painter_t* painter);

vw_display_list_t* vw_painter_paint(vw_painter_t* painter);
vw_display_list_t* vw_painter_paint_dirty(vw_painter_t* painter,
                                           float dirty_x, float dirty_y,
                                           float dirty_width, float dirty_height);
```

---

## 9. Phase 8: Resource Loading

### 9.1 Resource Manager

```c
typedef enum {
    VW_RESOURCE_HTML,
    VW_RESOURCE_CSS,
    VW_RESOURCE_IMAGE,
    VW_RESOURCE_FONT,
    VW_RESOURCE_OTHER,
} vw_resource_type_t;

typedef enum {
    VW_RESOURCE_STATE_PENDING,
    VW_RESOURCE_STATE_LOADING,
    VW_RESOURCE_STATE_LOADED,
    VW_RESOURCE_STATE_ERROR,
    VW_RESOURCE_STATE_CANCELED,
} vw_resource_state_t;

typedef struct vw_resource {
    vw_url_t* url;
    vw_resource_type_t type;
    vw_resource_state_t state;
    uint8_t* data;
    size_t data_length;
    char* mime_type;
    char* error_message;
    void (*on_complete)(struct vw_resource*, void*);
    void (*on_error)(struct vw_resource*, void*);
    void* user_data;
} vw_resource_t;

typedef struct vw_resource_manager vw_resource_manager_t;

vw_resource_manager_t* vw_resource_manager_create(void* http_client);
void vw_resource_manager_destroy(vw_resource_manager_t* mgr);

vw_resource_t* vw_resource_fetch(vw_resource_manager_t* mgr,
                                  const char* url,
                                  vw_resource_type_t type,
                                  void (*on_complete)(vw_resource_t*, void*),
                                  void (*on_error)(vw_resource_t*, void*),
                                  void* user_data);
```

### 9.2 Security Policy

```c
typedef struct vw_security_policy {
    char** allowed_schemes;
    size_t allowed_scheme_count;
    char** blocked_hosts;
    size_t blocked_host_count;
    bool allow_inline_styles;
    bool allow_external_styles;
    bool allow_images;
    size_t max_redirects;
    size_t max_resource_size;
    size_t max_total_size;
} vw_security_policy_t;

bool vw_security_check_url(vw_security_policy_t* policy, const vw_url_t* url);
```

---

## 10. Phase 9: Interactivity

### 10.1 Event System

```c
typedef enum {
    VW_EVENT_MOUSE_MOVE,
    VW_EVENT_MOUSE_DOWN,
    VW_EVENT_MOUSE_UP,
    VW_EVENT_CLICK,
    VW_EVENT_DBLCLICK,
    VW_EVENT_SCROLL,
    VW_EVENT_WHEEL,
    VW_EVENT_KEY_DOWN,
    VW_EVENT_KEY_UP,
    VW_EVENT_FOCUS,
    VW_EVENT_BLUR,
} vw_event_type_t;

typedef struct vw_event {
    vw_event_type_t type;
    vw_dom_element_t* target;
    vw_dom_element_t* current_target;
    bool propagation_stopped;
    bool default_prevented;

    struct {
        float client_x, client_y;
        float page_x, page_y;
        int button, buttons;
        bool ctrl_key, shift_key, alt_key;
    } mouse;

    struct {
        float delta_x, delta_y;
    } wheel;

    struct {
        const char* key;
        const char* code;
        bool ctrl_key, shift_key, alt_key;
    } keyboard;
} vw_event_t;

bool vw_event_dispatch(vw_dom_element_t* target, vw_event_t* event);
```

### 10.2 Interaction State

```c
typedef struct vw_interaction_state {
    vw_dom_element_t* hovered;
    vw_dom_element_t* active;
    vw_dom_element_t* focused;
    float mouse_x, mouse_y;
    int current_cursor;
} vw_interaction_state_t;

void vw_interaction_handle_mouse_move(vw_interaction_state_t* state,
                                       vw_layout_context_t* layout,
                                       float x, float y);
void vw_interaction_handle_mouse_down(vw_interaction_state_t* state,
                                       vw_layout_context_t* layout,
                                       float x, float y, int button);
void vw_interaction_handle_mouse_up(vw_interaction_state_t* state,
                                     vw_layout_context_t* layout,
                                     float x, float y, int button);
```

---

## 11. Phase 10: GUI Integration

### 11.1 WebBrowser Widget

```c
typedef struct vg_webbrowser {
    vg_widget_t base;

    vw_dom_document_t* document;
    vw_style_resolver_t* style_resolver;
    vw_layout_context_t* layout;
    vw_painter_t* painter;
    vw_display_list_t* display_list;
    vw_interaction_state_t interaction;
    vw_document_loader_t* loader;
    vw_resource_manager_t* resources;

    char* current_url;
    struct {
        char** urls;
        char** titles;
        int index, count;
    } history;

    bool is_loading;
    float load_progress;
    float scroll_x, scroll_y;
    bool needs_layout, needs_paint;

    void (*on_navigate)(vg_widget_t*, const char*, void*);
    void (*on_title_change)(vg_widget_t*, const char*, void*);
    void (*on_load_start)(vg_widget_t*, void*);
    void (*on_load_finish)(vg_widget_t*, void*);
    void* callback_data;
} vg_webbrowser_t;
```

### 11.2 Widget API

```c
vg_widget_t* vg_webbrowser_create(vg_widget_t* parent);

void vg_webbrowser_load_url(vg_webbrowser_t* browser, const char* url);
void vg_webbrowser_load_html(vg_webbrowser_t* browser, const char* html, const char* base_url);
void vg_webbrowser_go_back(vg_webbrowser_t* browser);
void vg_webbrowser_go_forward(vg_webbrowser_t* browser);
void vg_webbrowser_reload(vg_webbrowser_t* browser);
void vg_webbrowser_stop(vg_webbrowser_t* browser);

const char* vg_webbrowser_get_url(vg_webbrowser_t* browser);
const char* vg_webbrowser_get_title(vg_webbrowser_t* browser);
bool vg_webbrowser_is_loading(vg_webbrowser_t* browser);
bool vg_webbrowser_can_go_back(vg_webbrowser_t* browser);
bool vg_webbrowser_can_go_forward(vg_webbrowser_t* browser);
```

### 11.3 Runtime Bindings for Zia

```c
// rt_web.h
void* rt_webbrowser_new(void* parent);
void rt_webbrowser_load_url(void* browser, rt_string url);
void rt_webbrowser_load_html(void* browser, rt_string html, rt_string base_url);
void rt_webbrowser_go_back(void* browser);
void rt_webbrowser_go_forward(void* browser);
void rt_webbrowser_reload(void* browser);
rt_string rt_webbrowser_get_url(void* browser);
rt_string rt_webbrowser_get_title(void* browser);
int64_t rt_webbrowser_is_loading(void* browser);
int64_t rt_webbrowser_can_go_back(void* browser);
int64_t rt_webbrowser_can_go_forward(void* browser);
```

---

## 12. Phase 11: Advanced Layout

### 12.1 Flexbox Layout

```c
void vw_layout_flex(vw_layout_context_t* ctx,
                    vw_layout_box_t* flex_container,
                    float available_width,
                    float available_height);
```

**Flexbox Algorithm:**
1. Collect flex items
2. Determine main axis
3. Calculate hypothetical main size for each item
4. Determine single-line or multi-line
5. Distribute free space by flex-grow/shrink
6. Determine cross size
7. Align items (justify-content, align-items)
8. Handle order property

### 12.2 Positioned Layout

- **Static:** Normal flow
- **Relative:** Offset from normal position
- **Absolute:** Relative to positioned ancestor
- **Fixed:** Relative to viewport (watch for transform ancestor)
- **Sticky:** Normal until threshold, then fixed

### 12.3 Replaced Elements

```c
void vw_layout_replaced(vw_layout_context_t* ctx, vw_layout_box_t* box);
```

Sizing algorithm:
1. Both width and height specified → use specified
2. Only width → height = width / ratio
3. Only height → width = height * ratio
4. Neither → use intrinsic, constrain to max

---

## 13. Phase 12: Advanced Features

### 13.1 Generated Content

- `::before`, `::after` pseudo-elements
- `content` property: strings, attr(), counter()
- List markers

### 13.2 Accessibility

```c
typedef enum {
    VW_ROLE_NONE, VW_ROLE_BUTTON, VW_ROLE_LINK, VW_ROLE_HEADING,
    VW_ROLE_LIST, VW_ROLE_LIST_ITEM, VW_ROLE_TABLE, VW_ROLE_IMAGE,
    // ... more
} vw_aria_role_t;

vw_aria_role_t vw_get_role(vw_dom_element_t* element);
char* vw_get_accessible_name(vw_dom_element_t* element);
```

### 13.3 Future Phases

- **Phase 13:** JavaScript Engine
- **Phase 14:** Transitions, Animations, Transforms
- **Phase 15:** Web fonts, Media queries, SVG

---

## 14. Data Structures

### 14.1 Hash Maps

```c
typedef struct vw_hash_map vw_hash_map_t;

vw_hash_map_t* vw_hash_map_create(size_t initial_capacity);
void vw_hash_map_destroy(vw_hash_map_t* map);
void vw_hash_map_set(vw_hash_map_t* map, const char* key, void* value);
void* vw_hash_map_get(vw_hash_map_t* map, const char* key);
bool vw_hash_map_has(vw_hash_map_t* map, const char* key);
void vw_hash_map_remove(vw_hash_map_t* map, const char* key);
```

### 14.2 Dynamic Arrays

```c
typedef struct vw_array {
    void** items;
    size_t count;
    size_t capacity;
} vw_array_t;

vw_array_t* vw_array_create(size_t initial_capacity);
void vw_array_destroy(vw_array_t* arr);
void vw_array_push(vw_array_t* arr, void* item);
void* vw_array_pop(vw_array_t* arr);
void* vw_array_get(vw_array_t* arr, size_t index);
```

---

## 15. File Organization

```
src/lib/web/
├── CMakeLists.txt
├── include/
│   ├── viperweb.h
│   ├── vw_types.h
│   ├── vw_encoding.h
│   ├── vw_url.h
│   ├── vw_html.h
│   ├── vw_css.h
│   ├── vw_dom.h
│   ├── vw_style.h
│   ├── vw_layout.h
│   ├── vw_text.h
│   ├── vw_paint.h
│   ├── vw_event.h
│   └── vw_resource.h
├── src/
│   ├── encoding/
│   ├── url/
│   ├── html/
│   ├── css/
│   ├── dom/
│   ├── style/
│   ├── layout/
│   ├── text/
│   ├── paint/
│   ├── resource/
│   ├── event/
│   └── util/
└── tests/

src/lib/gui/
├── include/vg_webbrowser.h
└── src/vg_webbrowser.c

src/runtime/
├── rt_web.h
└── rt_web.c
```

---

## 16. Testing Strategy

### Unit Tests
- Encoding detection and conversion
- URL parsing and resolution
- HTML lexer/parser
- CSS lexer/parser
- Selector matching
- Style resolution
- Layout (block, inline, flex)

### Integration Tests
- Render to image, compare with golden images
- Event handling
- Document loading

### Conformance Tests
- html5lib-tests
- CSS WG test suite subsets

---

## 17. Milestones & Timeline

| Phase | Description | Duration |
|-------|-------------|----------|
| 1 | Foundation & Encoding | 2 weeks |
| 2 | HTML Parsing | 3 weeks |
| 3 | CSS Parsing | 3 weeks |
| 4 | Style Resolution | 2-3 weeks |
| 5 | Block Layout | 3-4 weeks |
| 6 | Inline Layout & Text | 4-5 weeks |
| 7 | Rendering | 2-3 weeks |
| 8 | Resource Loading | 2-3 weeks |
| 9 | Interactivity | 3 weeks |
| 10 | GUI Integration | 2 weeks |
| 11 | Advanced Layout | 4-5 weeks |
| 12 | Polish & Testing | 2-3 weeks |
| **Total** | | **32-40 weeks** |

**Risk Factors:**
- Text shaping complexity
- Font handling across platforms
- Edge cases in margin collapsing
- Real-world HTML quirks

---

## 18. References

### Specifications
- [HTML Living Standard](https://html.spec.whatwg.org/)
- [CSS 2.2](https://www.w3.org/TR/CSS22/)
- [CSS Flexbox](https://www.w3.org/TR/css-flexbox-1/)
- [DOM Living Standard](https://dom.spec.whatwg.org/)
- [Encoding Standard](https://encoding.spec.whatwg.org/)
- [URL Standard](https://url.spec.whatwg.org/)

### Implementations
- [Servo](https://github.com/servo/servo)
- [litehtml](https://github.com/nickg/litehtml)
- [Ladybird](https://github.com/SerenityOS/serenity)

### Books
- "Web Browser Engineering" (browser.engineering)
- "Let's Build a Browser Engine" by Matt Brubeck

---

## Appendix A: User-Agent Stylesheet

```css
html, body, div, p, h1, h2, h3, h4, h5, h6,
ul, ol, li, dl, dt, dd, table, form { display: block; }

h1 { font-size: 2em; margin: 0.67em 0; font-weight: bold; }
h2 { font-size: 1.5em; margin: 0.83em 0; font-weight: bold; }
h3 { font-size: 1.17em; margin: 1em 0; font-weight: bold; }
p { margin: 1em 0; }

ul, ol { padding-left: 40px; margin: 1em 0; }
ul { list-style-type: disc; }
ol { list-style-type: decimal; }
li { display: list-item; }

a:link { color: #0000EE; text-decoration: underline; }
a:visited { color: #551A8B; }
a:any-link { cursor: pointer; }

b, strong { font-weight: bold; }
i, em { font-style: italic; }
code { font-family: monospace; }

body { margin: 8px; }

[hidden], head, script, style, title, meta, link { display: none; }
```

---

## Appendix B: Quirks Mode Differences

| Feature | Standards | Quirks |
|---------|-----------|--------|
| Box sizing | content-box | border-box (some) |
| Table fonts | Inherited | Not inherited |
| Image valign | baseline | bottom |

---

*Document Version: 2.0*
*Last Updated: 2026-01-20*
