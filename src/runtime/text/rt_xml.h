//===----------------------------------------------------------------------===//
//
// File: src/runtime/text/rt_xml.h
// Purpose: XML parsing and formatting utilities for Viper.Data.Xml, handling elements, attributes, text nodes, comments, and CDATA with a reference-counted tree API.
//
// Key invariants:
//   - Parses well-formed XML; returns NULL on malformed input.
//   - Supports elements, attributes, text content, comments, and CDATA sections.
//   - Tree nodes are reference-counted; children are retained by their parent.
//   - rt_xml_serialize produces compact XML; rt_xml_serialize_pretty produces indented output.
//
// Ownership/Lifetime:
//   - Returned node objects start with refcount 1; caller owns the root.
//   - Child nodes are retained by their parent; releasing the root releases all children.
//
// Links: src/runtime/text/rt_xml.c (implementation), src/runtime/core/rt_string.h
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_string.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    //=========================================================================
    // XML Node Types
    //=========================================================================

    /// @brief Node type enumeration.
    typedef enum
    {
        XML_NODE_ELEMENT = 1, ///< Element node (has tag, attributes, children)
        XML_NODE_TEXT = 2,    ///< Text content
        XML_NODE_COMMENT = 3, ///< Comment node (<!-- ... -->)
        XML_NODE_CDATA = 4,   ///< CDATA section (<![CDATA[ ... ]]>)
        XML_NODE_DOCUMENT = 5 ///< Document root
    } XmlNodeType;

    //=========================================================================
    // XML Parsing
    //=========================================================================

    /// @brief Parse XML string into a document node.
    /// @param text XML text to parse.
    /// @return Document node containing the parsed tree, or NULL on error.
    /// @note Call rt_xml_error() to get error message on parse failure.
    void *rt_xml_parse(rt_string text);

    /// @brief Get the last parse error message.
    /// @return Error message string, or empty string if no error.
    rt_string rt_xml_error(void);

    /// @brief Check if a string contains valid XML.
    /// @param text String to validate.
    /// @return 1 if valid XML, 0 otherwise.
    int8_t rt_xml_is_valid(rt_string text);

    //=========================================================================
    // Node Creation
    //=========================================================================

    /// @brief Create a new element node.
    /// @param tag Element tag name.
    /// @return New element node.
    void *rt_xml_element(rt_string tag);

    /// @brief Create a new text node.
    /// @param content Text content.
    /// @return New text node.
    void *rt_xml_text(rt_string content);

    /// @brief Create a new comment node.
    /// @param content Comment text (without <!-- -->).
    /// @return New comment node.
    void *rt_xml_comment(rt_string content);

    /// @brief Create a new CDATA node.
    /// @param content CDATA content (without <![CDATA[ ]]>).
    /// @return New CDATA node.
    void *rt_xml_cdata(rt_string content);

    //=========================================================================
    // Node Properties
    //=========================================================================

    /// @brief Get the type of a node.
    /// @param node XML node.
    /// @return Node type (XML_NODE_ELEMENT, etc.)
    int64_t rt_xml_node_type(void *node);

    /// @brief Get the tag name of an element node.
    /// @param node Element node.
    /// @return Tag name, or empty string for non-elements.
    rt_string rt_xml_tag(void *node);

    /// @brief Get the text content of a text/comment/cdata node.
    /// @param node Text, comment, or CDATA node.
    /// @return Content string.
    rt_string rt_xml_content(void *node);

    /// @brief Get all text content recursively (for elements).
    /// @param node Any node.
    /// @return Combined text content of node and all descendants.
    rt_string rt_xml_text_content(void *node);

    //=========================================================================
    // Attributes
    //=========================================================================

    /// @brief Get an attribute value.
    /// @param node Element node.
    /// @param name Attribute name.
    /// @return Attribute value, or empty string if not present.
    rt_string rt_xml_attr(void *node, rt_string name);

    /// @brief Check if an attribute exists.
    /// @param node Element node.
    /// @param name Attribute name.
    /// @return 1 if attribute exists, 0 otherwise.
    int8_t rt_xml_has_attr(void *node, rt_string name);

    /// @brief Set an attribute value.
    /// @param node Element node.
    /// @param name Attribute name.
    /// @param value Attribute value.
    void rt_xml_set_attr(void *node, rt_string name, rt_string value);

    /// @brief Remove an attribute.
    /// @param node Element node.
    /// @param name Attribute name.
    /// @return 1 if removed, 0 if not present.
    int8_t rt_xml_remove_attr(void *node, rt_string name);

    /// @brief Get all attribute names.
    /// @param node Element node.
    /// @return Seq of attribute name strings.
    void *rt_xml_attr_names(void *node);

    //=========================================================================
    // Children
    //=========================================================================

    /// @brief Get child nodes.
    /// @param node Element or document node.
    /// @return Seq of child nodes.
    void *rt_xml_children(void *node);

    /// @brief Get number of children.
    /// @param node Element or document node.
    /// @return Number of child nodes.
    int64_t rt_xml_child_count(void *node);

    /// @brief Get child at index.
    /// @param node Element or document node.
    /// @param index 0-based index.
    /// @return Child node, or NULL if index out of bounds.
    void *rt_xml_child_at(void *node, int64_t index);

    /// @brief Get first child element with tag.
    /// @param node Element or document node.
    /// @param tag Tag name to match.
    /// @return First matching element, or NULL if not found.
    void *rt_xml_child(void *node, rt_string tag);

    /// @brief Get all child elements with tag.
    /// @param node Element or document node.
    /// @param tag Tag name to match.
    /// @return Seq of matching element nodes.
    void *rt_xml_children_by_tag(void *node, rt_string tag);

    /// @brief Append a child node.
    /// @param node Parent element or document node.
    /// @param child Node to append.
    void rt_xml_append(void *node, void *child);

    /// @brief Insert a child at index.
    /// @param node Parent element or document node.
    /// @param index 0-based position.
    /// @param child Node to insert.
    void rt_xml_insert(void *node, int64_t index, void *child);

    /// @brief Remove a child node.
    /// @param node Parent element or document node.
    /// @param child Node to remove.
    /// @return 1 if removed, 0 if not found.
    int8_t rt_xml_remove(void *node, void *child);

    /// @brief Remove child at index.
    /// @param node Parent element or document node.
    /// @param index 0-based index.
    void rt_xml_remove_at(void *node, int64_t index);

    /// @brief Set text content (replaces all children with text node).
    /// @param node Element node.
    /// @param text Text content.
    void rt_xml_set_text(void *node, rt_string text);

    //=========================================================================
    // Navigation
    //=========================================================================

    /// @brief Get parent node.
    /// @param node Any node.
    /// @return Parent node, or NULL if root.
    void *rt_xml_parent(void *node);

    /// @brief Get document root element.
    /// @param doc Document node.
    /// @return Root element, or NULL if empty document.
    void *rt_xml_root(void *doc);

    /// @brief Find elements by tag name (recursive).
    /// @param node Starting node.
    /// @param tag Tag name to match.
    /// @return Seq of all matching elements in subtree.
    void *rt_xml_find_all(void *node, rt_string tag);

    /// @brief Find first element by tag name (recursive).
    /// @param node Starting node.
    /// @param tag Tag name to match.
    /// @return First matching element, or NULL if not found.
    void *rt_xml_find(void *node, rt_string tag);

    //=========================================================================
    // Formatting
    //=========================================================================

    /// @brief Format node as compact XML string.
    /// @param node Any node.
    /// @return XML string without extra whitespace.
    rt_string rt_xml_format(void *node);

    /// @brief Format node as pretty-printed XML.
    /// @param node Any node.
    /// @param indent Number of spaces per indentation level.
    /// @return XML string with indentation and newlines.
    rt_string rt_xml_format_pretty(void *node, int64_t indent);

    //=========================================================================
    // Utility
    //=========================================================================

    /// @brief Escape special XML characters in text.
    /// @param text Text to escape.
    /// @return Escaped string with &amp; &lt; &gt; etc.
    rt_string rt_xml_escape(rt_string text);

    /// @brief Unescape XML entities in text.
    /// @param text Text with XML entities.
    /// @return Unescaped string.
    rt_string rt_xml_unescape(rt_string text);

#ifdef __cplusplus
}
#endif
