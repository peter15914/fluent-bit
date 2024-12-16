/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <fluent-bit.h>
#include <fluent-bit/calyptia/calyptia_constants.h>
#include <fluent-bit/flb_time.h>
#include <fluent-bit/flb_custom.h>

#include <monkey/mk_core.h>
#include <monkey/mk_lib.h>

#include "flb_tests_runtime.h"
#include "../../plugins/in_calyptia_fleet/in_calyptia_fleet.h"

#define MOCK_SERVER_HOST "127.0.0.1"
#define MOCK_SERVER_PORT "9876"

flb_sds_t fleet_config_filename(struct flb_in_calyptia_fleet_config *ctx, char *fname);
int get_calyptia_fleet_config(struct flb_in_calyptia_fleet_config *ctx);

static int tomlRequests = 0;
static int yamlRequests = 0;

/* Test context structure */
struct test_context {
    struct flb_in_calyptia_fleet_config *ctx;
    struct flb_config *config;
};

/* Initialize test context */
static struct test_context *init_test_context()
{
    struct test_context *t_ctx = flb_calloc(1, sizeof(struct test_context));
    if (!t_ctx) {
        return NULL;
    }

    t_ctx->config = flb_config_init();
    if (!t_ctx->config) {
        flb_free(t_ctx);
        return NULL;
    }

    t_ctx->ctx = flb_calloc(1, sizeof(struct flb_in_calyptia_fleet_config));
    if (!t_ctx->ctx) {
        flb_config_exit(t_ctx->config);
        flb_free(t_ctx);
        return NULL;
    }

    /* Initialize plugin instance for logging */
    t_ctx->ctx->ins = flb_calloc(1, sizeof(struct flb_input_instance));
    if (!t_ctx->ctx->ins) {
        flb_free(t_ctx->ctx);
        flb_config_exit(t_ctx->config);
        flb_free(t_ctx);
        return NULL;
    }

    /* Initialize test values in ctx */
    t_ctx->ctx->api_key = flb_strdup("test_api_key");
    t_ctx->ctx->fleet_id = flb_strdup("test_fleet_id");

    t_ctx->ctx->fleet_name = flb_strdup("test_fleet");
    t_ctx->ctx->machine_id = flb_strdup("test_machine_id");

    char mock_url[256] = {0};
    snprintf(mock_url, sizeof(mock_url) - 1, "%s:%s", MOCK_SERVER_HOST, MOCK_SERVER_PORT);
    t_ctx->ctx->fleet_files_url = flb_strdup(mock_url);

    t_ctx->ctx->fleet_config_legacy_format = FLB_TRUE;

    return t_ctx;
}

static void cleanup_test_context(struct test_context *t_ctx)
{
    if (!t_ctx) {
        return;
    }

    if (t_ctx->ctx) {
        if (t_ctx->ctx->api_key) flb_free(t_ctx->ctx->api_key);
        if (t_ctx->ctx->fleet_id) flb_free(t_ctx->ctx->fleet_id);

        if (t_ctx->ctx->fleet_name) flb_free(t_ctx->ctx->fleet_name);
        if (t_ctx->ctx->machine_id) flb_free(t_ctx->ctx->machine_id);
        if (t_ctx->ctx->fleet_files_url) flb_free(t_ctx->ctx->fleet_files_url);

        if (t_ctx->ctx->ins) flb_free(t_ctx->ctx->ins);
        flb_free(t_ctx->ctx);
    }

    if (t_ctx->config) {
        /* Destroy the config which will cleanup any remaining instances */
        flb_config_exit(t_ctx->config);
    }

    flb_free(t_ctx);
}

static void test_in_fleet_format() {
    struct test_context *t_ctx = init_test_context();
    TEST_CHECK(t_ctx != NULL);

    /* Ensure we create TOML files by default */
    char expectedValue[CALYPTIA_MAX_DIR_SIZE];
    int ret = sprintf(expectedValue, "%s/%s/%s/test.conf", FLEET_DEFAULT_CONFIG_DIR, t_ctx->ctx->machine_id, t_ctx->ctx->fleet_name);
    TEST_CHECK(ret > 0);

    flb_sds_t value = fleet_config_filename( t_ctx->ctx, "test" );
    TEST_CHECK(value != NULL);
    TEST_MSG("fleet_config_filename expected=%s got=%s", expectedValue, value);
    TEST_CHECK(value && strcmp(value, expectedValue) == 0);
    flb_sds_destroy(value);
    value = NULL;

    /* Ensure we create YAML files if configured to do so */
    t_ctx->ctx->fleet_config_legacy_format = FLB_FALSE;

    ret = sprintf(expectedValue, "%s/%s/%s/test.yaml", FLEET_DEFAULT_CONFIG_DIR, t_ctx->ctx->machine_id, t_ctx->ctx->fleet_name);
    TEST_CHECK(ret > 0);

    value = fleet_config_filename( t_ctx->ctx, "test" );
    TEST_CHECK(value != NULL);
    TEST_MSG("fleet_config_filename expected=%s got=%s", expectedValue, value);
    TEST_CHECK(value && strcmp(value, expectedValue) == 0);
    flb_sds_destroy(value);
    value = NULL;

    cleanup_test_context(t_ctx);
}

