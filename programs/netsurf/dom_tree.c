#include "dom_tree.h"

#include <stdlib.h>
#include <string.h>

static char *copy_bytes(const uint8_t *bytes, uint32_t length) {
    char *copy = (char *)malloc((size_t)length + 1U);
    uint32_t i;
    if (!copy) return NULL;
    for (i = 0U; i < length; i++) copy[i] = (char)bytes[i];
    copy[length] = '\0';
    return copy;
}

static bool ascii_equal(const char *left, uint32_t left_length,
                        const char *right) {
    uint32_t i = 0U;
    if (!left || !right) return false;
    while (right[i]) {
        char a;
        char b;
        if (i >= left_length) return false;
        a = left[i];
        b = right[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
        if (a != b) return false;
        i++;
    }
    return i == left_length;
}

static void force_destroy_node(nsbk_dom_node_t *node) {
    nsbk_dom_node_t *child;
    nsbk_dom_node_t *next;
    uint32_t i;
    if (!node) return;
    child = node->first_child;
    while (child) {
        next = child->next;
        force_destroy_node(child);
        child = next;
    }
    if (node->name) lwc_string_unref(node->name);
    if (node->css_id) lwc_string_unref(node->css_id);
    if (node->css_classes) {
        for (i = 0U; i < node->css_class_count; i++)
            if (node->css_classes[i]) lwc_string_unref(node->css_classes[i]);
        free(node->css_classes);
    }
    if (node->text) free(node->text);
    for (i = 0U; i < node->attribute_count; i++) {
        if (node->attributes[i].name)
            lwc_string_unref(node->attributes[i].name);
        if (node->attributes[i].value)
            free(node->attributes[i].value);
    }
    if (node->attributes) free(node->attributes);
    free(node);
}

static nsbk_dom_node_t *new_node(nsbk_dom_tree_t *tree,
                                 nsbk_dom_node_type_t type) {
    nsbk_dom_node_t *node;
    if (!tree) return NULL;
    node = (nsbk_dom_node_t *)calloc(1U, sizeof(*node));
    if (!node) {
        tree->out_of_memory = true;
        return NULL;
    }
    node->type = type;
    node->references = 1U;
    tree->node_count++;
    if (type == NSBK_DOM_ELEMENT) tree->element_count++;
    return node;
}

static void detach_node(nsbk_dom_node_t *node) {
    nsbk_dom_node_t *parent;
    if (!node || !node->parent) return;
    parent = node->parent;
    if (node->previous) node->previous->next = node->next;
    else parent->first_child = node->next;
    if (node->next) node->next->previous = node->previous;
    else parent->last_child = node->previous;
    node->parent = NULL;
    node->previous = NULL;
    node->next = NULL;
}

static hubbub_error intern_name(nsbk_dom_tree_t *tree,
                                const hubbub_string *name,
                                lwc_string **result) {
    if (!tree || !name || !result) return HUBBUB_BADPARM;
    if (lwc_intern_string((const char *)name->ptr, name->len, result) !=
        lwc_error_ok) {
        tree->out_of_memory = true;
        return HUBBUB_NOMEM;
    }
    return HUBBUB_OK;
}

static hubbub_error append_attributes(nsbk_dom_tree_t *tree,
                                      nsbk_dom_node_t *node,
                                      const hubbub_attribute *attributes,
                                      uint32_t count) {
    uint32_t i;
    if (!tree || !node || (count != 0U && !attributes))
        return HUBBUB_BADPARM;
    for (i = 0U; i < count; i++) {
        nsbk_dom_attribute_t *grown;
        lwc_string *name = NULL;
        char *value;
        uint32_t j;
        bool duplicate = false;
        hubbub_error error = intern_name(tree, &attributes[i].name, &name);
        if (error != HUBBUB_OK) return error;
        for (j = 0U; j < node->attribute_count; j++) {
            if (node->attributes[j].ns == attributes[i].ns &&
                node->attributes[j].name == name) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            lwc_string_unref(name);
            continue;
        }
        value = copy_bytes(attributes[i].value.ptr,
                           (uint32_t)attributes[i].value.len);
        if (!value) {
            lwc_string_unref(name);
            tree->out_of_memory = true;
            return HUBBUB_NOMEM;
        }
        grown = (nsbk_dom_attribute_t *)realloc(
            node->attributes,
            (size_t)(node->attribute_count + 1U) * sizeof(*grown));
        if (!grown) {
            lwc_string_unref(name);
            free(value);
            tree->out_of_memory = true;
            return HUBBUB_NOMEM;
        }
        node->attributes = grown;
        node->attributes[node->attribute_count].ns = attributes[i].ns;
        node->attributes[node->attribute_count].name = name;
        node->attributes[node->attribute_count].value = value;
        node->attributes[node->attribute_count].value_length =
            (uint32_t)attributes[i].value.len;
        node->attribute_count++;
    }
    return HUBBUB_OK;
}

static hubbub_error create_comment(void *context, const hubbub_string *data,
                                   void **result) {
    nsbk_dom_tree_t *tree = (nsbk_dom_tree_t *)context;
    nsbk_dom_node_t *node;
    if (!tree || !data || !result) return HUBBUB_BADPARM;
    node = new_node(tree, NSBK_DOM_COMMENT);
    if (!node) return HUBBUB_NOMEM;
    node->text = copy_bytes(data->ptr, (uint32_t)data->len);
    if (!node->text) {
        force_destroy_node(node);
        tree->out_of_memory = true;
        return HUBBUB_NOMEM;
    }
    node->text_length = (uint32_t)data->len;
    *result = node;
    return HUBBUB_OK;
}

static hubbub_error create_doctype(void *context,
                                   const hubbub_doctype *doctype,
                                   void **result) {
    nsbk_dom_tree_t *tree = (nsbk_dom_tree_t *)context;
    nsbk_dom_node_t *node;
    hubbub_error error;
    if (!tree || !doctype || !result) return HUBBUB_BADPARM;
    node = new_node(tree, NSBK_DOM_DOCTYPE);
    if (!node) return HUBBUB_NOMEM;
    error = intern_name(tree, &doctype->name, &node->name);
    if (error != HUBBUB_OK) {
        force_destroy_node(node);
        return error;
    }
    *result = node;
    return HUBBUB_OK;
}

static hubbub_error create_element(void *context, const hubbub_tag *tag,
                                   void **result) {
    nsbk_dom_tree_t *tree = (nsbk_dom_tree_t *)context;
    nsbk_dom_node_t *node;
    hubbub_error error;
    if (!tree || !tag || !result) return HUBBUB_BADPARM;
    node = new_node(tree, NSBK_DOM_ELEMENT);
    if (!node) return HUBBUB_NOMEM;
    node->ns = tag->ns;
    error = intern_name(tree, &tag->name, &node->name);
    if (error != HUBBUB_OK) {
        force_destroy_node(node);
        return error;
    }
    error = append_attributes(tree, node, tag->attributes,
                              tag->n_attributes);
    if (error != HUBBUB_OK) {
        force_destroy_node(node);
        return error;
    }
    *result = node;
    return HUBBUB_OK;
}

static hubbub_error create_text(void *context, const hubbub_string *data,
                                void **result) {
    nsbk_dom_tree_t *tree = (nsbk_dom_tree_t *)context;
    nsbk_dom_node_t *node;
    if (!tree || !data || !result) return HUBBUB_BADPARM;
    node = new_node(tree, NSBK_DOM_TEXT);
    if (!node) return HUBBUB_NOMEM;
    node->text = copy_bytes(data->ptr, (uint32_t)data->len);
    if (!node->text) {
        force_destroy_node(node);
        tree->out_of_memory = true;
        return HUBBUB_NOMEM;
    }
    node->text_length = (uint32_t)data->len;
    *result = node;
    return HUBBUB_OK;
}

static hubbub_error ref_node(void *context, void *raw_node) {
    nsbk_dom_node_t *node = (nsbk_dom_node_t *)raw_node;
    (void)context;
    if (!node) return HUBBUB_BADPARM;
    node->references++;
    return HUBBUB_OK;
}

static hubbub_error unref_node(void *context, void *raw_node) {
    nsbk_dom_tree_t *tree = (nsbk_dom_tree_t *)context;
    nsbk_dom_node_t *node = (nsbk_dom_node_t *)raw_node;
    if (!tree || !node || node->references == 0U) return HUBBUB_BADPARM;
    node->references--;
    if (node->references == 0U && node->parent == NULL &&
        node != tree->document) {
        force_destroy_node(node);
    }
    return HUBBUB_OK;
}

static bool append_text(nsbk_dom_tree_t *tree, nsbk_dom_node_t *destination,
                        const nsbk_dom_node_t *source) {
    char *grown;
    uint32_t length;
    if (!tree || !destination || !source) return false;
    length = destination->text_length + source->text_length;
    grown = (char *)realloc(destination->text, (size_t)length + 1U);
    if (!grown) {
        tree->out_of_memory = true;
        return false;
    }
    memcpy(grown + destination->text_length, source->text,
           source->text_length);
    grown[length] = '\0';
    destination->text = grown;
    destination->text_length = length;
    return true;
}

static hubbub_error append_child(void *context, void *raw_parent,
                                 void *raw_child, void **result) {
    nsbk_dom_tree_t *tree = (nsbk_dom_tree_t *)context;
    nsbk_dom_node_t *parent = (nsbk_dom_node_t *)raw_parent;
    nsbk_dom_node_t *child = (nsbk_dom_node_t *)raw_child;
    if (!tree || !parent || !child || !result) return HUBBUB_BADPARM;
    if (parent->last_child && parent->last_child->type == NSBK_DOM_TEXT &&
        child->type == NSBK_DOM_TEXT) {
        if (!append_text(tree, parent->last_child, child)) return HUBBUB_NOMEM;
        *result = parent->last_child;
        return ref_node(context, *result);
    }
    detach_node(child);
    child->parent = parent;
    child->previous = parent->last_child;
    if (parent->last_child) parent->last_child->next = child;
    else parent->first_child = child;
    parent->last_child = child;
    *result = child;
    return ref_node(context, child);
}

static hubbub_error insert_before(void *context, void *raw_parent,
                                  void *raw_child, void *raw_reference,
                                  void **result) {
    nsbk_dom_tree_t *tree = (nsbk_dom_tree_t *)context;
    nsbk_dom_node_t *parent = (nsbk_dom_node_t *)raw_parent;
    nsbk_dom_node_t *child = (nsbk_dom_node_t *)raw_child;
    nsbk_dom_node_t *reference = (nsbk_dom_node_t *)raw_reference;
    if (!tree || !parent || !child || !reference || !result ||
        reference->parent != parent) return HUBBUB_BADPARM;
    if (reference->previous &&
        reference->previous->type == NSBK_DOM_TEXT &&
        child->type == NSBK_DOM_TEXT) {
        if (!append_text(tree, reference->previous, child)) return HUBBUB_NOMEM;
        *result = reference->previous;
        return ref_node(context, *result);
    }
    detach_node(child);
    child->parent = parent;
    child->next = reference;
    child->previous = reference->previous;
    if (reference->previous) reference->previous->next = child;
    else parent->first_child = child;
    reference->previous = child;
    *result = child;
    return ref_node(context, child);
}

static hubbub_error remove_child(void *context, void *raw_parent,
                                 void *raw_child, void **result) {
    nsbk_dom_node_t *parent = (nsbk_dom_node_t *)raw_parent;
    nsbk_dom_node_t *child = (nsbk_dom_node_t *)raw_child;
    if (!parent || !child || !result || child->parent != parent)
        return HUBBUB_BADPARM;
    detach_node(child);
    *result = child;
    return ref_node(context, child);
}

static hubbub_error clone_node(void *context, void *raw_node, bool deep,
                               void **result) {
    nsbk_dom_tree_t *tree = (nsbk_dom_tree_t *)context;
    nsbk_dom_node_t *source = (nsbk_dom_node_t *)raw_node;
    nsbk_dom_node_t *copy;
    nsbk_dom_node_t *child;
    uint32_t i;
    if (!tree || !source || !result) return HUBBUB_BADPARM;
    copy = new_node(tree, source->type);
    if (!copy) return HUBBUB_NOMEM;
    copy->ns = source->ns;
    if (source->name) copy->name = lwc_string_ref(source->name);
    if (source->text) {
        copy->text = copy_bytes((const uint8_t *)source->text,
                                source->text_length);
        if (!copy->text) goto no_memory;
        copy->text_length = source->text_length;
    }
    if (source->attribute_count) {
        copy->attributes = (nsbk_dom_attribute_t *)calloc(
            source->attribute_count, sizeof(*copy->attributes));
        if (!copy->attributes) goto no_memory;
        for (i = 0U; i < source->attribute_count; i++) {
            copy->attributes[i].ns = source->attributes[i].ns;
            copy->attributes[i].name =
                lwc_string_ref(source->attributes[i].name);
            copy->attributes[i].value = copy_bytes(
                (const uint8_t *)source->attributes[i].value,
                source->attributes[i].value_length);
            if (!copy->attributes[i].value) goto no_memory;
            copy->attributes[i].value_length =
                source->attributes[i].value_length;
            copy->attribute_count++;
        }
    }
    if (deep) {
        for (child = source->first_child; child; child = child->next) {
            nsbk_dom_node_t *child_copy = NULL;
            void *appended = NULL;
            if (clone_node(context, child, true,
                           (void **)&child_copy) != HUBBUB_OK)
                goto no_memory;
            if (append_child(context, copy, child_copy, &appended) != HUBBUB_OK) {
                force_destroy_node(child_copy);
                goto no_memory;
            }
            unref_node(context, appended);
            unref_node(context, child_copy);
        }
    }
    *result = copy;
    return HUBBUB_OK;

no_memory:
    tree->out_of_memory = true;
    force_destroy_node(copy);
    return HUBBUB_NOMEM;
}

static hubbub_error reparent_children(void *context, void *raw_node,
                                      void *raw_new_parent) {
    nsbk_dom_node_t *node = (nsbk_dom_node_t *)raw_node;
    nsbk_dom_node_t *new_parent = (nsbk_dom_node_t *)raw_new_parent;
    nsbk_dom_node_t *child;
    if (!node || !new_parent) return HUBBUB_BADPARM;
    child = node->first_child;
    while (child) {
        nsbk_dom_node_t *next = child->next;
        void *result = NULL;
        detach_node(child);
        if (append_child(context, new_parent, child, &result) != HUBBUB_OK)
            return HUBBUB_NOMEM;
        unref_node(context, result);
        child = next;
    }
    return HUBBUB_OK;
}

static hubbub_error get_parent(void *context, void *raw_node,
                               bool element_only, void **result) {
    nsbk_dom_node_t *node = (nsbk_dom_node_t *)raw_node;
    nsbk_dom_node_t *parent;
    if (!node || !result) return HUBBUB_BADPARM;
    parent = node->parent;
    if (element_only && parent && parent->type != NSBK_DOM_ELEMENT)
        parent = NULL;
    *result = parent;
    if (parent) return ref_node(context, parent);
    return HUBBUB_OK;
}

static hubbub_error has_children(void *context, void *raw_node,
                                 bool *result) {
    nsbk_dom_node_t *node = (nsbk_dom_node_t *)raw_node;
    (void)context;
    if (!node || !result) return HUBBUB_BADPARM;
    *result = node->first_child != NULL;
    return HUBBUB_OK;
}

static hubbub_error form_associate(void *context, void *raw_form,
                                   void *raw_node) {
    nsbk_dom_node_t *node = (nsbk_dom_node_t *)raw_node;
    (void)context;
    if (!node) return HUBBUB_BADPARM;
    node->form = (nsbk_dom_node_t *)raw_form;
    return HUBBUB_OK;
}

static hubbub_error add_attributes(void *context, void *raw_node,
                                   const hubbub_attribute *attributes,
                                   uint32_t count) {
    nsbk_dom_tree_t *tree = (nsbk_dom_tree_t *)context;
    nsbk_dom_node_t *node = (nsbk_dom_node_t *)raw_node;
    if (!tree || !node || node->type != NSBK_DOM_ELEMENT)
        return HUBBUB_BADPARM;
    return append_attributes(tree, node, attributes, count);
}

static hubbub_error set_quirks_mode(void *context, hubbub_quirks_mode mode) {
    nsbk_dom_tree_t *tree = (nsbk_dom_tree_t *)context;
    if (!tree) return HUBBUB_BADPARM;
    tree->quirks_mode = mode;
    return HUBBUB_OK;
}

static hubbub_error encoding_change(void *context, const char *encoding) {
    nsbk_dom_tree_t *tree = (nsbk_dom_tree_t *)context;
    uint32_t i = 0U;
    if (!tree) return HUBBUB_BADPARM;
    while (encoding && encoding[i] && i + 1U < sizeof(tree->requested_charset)) {
        tree->requested_charset[i] = encoding[i];
        i++;
    }
    tree->requested_charset[i] = '\0';
    return HUBBUB_OK;
}

static hubbub_error complete_script(void *context, void *script) {
    (void)context;
    (void)script;
    return HUBBUB_OK;
}

bool nsbk_dom_tree_init(nsbk_dom_tree_t *tree) {
    if (!tree) return false;
    memset(tree, 0, sizeof(*tree));
    tree->document = new_node(tree, NSBK_DOM_DOCUMENT);
    if (!tree->document) return false;
    tree->quirks_mode = HUBBUB_QUIRKS_MODE_NONE;
    tree->handler.create_comment = create_comment;
    tree->handler.create_doctype = create_doctype;
    tree->handler.create_element = create_element;
    tree->handler.create_text = create_text;
    tree->handler.ref_node = ref_node;
    tree->handler.unref_node = unref_node;
    tree->handler.append_child = append_child;
    tree->handler.insert_before = insert_before;
    tree->handler.remove_child = remove_child;
    tree->handler.clone_node = clone_node;
    tree->handler.reparent_children = reparent_children;
    tree->handler.get_parent = get_parent;
    tree->handler.has_children = has_children;
    tree->handler.form_associate = form_associate;
    tree->handler.add_attributes = add_attributes;
    tree->handler.set_quirks_mode = set_quirks_mode;
    tree->handler.encoding_change = encoding_change;
    tree->handler.complete_script = complete_script;
    tree->handler.ctx = tree;
    return true;
}

void nsbk_dom_tree_destroy(nsbk_dom_tree_t *tree) {
    if (!tree) return;
    if (tree->document) force_destroy_node(tree->document);
    tree->document = NULL;
}

bool nsbk_dom_name_is(const nsbk_dom_node_t *node, const char *name) {
    if (!node || node->type != NSBK_DOM_ELEMENT || !node->name) return false;
    return ascii_equal(lwc_string_data(node->name),
                       (uint32_t)lwc_string_length(node->name), name);
}

const char *nsbk_dom_attribute(const nsbk_dom_node_t *node,
                               const char *name) {
    uint32_t i;
    if (!node || node->type != NSBK_DOM_ELEMENT || !name) return NULL;
    for (i = 0U; i < node->attribute_count; i++) {
        const nsbk_dom_attribute_t *attribute = &node->attributes[i];
        if (attribute->name &&
            ascii_equal(lwc_string_data(attribute->name),
                        (uint32_t)lwc_string_length(attribute->name), name))
            return attribute->value;
    }
    return NULL;
}


static char resource_ascii_lower(char value) {
    if (value >= 'A' && value <= 'Z') return (char)(value + ('a' - 'A'));
    return value;
}

static void resource_copy_range(char *destination, uint32_t capacity,
                                const char *source, uint32_t length) {
    uint32_t copy;
    if (!destination || capacity == 0U) return;
    destination[0] = '\0';
    if (!source) return;
    copy = length < capacity - 1U ? length : capacity - 1U;
    if (copy) memcpy(destination, source, copy);
    destination[copy] = '\0';
}

static bool resource_contains_nocase(const char *text, const char *needle) {
    uint32_t i, j, text_length = 0U, needle_length = 0U;
    if (!text || !needle) return false;
    while (text[text_length]) text_length++;
    while (needle[needle_length]) needle_length++;
    if (needle_length == 0U) return true;
    if (needle_length > text_length) return false;
    for (i = 0U; i + needle_length <= text_length; i++) {
        for (j = 0U; j < needle_length; j++)
            if (resource_ascii_lower(text[i + j]) !=
                resource_ascii_lower(needle[j])) break;
        if (j == needle_length) return true;
    }
    return false;
}

static const char *resource_find_nocase(const char *text, const char *needle) {
    uint32_t i, j, needle_length = 0U;
    if (!text || !needle) return NULL;
    while (needle[needle_length]) needle_length++;
    if (!needle_length) return text;
    for (i = 0U; text[i]; i++) {
        for (j = 0U; j < needle_length && text[i + j]; j++)
            if (resource_ascii_lower(text[i + j]) !=
                resource_ascii_lower(needle[j])) break;
        if (j == needle_length) return text + i;
    }
    return NULL;
}

static uint32_t resource_number(const char *text) {
    uint32_t value = 0U;
    if (!text) return 0U;
    while (*text == ' ' || *text == '\t') text++;
    while (*text >= '0' && *text <= '9') {
        value = value * 10U + (uint32_t)(*text++ - '0');
        if (value > 8192U) return 8192U;
    }
    return value;
}

static bool image_media_matches(const char *media, int32_t viewport_width) {
    const char *property;
    if (!media || !media[0]) return true;
    property = resource_find_nocase(media, "max-width");
    if (property) {
        const char *colon = property;
        uint32_t value;
        while (*colon && *colon != ':') colon++;
        value = *colon ? resource_number(colon + 1) : 0U;
        if (value && viewport_width > (int32_t)value) return false;
    }
    property = resource_find_nocase(media, "min-width");
    if (property) {
        const char *colon = property;
        uint32_t value;
        while (*colon && *colon != ':') colon++;
        value = *colon ? resource_number(colon + 1) : 0U;
        if (value && viewport_width < (int32_t)value) return false;
    }
    return true;
}

static bool image_type_supported(const char *type) {
    if (!type || !type[0]) return true;
    return resource_contains_nocase(type, "image/png") ||
           resource_contains_nocase(type, "image/gif") ||
           resource_contains_nocase(type, "image/bmp") ||
           resource_contains_nocase(type, "image/jpeg") ||
           resource_contains_nocase(type, "image/jpg");
}

static int32_t responsive_target_width(const nsbk_dom_node_t *node,
                                       int32_t viewport_width) {
    const char *sizes = nsbk_dom_attribute(node, "sizes");
    const char *width = nsbk_dom_attribute(node, "width");
    uint32_t value;
    if (width && (value = resource_number(width)) != 0U)
        return (int32_t)value;
    if (sizes && sizes[0]) {
        const char *cursor = sizes;
        const char *last = sizes;
        while (*cursor) {
            if (*cursor == ',') last = cursor + 1;
            cursor++;
        }
        while (*last == ' ' || *last == '\t') last++;
        value = resource_number(last);
        if (value) {
            const char *unit = last;
            while (*unit >= '0' && *unit <= '9') unit++;
            if (resource_ascii_lower(unit[0]) == 'v' &&
                resource_ascii_lower(unit[1]) == 'w')
                return viewport_width * (int32_t)value / 100;
            return (int32_t)value;
        }
    }
    return viewport_width > 0 ? viewport_width : 640;
}

static bool choose_srcset_candidate(const char *srcset, int32_t target_width,
                                    char *destination, uint32_t capacity) {
    const char *cursor;
    char fallback[256];
    char best[256];
    uint32_t best_score = 0xffffffffU;
    uint32_t largest_score = 0U;
    bool have_best = false;
    bool have_fallback = false;
    if (!srcset || !srcset[0] || !destination || capacity == 0U) return false;
    fallback[0] = '\0';
    best[0] = '\0';
    cursor = srcset;
    while (*cursor) {
        const char *url_start;
        const char *url_end;
        const char *descriptor;
        uint32_t score = 0U;
        char candidate[256];
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
               *cursor == '\n' || *cursor == ',') cursor++;
        if (!*cursor) break;
        url_start = cursor;
        while (*cursor && *cursor != ',' && *cursor != ' ' &&
               *cursor != '\t' && *cursor != '\r' && *cursor != '\n') cursor++;
        url_end = cursor;
        resource_copy_range(candidate, sizeof(candidate), url_start,
                            (uint32_t)(url_end - url_start));
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
               *cursor == '\n') cursor++;
        descriptor = cursor;
        while (*cursor && *cursor != ',') cursor++;
        if (candidate[0]) {
            uint32_t number = resource_number(descriptor);
            const char *unit = descriptor;
            while (*unit == ' ' || *unit == '\t') unit++;
            while (*unit >= '0' && *unit <= '9') unit++;
            if (*unit == '.') {
                while (*unit && *unit != 'x' && *unit != 'X' &&
                       *unit != 'w' && *unit != 'W' && *unit != ',') unit++;
            }
            if (*unit == 'x' || *unit == 'X')
                score = (number ? number : 1U) *
                        (uint32_t)(target_width > 0 ? target_width : 640);
            else if (*unit == 'w' || *unit == 'W') score = number;
            else score = (uint32_t)(target_width > 0 ? target_width : 640);
            if (!have_fallback) {
                resource_copy_range(fallback, sizeof(fallback), candidate,
                                    (uint32_t)strlen(candidate));
                have_fallback = true;
            }
            if (score >= (uint32_t)target_width && score < best_score) {
                resource_copy_range(best, sizeof(best), candidate,
                                    (uint32_t)strlen(candidate));
                best_score = score;
                have_best = true;
            } else if (!have_best && score >= largest_score) {
                resource_copy_range(best, sizeof(best), candidate,
                                    (uint32_t)strlen(candidate));
                largest_score = score;
            }
        }
        if (*cursor == ',') cursor++;
    }
    if (best[0]) {
        resource_copy_range(destination, capacity, best,
                            (uint32_t)strlen(best));
        return true;
    }
    if (have_fallback) {
        resource_copy_range(destination, capacity, fallback,
                            (uint32_t)strlen(fallback));
        return true;
    }
    return false;
}

