/* HTTP File Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>

#include "esp_err.h"
#include "esp_log.h"

#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "esp_http_server.h"

#include "driver/gpio.h"
/* Max length a file path can have on storage */
#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_SPIFFS_OBJ_NAME_LEN)

/* Max size of an individual file. Make sure this
 * value is same as that set in index_off.html */


/* Scratch buffer size */
#define SCRATCH_BUFSIZE  8192

#define BLINK_GPIO 27

struct file_server_data {
    /* Base path of file storage */
    char base_path[ESP_VFS_PATH_MAX + 1];

    /* Scratch buffer for temporary storage during file transfer */
    char scratch[SCRATCH_BUFSIZE];
};

static const char *TAG = "led_server";

/* Handler to redirect incoming GET request for /index_off.html to /
 * This can be overridden by uploading file with same name */
static esp_err_t index_off_html_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "307 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);  // Response body can be empty
    return ESP_OK;
}
static esp_err_t index_on_html_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "307 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);  // Response body can be empty
    return ESP_OK;
}

/* Handler to respond with an icon file embedded in flash.
 * Browsers expect to GET website icon at URI /favicon.ico.
 * This can be overridden by uploading file with same name */


static esp_err_t style_css_get_handler(httpd_req_t *req)
{
    extern const uint8_t style_css_start[] asm("_binary_style_css_start");
    extern const uint8_t style_css_end[]   asm("_binary_style_css_end");   

	httpd_resp_set_type(req, "text/css");

	httpd_resp_send(req, (const char *)style_css_start, (style_css_end-1) - style_css_start);
    return ESP_OK;
}
/* Send HTTP response with a run-time generated html consisting of
 * a list of all files and folders under the requested path.
 * In case of SPIFFS this returns empty list when path is any
 * string other than '/', since SPIFFS doesn't support directories */
static esp_err_t http_resp_dir_html(httpd_req_t *req, const char *dirpath)
{
    char entrypath[FILE_PATH_MAX];      

    DIR *dir = opendir(dirpath);
    
    /* Retrieve the base path of file storage to construct the full path */
    strlcpy(entrypath, dirpath, sizeof(entrypath));

    if (!dir) {
        ESP_LOGE(TAG, "Failed to stat dir : %s", dirpath);
        /* Respond with 404 Not Found */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Directory does not exist");
        return ESP_FAIL;
    }

    /* Send HTML file header */
    httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html><body>");

    /* Get handle to embedded file upload script */
    extern const unsigned char index_off_start[] asm("_binary_index_off_html_start");
    extern const unsigned char index_off_end[]   asm("_binary_index_off_html_end");
    const size_t index_off_size = (index_off_end - index_off_start);   

    /* Add file upload form and script which on execution sends a POST request to /upload */
    httpd_resp_send_chunk(req, (const char *)index_off_start, index_off_size);   

    //Send empty chunk to signal HTTP response completion 
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}



/* Set HTTP response content type according to file extension */


/* Copies the full path into destination buffer and returns
 * pointer to path (skipping the preceding base path) */
static const char* get_path_from_uri(char *dest, const char *base_path, const char *uri, size_t destsize)
{
    const size_t base_pathlen = strlen(base_path);
    size_t pathlen = strlen(uri);

    const char *quest = strchr(uri, '?');
    if (quest) {
        pathlen = MIN(pathlen, quest - uri);
    }
    const char *hash = strchr(uri, '#');
    if (hash) {
        pathlen = MIN(pathlen, hash - uri);
    }

    if (base_pathlen + pathlen + 1 > destsize) {
        /* Full path string won't fit into destination buffer */
        return NULL;
    }

    /* Construct full path (base + path) */
    strcpy(dest, base_path);
    strlcpy(dest + base_pathlen, uri, pathlen + 1);

    /* Return pointer to path, skipping the base */
    return dest + base_pathlen;
}