static void mock_server_fleet_files_toml(mk_request_t *request, void *data)
{
    tomlRequests++;
    /* Use a local buffer with correct size */
    char *response = "{\"id\":\"test_fleet_id\"}";
    size_t response_len = strlen(response);

    mk_http_status(request, 200);
    mk_http_header(request, "Content-Type", sizeof("Content-Type") - 1,
                    "application/json", sizeof("application/json") - 1);
    mk_http_send(request, response, response_len, NULL);
    mk_http_done(request);
}

static void mock_server_fleet_files_yaml(mk_request_t *request, void *data)
{
    yamlRequests++;
    /* Use a local buffer with correct size */
    char *response = "{\"id\":\"test_fleet_id\"}";
    size_t response_len = strlen(response);

    mk_http_status(request, 200);
    mk_http_header(request, "Content-Type", sizeof("Content-Type") - 1,
                    "application/json", sizeof("application/json") - 1);
    mk_http_send(request, response, response_len, NULL);
    mk_http_done(request);
}

static void mock_server_registration(mk_request_t *request, void *data)
{
    /* Use a local buffer with correct size */
    const char *response = "{\"id\":\"test_fleet_id\"}";
    size_t response_len = strlen(response); // Ensure size is accurate

    mk_http_status(request, 200);
    mk_http_header(request, "Content-Type", sizeof("Content-Type") - 1,
                    "application/json", sizeof("application/json") - 1);
    mk_http_send(request, response, response_len, NULL); // Use response_len
    mk_http_done(request);
}


static void test_in_fleet_get_calyptia_files() {
    char tmp[256] = {0};
    struct test_context *t_ctx = init_test_context();
    TEST_CHECK(t_ctx != NULL);

    /* Init mock server */
    mk_ctx_t *mock_ctx = mk_create();
    TEST_CHECK(mock_ctx != NULL);

    /* Compose listen address */
    snprintf(tmp, sizeof(tmp) - 1, "%s:%s", MOCK_SERVER_HOST, MOCK_SERVER_PORT);
    int ret = mk_config_set(mock_ctx, "Listen", tmp, NULL);
    TEST_CHECK(ret == 0);

    int vid = mk_vhost_create(mock_ctx, NULL);
    TEST_CHECK(vid >= 0);

    sprintf(tmp, CALYPTIA_ENDPOINT_FLEET_CONFIG_INI, t_ctx->ctx->fleet_id);
    ret = mk_vhost_handler(mock_ctx, vid, tmp, mock_server_fleet_files_toml, NULL);
    TEST_CHECK(ret == 0);

    sprintf(tmp, CALYPTIA_ENDPOINT_FLEET_CONFIG_YAML, t_ctx->ctx->fleet_id);
    ret = mk_vhost_handler(mock_ctx, vid, tmp, mock_server_fleet_files_yaml, NULL);
    TEST_CHECK(ret == 0);

    ret = mk_vhost_handler(mock_ctx, vid, "/v1/agents", mock_server_registration, NULL);
    TEST_CHECK(ret == 0);

    ret = mk_vhost_handler(mock_ctx, vid, "/v1/agents/test_fleet_id", mock_server_registration, NULL);
    TEST_CHECK(ret == 0);

    ret = mk_start(mock_ctx);
    TEST_CHECK(ret == 0);

    /* Allow the mock server to initialize */
    flb_time_msleep(500);

    tomlRequests = 0;
    yamlRequests = 0;

    /* Init Fluent Bit context */
    flb_ctx_t *ctx = flb_create();
    TEST_CHECK(ctx != NULL);

    ret = flb_service_set(ctx,
                          "Log_Level", "debug",
                          NULL);
    TEST_CHECK(ret == 0);

    /* Create dummy input */
    int in_ffd = flb_input(ctx, (char *)"dummy", NULL);
    TEST_CHECK(in_ffd >= 0);

    /* Create custom Calyptia plugin */
    struct flb_custom_instance *calyptia = flb_custom_new(ctx->config, (char *)"calyptia", NULL);
    TEST_CHECK(calyptia != NULL);

    /* Set custom plugin properties */
    flb_custom_set_property(calyptia, "api_key", "test-key");
    flb_custom_set_property(calyptia, "log_level", "debug");
    flb_custom_set_property(calyptia, "calyptia_host", MOCK_SERVER_HOST);
    flb_custom_set_property(calyptia, "calyptia_port", MOCK_SERVER_PORT);
    flb_custom_set_property(calyptia, "calyptia_tls", "off");
    flb_custom_set_property(calyptia, "calyptia_tls.verify", "off");

    /* Start the engine */
    ret = flb_start(ctx);
    TEST_CHECK(ret == 0);

    /* TOML */
    ret = get_calyptia_fleet_config(t_ctx->ctx);
    TEST_CHECK(ret == 0);
    TEST_CHECK(tomlRequests == 1);
    TEST_CHECK(yamlRequests == 0);

    /* YAML */
    t_ctx->ctx->fleet_config_legacy_format = FLB_FALSE;
    ret = get_calyptia_fleet_config(t_ctx->ctx);
    TEST_CHECK(ret == 0);
    TEST_CHECK(tomlRequests == 1);
    TEST_CHECK(yamlRequests == 1);

    /* Cleanup */
    flb_stop(ctx);
    flb_destroy(ctx);
    mk_stop(mock_ctx);
    mk_destroy(mock_ctx);

    cleanup_test_context(t_ctx);
}

/* Define test list */
TEST_LIST = {
    {"in_calyptia_fleet_format", test_in_fleet_format},
    {"in_calyptia_fleet_get_files", test_in_fleet_get_calyptia_files},
    {NULL, NULL}
};