bool nsbk_dom_image_source(const nsbk_dom_node_t *node, int32_t viewport_width,
                           char *destination, uint32_t capacity) {
    const char *source = NULL;
    const char *srcset;
    int32_t target;
    if (!destination || capacity == 0U) return false;
    destination[0] = '\0';
    if (!node || node->type != NSBK_DOM_ELEMENT) return false;
    target = responsive_target_width(node, viewport_width);
    if (nsbk_dom_name_is(node, "img") && node->parent &&
        nsbk_dom_name_is(node->parent, "picture")) {
        const nsbk_dom_node_t *candidate;
        for (candidate = node->parent->first_child; candidate && candidate != node;
             candidate = candidate->next) {
            if (candidate->type != NSBK_DOM_ELEMENT ||
                !nsbk_dom_name_is(candidate, "source")) continue;
            if (!image_media_matches(nsbk_dom_attribute(candidate, "media"),
                                     viewport_width) ||
                !image_type_supported(nsbk_dom_attribute(candidate, "type")))
                continue;
            srcset = nsbk_dom_attribute(candidate, "srcset");
            if (choose_srcset_candidate(srcset, target, destination, capacity))
                return true;
            source = nsbk_dom_attribute(candidate, "src");
            if (source && source[0]) {
                resource_copy_range(destination, capacity, source,
                                    (uint32_t)strlen(source));
                return true;
            }
        }
    }
    srcset = nsbk_dom_attribute(node, "srcset");
    if (choose_srcset_candidate(srcset, target, destination, capacity))
        return true;
    if (nsbk_dom_name_is(node, "video")) source = nsbk_dom_attribute(node, "poster");
    else source = nsbk_dom_attribute(node, "src");
    if (!source || !source[0]) source = nsbk_dom_attribute(node, "data-src");
    if (!source || !source[0]) source = nsbk_dom_attribute(node, "data-original");
    if (!source || !source[0]) source = nsbk_dom_attribute(node, "data-lazy-src");
    if (!source || !source[0]) return false;
    resource_copy_range(destination, capacity, source, (uint32_t)strlen(source));
    return destination[0] != '\0';
}

