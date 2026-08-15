#ifndef BLESKERNOS_NETSURF_PLATFORM_H
#define BLESKERNOS_NETSURF_PLATFORM_H

#include <bleskernos_api.h>
#include "html_parser.h"
#include "layout.h"

#define NSBK_NETWORK_BUFFER_SIZE 65536U
#define NSBK_RESOURCE_BUFFER_SIZE 81920U
#define NSBK_TEXT_BUFFER_SIZE      49152U
#define NSBK_DECODE_BUFFER_SIZE    81920U
#define NSBK_COOKIE_MAX              16U
#define NSBK_COOKIE_NAME_MAX         48U
#define NSBK_COOKIE_VALUE_MAX       128U
#define NSBK_COOKIE_DOMAIN_MAX       96U
#define NSBK_COOKIE_PATH_MAX         96U
#define NSBK_URL_MAX               1024U
#define NSBK_CONTENT_TYPE_MAX        64U
#define NSBK_CSS_RESOURCE_MAX NSBK_HTML_STYLESHEET_MAX
#define NSBK_IMAGE_RESOURCE_MAX NSBK_HTML_IMAGE_MAX

typedef struct {
    char name[NSBK_COOKIE_NAME_MAX];
    char value[NSBK_COOKIE_VALUE_MAX];
    char domain[NSBK_COOKIE_DOMAIN_MAX];
    char path[NSBK_COOKIE_PATH_MAX];
    bool secure;
    bool host_only;
} nsbk_cookie_t;

typedef struct {
    char reference[NSBK_URL_MAX];
    char url[NSBK_URL_MAX];
    char media[NSBK_HTML_MEDIA_MAX];
    uint8_t *data;
    uint32_t length;
    uint16_t http_status;
    bool imported;
    bool gzip;
    void *compiled;
} nsbk_loaded_css_t;

typedef struct {
    char reference[NSBK_HTML_IMAGE_SOURCE_MAX];
    char url[NSBK_URL_MAX];
    bk_gui_image_t image;
} nsbk_loaded_image_t;

typedef void (*nsbk_progress_callback_t)(void *context, uint8_t percent,
                                         const char *phase);

typedef struct {
    char *network_data;
    char *resource_data;
    char *decode_data;
    uint32_t network_length;
    uint32_t body_offset;
    uint32_t body_length;
    char *text_data;
    void *workspace;
    uint32_t text_length;
    int http_status;
    int32_t tls_error;
    uint8_t redirect_count;
    bool secure;
    bool chunked;
    bool truncated;
    bool parsed_with_hubbub;
    uint32_t parse_errors;
    uint32_t node_count;
    uint32_t element_count;
    uint32_t quirks_mode;
    bool built_dom;
    char title[NSBK_HTML_TITLE_MAX];
    char charset[32];
    char content_type[NSBK_CONTENT_TYPE_MAX];
    char final_url[NSBK_URL_MAX];
    char base_url[NSBK_URL_MAX];
    char error[128];
    nsbk_html_link_t links[NSBK_HTML_LINK_MAX];
    uint32_t link_count;
    nsbk_html_form_t forms[NSBK_HTML_FORM_MAX];
    uint32_t form_count;
    nsbk_html_control_t controls[NSBK_HTML_CONTROL_MAX];
    uint32_t control_count;
    nsbk_html_option_t options[NSBK_HTML_OPTION_MAX];
    uint32_t option_count;
    nsbk_loaded_css_t stylesheets[NSBK_CSS_RESOURCE_MAX];
    uint32_t stylesheet_count;
    uint32_t stylesheet_discovered_count;
    uint32_t stylesheet_failure_count;
    uint32_t stylesheet_truncated_count;
    uint32_t stylesheet_rejected_count;
    nsbk_loaded_image_t images[NSBK_IMAGE_RESOURCE_MAX];
    uint32_t image_count;
    uint32_t image_failures;
    nsbk_cookie_t cookies[NSBK_COOKIE_MAX];
    uint32_t cookie_count;
    bool content_gzip;
    bool allow_stylesheets;
    bool allow_images;
    bool allow_cookies;
    bool session_registered;
    uint32_t timing_total_ms;
    uint32_t timing_network_ms;
    uint32_t timing_html_ms;
    uint32_t timing_css_ms;
    uint32_t timing_images_ms;
    uint32_t timing_layout_ms;
    uint32_t network_request_count;
    uint32_t cache_hit_count;
    nsbk_progress_callback_t progress_callback;
    void *progress_context;
    volatile bool *cancel_flag;
    int32_t viewport_width;
    nsbk_layout_t layout;
} nsbk_document_t;

bool nsbk_document_init(nsbk_document_t *document);
void nsbk_document_destroy(nsbk_document_t *document);
void nsbk_document_set_progress(nsbk_document_t *document,
                                nsbk_progress_callback_t callback,
                                void *context);
void nsbk_document_set_cancel_flag(nsbk_document_t *document,
                                   volatile bool *cancel_flag);
void nsbk_document_set_preferences(nsbk_document_t *document,
                                   bool stylesheets, bool images,
                                   bool cookies);
bool nsbk_document_fetch(nsbk_document_t *document, const char *url,
                         uint32_t timeout_ms, int32_t viewport_width);
bool nsbk_document_fetch_request(nsbk_document_t *document, const char *url,
                                 const char *method, const char *body,
                                 uint32_t body_length, uint32_t timeout_ms,
                                 int32_t viewport_width);
bool nsbk_document_reflow(nsbk_document_t *document, int32_t viewport_width);
bool nsbk_form_set_value(nsbk_document_t *document, uint32_t control_index,
                         const char *value);
bool nsbk_form_toggle(nsbk_document_t *document, uint32_t control_index);
bool nsbk_form_select_next(nsbk_document_t *document, uint32_t control_index,
                           int direction);
bool nsbk_form_build_submission(nsbk_document_t *document,
                                uint32_t form_index,
                                uint32_t submit_control,
                                char *url, uint32_t url_capacity,
                                char *method, uint32_t method_capacity,
                                char *body, uint32_t body_capacity,
                                uint32_t *body_length);
void nsbk_url_normalize(char *destination, uint32_t capacity,
                        const char *source);
void nsbk_url_resolve(char *destination, uint32_t capacity,
                      const char *base, const char *reference);
uint32_t nsbk_text_line_count(const char *text, uint32_t length,
                              uint32_t columns);

#endif