/* Handler to download a file kept on the server */
static esp_err_t start_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
   
    struct stat file_stat;

    const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                             req->uri, sizeof(filepath));
    if (!filename) {
        ESP_LOGE(TAG, "Filename is too long");
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }    
    
    /* If name has trailing '/', respond with directory contents */
    if (filename[strlen(filename) - 1] == '/') {
        return http_resp_dir_html(req, filepath);
    }

    if (stat(filepath, &file_stat) == -1) {
        /* If file not present on SPIFFS check if URI
         * corresponds to one of the hardcoded paths */
        if (strcmp(filename, "/index_on.html") == 0) {
            return index_on_html_get_handler(req);
        } else if (strcmp(filename, "/index_off.html") == 0) {
            return index_off_html_get_handler(req);        
        } else if (strcmp(filename, "/style.css") == 0) {
            return style_css_get_handler(req);
        }
        ESP_LOGE(TAG, "Failed to stat file : %s", filepath);
        /* Respond with 404 Not Found */
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

static esp_err_t led_on_handler(httpd_req_t *req){
    

    httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html><body>"); 

    extern const unsigned char index_on_start[] asm("_binary_index_on_html_start");
    extern const unsigned char index_on_end[]   asm("_binary_index_on_html_end");
    const size_t index_on_size = (index_on_end - index_on_start);

    httpd_resp_send_chunk(req, (const char *)index_on_start, index_on_size);   

    gpio_set_level(BLINK_GPIO, 1);
    ESP_LOGE(TAG, "Led is ON");
    return ESP_OK;
}
static esp_err_t led_off_handler(httpd_req_t *req){    

    httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html><body>"); 

    extern const unsigned char index_off_start[] asm("_binary_index_off_html_start");
    extern const unsigned char index_off_end[]   asm("_binary_index_off_html_end");
    const size_t index_off_size = (index_off_end - index_off_start);

    httpd_resp_send_chunk(req, (const char *)index_off_start, index_off_size);    
    
    gpio_set_level(BLINK_GPIO, 0);    
    ESP_LOGE(TAG, "Led is OFF");
    return ESP_OK;
}
/* Function to start the file server */
esp_err_t start_file_server(const char *base_path)
{
    static struct file_server_data *server_data = NULL;

    /* Validate file storage base path */
    if (!base_path || strcmp(base_path, "/spiffs") != 0) {
        ESP_LOGE(TAG, "File server presently supports only '/spiffs' as base path");
        return ESP_ERR_INVALID_ARG;
    }

    if (server_data) {
        ESP_LOGE(TAG, "File server already started");
        return ESP_ERR_INVALID_STATE;
    }

    /* Allocate memory for server data */
    server_data = calloc(1, sizeof(struct file_server_data));
    if (!server_data) {
        ESP_LOGE(TAG, "Failed to allocate memory for server data");
        return ESP_ERR_NO_MEM;
    }
    strlcpy(server_data->base_path, base_path,
            sizeof(server_data->base_path));

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    /* Use the URI wildcard matching function in order to
     * allow the same handler to respond to multiple different
     * target URIs which match the wildcard scheme */
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Starting HTTP Server");
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start file server!");
        return ESP_FAIL;
    }
 
    httpd_uri_t led_on = {
        .uri       = "/on",
        .method    = HTTP_GET,
        .handler   = led_on_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &led_on);

    httpd_uri_t led_off = {
        .uri       = "/off",
        .method    = HTTP_GET,
        .handler   = led_off_handler,
        .user_ctx  = NULL
    };
    httpd_register_uri_handler(server, &led_off);

    /* URI handler for getting uploaded files */
    httpd_uri_t start = {
        .uri       = "/*",  // Match all URIs of type /path/to/file
        .method    = HTTP_GET,
        .handler   = start_handler,
        .user_ctx  = server_data    // Pass server data as context
    };
    httpd_register_uri_handler(server, &start);

    return ESP_OK;
}