typedef struct {
    nsbk_html_result_t *result;
    bool pending_space;
    bool in_title;
    bool in_pre;
    uint32_t title_length;
    uint32_t active_link;
    uint32_t active_link_length;
    uint32_t active_form;
} render_context_t;

static void emit_newline(render_context_t *context) {
    nsbk_html_result_t *result = context->result;
    if (!result || !result->text || result->text_capacity == 0U) return;
    while (result->text_length > 0U &&
           result->text[result->text_length - 1U] == ' ')
        result->text_length--;
    if (result->text_length == 0U ||
        result->text[result->text_length - 1U] == '\n') {
        context->pending_space = false;
        return;
    }
    if (result->text_length + 1U < result->text_capacity)
        result->text[result->text_length++] = '\n';
    else
        result->truncated = true;
    context->pending_space = false;
}

static void emit_byte(render_context_t *context, char character) {
    nsbk_html_result_t *result = context->result;
    nsbk_html_link_t *link = NULL;
    bool emitted_space = false;
    if (!result || !result->text || result->text_capacity == 0U) return;
    if (context->active_link < result->link_count)
        link = &result->links[context->active_link];
    if (!context->in_pre && (character == '\r' || character == '\n' ||
        character == '\t' || character == '\f' || character == ' ')) {
        context->pending_space = true;
        return;
    }
    if (context->in_pre && character == '\r') return;
    if (context->in_pre && character == '\n') {
        emit_newline(context);
        return;
    }
    if (context->pending_space && result->text_length > 0U &&
        result->text[result->text_length - 1U] != '\n') {
        if (result->text_length + 1U < result->text_capacity) {
            result->text[result->text_length++] = ' ';
            emitted_space = true;
        } else {
            result->truncated = true;
        }
    }
    if (emitted_space && context->in_title && context->title_length > 0U &&
        context->title_length + 1U < sizeof(result->title)) {
        result->title[context->title_length++] = ' ';
        result->title[context->title_length] = '\0';
    }
    if (emitted_space && link && context->active_link_length > 0U &&
        context->active_link_length + 1U < sizeof(link->text)) {
        link->text[context->active_link_length++] = ' ';
        link->text[context->active_link_length] = '\0';
    }
    context->pending_space = false;
    if (result->text_length + 1U < result->text_capacity)
        result->text[result->text_length++] = character;
    else
        result->truncated = true;
    if (context->in_title &&
        context->title_length + 1U < sizeof(result->title)) {
        result->title[context->title_length++] = character;
        result->title[context->title_length] = '\0';
    }
    if (link && context->active_link_length + 1U < sizeof(link->text)) {
        link->text[context->active_link_length++] = character;
        link->text[context->active_link_length] = '\0';
    }
}

static void emit_text(render_context_t *context, const char *text) {
    uint32_t i = 0U;
    while (text && text[i]) emit_byte(context, text[i++]);
}

static bool block_element(const nsbk_dom_node_t *node) {
    static const char *names[] = {
        "address", "article", "aside", "blockquote", "body", "div",
        "dl", "dt", "dd", "fieldset", "figcaption", "figure", "footer",
        "form", "h1", "h2", "h3", "h4", "h5", "h6", "header", "hr",
        "li", "main", "nav", "ol", "p", "pre", "section", "table",
        "tbody", "td", "tfoot", "th", "thead", "tr", "ul"
    };
    uint32_t i;
    for (i = 0U; i < sizeof(names) / sizeof(names[0]); i++)
        if (nsbk_dom_name_is(node, names[i])) return true;
    return false;
}

static void copy_limited(char *destination, uint32_t capacity,
                         const char *source) {
    uint32_t i = 0U;
    if (!destination || capacity == 0U) return;
    while (source && source[i] && i + 1U < capacity) {
        destination[i] = source[i];
        i++;
    }
    destination[i] = '\0';
}

static char resource_lower(char character) {
    return character >= 'A' && character <= 'Z' ?
           (char)(character + ('a' - 'A')) : character;
}

static bool token_list_contains(const char *value, const char *wanted) {
    uint32_t i = 0U;
    uint32_t wanted_length = 0U;
    if (!value || !wanted) return false;
    while (wanted[wanted_length]) wanted_length++;
    while (value[i]) {
        uint32_t start;
        uint32_t length;
        while (value[i] == ' ' || value[i] == '\t' || value[i] == '\r' ||
               value[i] == '\n' || value[i] == '\f') i++;
        start = i;
        while (value[i] && value[i] != ' ' && value[i] != '\t' &&
               value[i] != '\r' && value[i] != '\n' && value[i] != '\f') i++;
        length = i - start;
        if (length == wanted_length) {
            uint32_t j;
            for (j = 0U; j < length; j++)
                if (resource_lower(value[start + j]) !=
                    resource_lower(wanted[j])) break;
            if (j == length) return true;
        }
    }
    return false;
}

static uint16_t dimension_hint(const char *value) {
    uint32_t number = 0U;
    if (!value || value[0] < '0' || value[0] > '9') return 0U;
    while (*value >= '0' && *value <= '9') {
        number = number * 10U + (uint32_t)(*value++ - '0');
        if (number > 4096U) return 4096U;
    }
    return (uint16_t)number;
}

static bool stylesheet_known(const nsbk_html_result_t *result,
                             const char *href) {
    uint32_t i;
    for (i = 0U; i < result->stylesheet_ref_count; i++)
        if (strcmp(result->stylesheet_refs[i].href, href) == 0) return true;
    return false;
}

static bool image_known(const nsbk_html_result_t *result, const char *src) {
    uint32_t i;
    for (i = 0U; i < result->image_ref_count; i++)
        if (strcmp(result->image_refs[i].src, src) == 0) return true;
    return false;
}

static bool text_equal_nocase(const char *left, const char *right);

static void collect_resource_reference(render_context_t *context,
                                       nsbk_dom_node_t *node) {
    nsbk_html_result_t *result;
    if (!context || !node || node->type != NSBK_DOM_ELEMENT) return;
    result = context->result;
    if (nsbk_dom_name_is(node, "base") && !result->base_href[0]) {
        const char *href = nsbk_dom_attribute(node, "href");
        if (href && href[0]) copy_limited(result->base_href,
                                         sizeof(result->base_href), href);
    } else if (nsbk_dom_name_is(node, "link")) {
        const char *rel = nsbk_dom_attribute(node, "rel");
        const char *href = nsbk_dom_attribute(node, "href");
        const char *media = nsbk_dom_attribute(node, "media");
        const char *as = nsbk_dom_attribute(node, "as");
        bool preload_style = rel && token_list_contains(rel, "preload") &&
                             as && text_equal_nocase(as, "style");
        bool stylesheet = rel && (token_list_contains(rel, "stylesheet") ||
                                  preload_style);
        bool alternate = rel && token_list_contains(rel, "alternate");
        bool image_preload = rel && token_list_contains(rel, "preload") &&
                             as && text_equal_nocase(as, "image");
        bool media_ok = true;
        if (media && media[0]) {
            bool explicitly_screen = token_list_contains(media, "screen") ||
                                     token_list_contains(media, "all");
            bool explicitly_other = token_list_contains(media, "print") ||
                                    token_list_contains(media, "speech");
            media_ok = (!explicitly_other || explicitly_screen) &&
                       image_media_matches(media, result->viewport_width);
        }
        if (href && href[0] && stylesheet && media_ok && !alternate &&
            result->stylesheet_ref_count < NSBK_HTML_STYLESHEET_MAX &&
            !stylesheet_known(result, href)) {
            nsbk_html_stylesheet_ref_t *style_ref =
                &result->stylesheet_refs[result->stylesheet_ref_count++];
            memset(style_ref, 0, sizeof(*style_ref));
            copy_limited(style_ref->href, sizeof(style_ref->href), href);
            copy_limited(style_ref->media, sizeof(style_ref->media),
                         media && media[0] ? media : "all");
            style_ref->alternate = alternate;
            style_ref->preload = preload_style;
        } else if (href && href[0] && image_preload &&
                   result->image_ref_count < NSBK_HTML_IMAGE_MAX &&
                   !image_known(result, href)) {
            nsbk_html_image_ref_t *image =
                &result->image_refs[result->image_ref_count++];
            memset(image, 0, sizeof(*image));
            copy_limited(image->src, sizeof(image->src), href);
        }
    } else if (nsbk_dom_name_is(node, "img") ||
               nsbk_dom_name_is(node, "video") ||
               (nsbk_dom_name_is(node, "input") &&
                nsbk_dom_attribute(node, "type") &&
                text_equal_nocase(nsbk_dom_attribute(node, "type"), "image"))) {
        char selected[NSBK_HTML_IMAGE_SOURCE_MAX];
        if (nsbk_dom_image_source(node, result->viewport_width,
                                  selected, sizeof(selected)) &&
            result->image_ref_count < NSBK_HTML_IMAGE_MAX &&
            !image_known(result, selected)) {
            nsbk_html_image_ref_t *image =
                &result->image_refs[result->image_ref_count++];
            memset(image, 0, sizeof(*image));
            copy_limited(image->src, sizeof(image->src), selected);
            copy_limited(image->alt, sizeof(image->alt),
                         nsbk_dom_attribute(node, "alt"));
            image->width_hint = dimension_hint(nsbk_dom_attribute(node, "width"));
            image->height_hint = dimension_hint(nsbk_dom_attribute(node, "height"));
        }
    }
}

static bool text_equal_nocase(const char *left, const char *right) {
    uint32_t i = 0U;
    if (!left || !right) return false;
    while (left[i] && right[i]) {
        if (resource_lower(left[i]) != resource_lower(right[i])) return false;
        i++;
    }
    return left[i] == '\0' && right[i] == '\0';
}

static void node_text_append(const nsbk_dom_node_t *node, char *destination,
                             uint32_t capacity, uint32_t *used) {
    const nsbk_dom_node_t *child;
    uint32_t i;
    if (!node || !destination || !used || *used + 1U >= capacity) return;
    if (node->type == NSBK_DOM_TEXT) {
        for (i = 0U; i < node->text_length && *used + 1U < capacity; i++) {
            char c = node->text[i];
            if (c == '\r' || c == '\n' || c == '\t' || c == '\f') c = ' ';
            if (c == ' ' && (*used == 0U || destination[*used - 1U] == ' '))
                continue;
            destination[(*used)++] = c;
        }
        destination[*used] = '\0';
        return;
    }
    for (child = node->first_child; child; child = child->next)
        node_text_append(child, destination, capacity, used);
}

static void node_text_copy(const nsbk_dom_node_t *node, char *destination,
                           uint32_t capacity) {
    uint32_t used = 0U;
    if (!destination || capacity == 0U) return;
    destination[0] = '\0';
    node_text_append(node, destination, capacity, &used);
    while (used > 0U && destination[used - 1U] == ' ') used--;
    destination[used] = '\0';
}

static uint32_t begin_form(render_context_t *context, nsbk_dom_node_t *node) {
    nsbk_html_result_t *result;
    nsbk_html_form_t *form;
    const char *method;
    if (!context || !node) return NSBK_HTML_FORM_MAX;
    result = context->result;
    if (result->form_count >= NSBK_HTML_FORM_MAX) return NSBK_HTML_FORM_MAX;
    form = &result->forms[result->form_count];
    memset(form, 0, sizeof(*form));
    copy_limited(form->action, sizeof(form->action),
                 nsbk_dom_attribute(node, "action"));
    method = nsbk_dom_attribute(node, "method");
    copy_limited(form->method, sizeof(form->method),
                 method && text_equal_nocase(method, "post") ? "POST" : "GET");
    form->first_control = (uint16_t)result->control_count;
    form->node = node;
    return result->form_count++;
}

static uint8_t input_control_type(const nsbk_dom_node_t *node) {
    const char *type = nsbk_dom_attribute(node, "type");
    if (!type || !type[0] || text_equal_nocase(type, "text"))
        return NSBK_CONTROL_TEXT;
    if (text_equal_nocase(type, "search")) return NSBK_CONTROL_SEARCH;
    if (text_equal_nocase(type, "password")) return NSBK_CONTROL_PASSWORD;
    if (text_equal_nocase(type, "hidden")) return NSBK_CONTROL_HIDDEN;
    if (text_equal_nocase(type, "submit") || text_equal_nocase(type, "image"))
        return NSBK_CONTROL_SUBMIT;
    if (text_equal_nocase(type, "button") || text_equal_nocase(type, "reset"))
        return NSBK_CONTROL_BUTTON;
    if (text_equal_nocase(type, "checkbox")) return NSBK_CONTROL_CHECKBOX;
    if (text_equal_nocase(type, "radio")) return NSBK_CONTROL_RADIO;
    return NSBK_CONTROL_TEXT;
}

static void collect_select_options(nsbk_html_result_t *result,
                                   nsbk_html_control_t *control,
                                   nsbk_dom_node_t *node) {
    nsbk_dom_node_t *child;
    if (!result || !control || !node) return;
    for (child = node->first_child; child; child = child->next) {
        if (child->type == NSBK_DOM_ELEMENT && nsbk_dom_name_is(child, "option")) {
            nsbk_html_option_t *option;
            const char *value;
            if (result->option_count >= NSBK_HTML_OPTION_MAX) return;
            option = &result->options[result->option_count];
            memset(option, 0, sizeof(*option));
            node_text_copy(child, option->label, sizeof(option->label));
            value = nsbk_dom_attribute(child, "value");
            copy_limited(option->value, sizeof(option->value),
                         value && value[0] ? value : option->label);
            option->disabled = nsbk_dom_attribute(child, "disabled") != NULL;
            if (control->option_count == 0U)
                control->first_option = (uint16_t)result->option_count;
            if (nsbk_dom_attribute(child, "selected") != NULL)
                control->selected_option = control->option_count;
            control->option_count++;
            result->option_count++;
        } else {
            collect_select_options(result, control, child);
        }
    }
    if (control->option_count) {
        uint32_t selected = control->first_option + control->selected_option;
        if (selected < result->option_count) {
            copy_limited(control->value, sizeof(control->value),
                         result->options[selected].value);
            copy_limited(control->label, sizeof(control->label),
                         result->options[selected].label);
        }
    }
}

static void collect_form_control(render_context_t *context,
                                 nsbk_dom_node_t *node) {
    nsbk_html_result_t *result;
    nsbk_html_control_t *control;
    uint8_t type;
    if (!context || !node || context->active_form >= NSBK_HTML_FORM_MAX)
        return;
    result = context->result;
    if (context->active_form >= result->form_count ||
        result->control_count >= NSBK_HTML_CONTROL_MAX) return;
    if (nsbk_dom_name_is(node, "input")) type = input_control_type(node);
    else if (nsbk_dom_name_is(node, "textarea")) type = NSBK_CONTROL_TEXTAREA;
    else if (nsbk_dom_name_is(node, "button")) {
        const char *button_type = nsbk_dom_attribute(node, "type");
        type = button_type && text_equal_nocase(button_type, "button") ?
               NSBK_CONTROL_BUTTON : NSBK_CONTROL_SUBMIT;
    } else if (nsbk_dom_name_is(node, "select")) type = NSBK_CONTROL_SELECT;
    else return;
    control = &result->controls[result->control_count];
    memset(control, 0, sizeof(*control));
    control->type = type;
    control->form_index = (uint16_t)context->active_form;
    control->node = node;
    control->disabled = nsbk_dom_attribute(node, "disabled") != NULL;
    control->checked = nsbk_dom_attribute(node, "checked") != NULL;
    copy_limited(control->name, sizeof(control->name),
                 nsbk_dom_attribute(node, "name"));
    copy_limited(control->placeholder, sizeof(control->placeholder),
                 nsbk_dom_attribute(node, "placeholder"));
    if (type == NSBK_CONTROL_TEXTAREA) {
        node_text_copy(node, control->value, sizeof(control->value));
    } else if (type == NSBK_CONTROL_SELECT) {
        collect_select_options(result, control, node);
    } else {
        copy_limited(control->value, sizeof(control->value),
                     nsbk_dom_attribute(node, "value"));
    }
    if ((type == NSBK_CONTROL_CHECKBOX || type == NSBK_CONTROL_RADIO) &&
        !control->value[0]) copy_limited(control->value, sizeof(control->value), "on");
    if (type == NSBK_CONTROL_SUBMIT || type == NSBK_CONTROL_BUTTON) {
        copy_limited(control->label, sizeof(control->label), control->value);
        if (!control->label[0]) node_text_copy(node, control->label,
                                               sizeof(control->label));
        if (!control->label[0]) copy_limited(control->label,
                                             sizeof(control->label), "Enviar");
    }
    result->forms[context->active_form].control_count++;
    result->control_count++;
}

static void render_node(render_context_t *context, nsbk_dom_node_t *node) {
    nsbk_dom_node_t *child;
    uint32_t previous_link;
    uint32_t previous_link_length;
    uint32_t previous_form;
    bool previous_title;
    bool previous_pre;
    bool is_block;
    if (!context || !node) return;
    if (node->type == NSBK_DOM_TEXT) {
        uint32_t i;
        for (i = 0U; i < node->text_length; i++)
            emit_byte(context, node->text[i]);
        return;
    }
    if (node->type != NSBK_DOM_ELEMENT && node->type != NSBK_DOM_DOCUMENT) {
        return;
    }
    if (node->type == NSBK_DOM_ELEMENT)
        collect_resource_reference(context, node);
    if (node->type == NSBK_DOM_ELEMENT &&
        (nsbk_dom_name_is(node, "script") ||
         nsbk_dom_name_is(node, "style") ||
         nsbk_dom_name_is(node, "template")))
        return;

    previous_link = context->active_link;
    previous_link_length = context->active_link_length;
    previous_form = context->active_form;
    previous_title = context->in_title;
    previous_pre = context->in_pre;
    is_block = node->type == NSBK_DOM_ELEMENT && block_element(node);
    if (is_block) emit_newline(context);
    if (node->type == NSBK_DOM_ELEMENT && nsbk_dom_name_is(node, "br"))
        emit_newline(context);
    if (node->type == NSBK_DOM_ELEMENT && nsbk_dom_name_is(node, "li"))
        emit_text(context, "* ");
    if (node->type == NSBK_DOM_ELEMENT && nsbk_dom_name_is(node, "title")) {
        context->in_title = true;
        context->title_length = 0U;
        context->result->title[0] = '\0';
    }
    if (node->type == NSBK_DOM_ELEMENT && nsbk_dom_name_is(node, "pre"))
        context->in_pre = true;
    if (node->type == NSBK_DOM_ELEMENT && nsbk_dom_name_is(node, "form"))
        context->active_form = begin_form(context, node);
    if (node->type == NSBK_DOM_ELEMENT)
        collect_form_control(context, node);
    if (node->type == NSBK_DOM_ELEMENT && nsbk_dom_name_is(node, "a")) {
        const char *href = nsbk_dom_attribute(node, "href");
        if (href && href[0] &&
            context->result->link_count < NSBK_HTML_LINK_MAX) {
            context->active_link = context->result->link_count++;
            context->active_link_length = 0U;
            copy_limited(context->result->links[context->active_link].href,
                         sizeof(context->result->links[0].href), href);
            context->result->links[context->active_link].text[0] = '\0';
        }
    }
    if (node->type == NSBK_DOM_ELEMENT && nsbk_dom_name_is(node, "img")) {
        const char *alt = nsbk_dom_attribute(node, "alt");
        if (alt && alt[0]) emit_text(context, alt);
    }
    for (child = node->first_child; child; child = child->next)
        render_node(context, child);
    if (node->type == NSBK_DOM_ELEMENT && nsbk_dom_name_is(node, "a") &&
        context->active_link < context->result->link_count) {
        nsbk_html_link_t *link =
            &context->result->links[context->active_link];
        if (!link->text[0])
            copy_limited(link->text, sizeof(link->text), link->href);
    }
    if (is_block) emit_newline(context);
    context->active_link = previous_link;
    context->active_link_length = previous_link_length;
    context->active_form = previous_form;
    context->in_title = previous_title;
    context->in_pre = previous_pre;
}

void nsbk_dom_render(nsbk_dom_tree_t *tree, nsbk_html_result_t *result) {
    render_context_t context;
    if (!tree || !tree->document || !result || !result->text ||
        result->text_capacity == 0U) return;
    memset(&context, 0, sizeof(context));
    context.result = result;
    context.active_link = NSBK_HTML_LINK_MAX;
    context.active_form = NSBK_HTML_FORM_MAX;
    result->text_length = 0U;
    result->text[0] = '\0';
    result->title[0] = '\0';
    result->base_href[0] = '\0';
    result->link_count = 0U;
    result->stylesheet_ref_count = 0U;
    result->image_ref_count = 0U;
    result->form_count = 0U;
    result->control_count = 0U;
    render_node(&context, tree->document);
    while (result->text_length > 0U &&
           (result->text[result->text_length - 1U] == ' ' ||
            result->text[result->text_length - 1U] == '\n'))
        result->text_length--;
    result->text[result->text_length] = '\0';
}
