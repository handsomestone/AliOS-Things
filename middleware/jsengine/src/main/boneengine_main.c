/*
 * Copyright (C) 2015-2019 Alibaba Group Holding Limited
 */

/*文件说明:
  功能：本文件是一个TinyEngine sdk sample例程，用于说明如何使用TinyEngine
  SDK的API
       同时将TinyEngine静态库链接进不同平台的SDK，编译出可执行ELF文件用于测试。
  版本: v0.0.2
  时间：2018.08.27
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "be_jse_task.h"
#include "be_log.h"
#include "be_port_osal.h"
#include "board_mgr.h"

#include "cJSON.h"
#include "hal/system.h"

#include "be_jse_export.h"
#include "be_utils.h"

#ifdef JSE_CLI_DEBUG
#include "cli.h"
#endif

#ifdef JSE_IDE_DEBUG
#include "be_service.h"
#endif

#define TAG "tiny-engine-main"

#define APP_PACKAGE_FILE_NAME BE_FS_ROOT_DIR"/package.json"
#define MAX_FILE_NAME_LEN 127
static char js_app_file_name[128];

static int32_t g_console_log_enable = 1;

int32_t bone_console_get_log_flag(){
    return g_console_log_enable;
}
void bone_console_log_enable() {
    g_console_log_enable = 1;
}

void bone_console_log_disable() {
    g_console_log_enable = 0;
}


/**
 *
 * log回调函数
 * 1. stdout标准输出打印
 *
 */
static void be_log_cb(const char* tag, int level, const char* msg, int len) {
    if (tag)
        printf("[%s] %s\n", tag, msg);
    else
        printf("%s\n", msg);
    fflush(stdout);
}

/**
 * 搜索JS应用程序
 * 1. /spiffs/index.js
 * 2. 分析/spiffs/package.json获取【test】或者【main】
 */
char *search_js_app_main_entry() {
    cJSON *root = NULL;
    cJSON *item = NULL;
    void *json_data = NULL;

    int js_app_fd = -1;
    int file_len = 0;
    int json_fd = -1;

    memset(js_app_file_name, 0, sizeof(js_app_file_name));

    strcpy(js_app_file_name, BE_FS_ROOT_DIR "/index.js");

    /* use the currrent dir default index.js file to the main js entry programe */
    if ((js_app_fd = be_open(js_app_file_name, O_RDONLY)) > 0) {
        be_close(js_app_fd);
        be_debug(TAG, "find the default file :%s\n", js_app_file_name);
        return js_app_file_name;
    }

    /* cannot find the index.js int current dir*/
    if ((json_fd = be_open(APP_PACKAGE_FILE_NAME, O_RDONLY)) < 0) {
        be_error(TAG, "cannot find the file :%s\n", APP_PACKAGE_FILE_NAME);
        return NULL;
    }

    /* read package config file to json_data buffer */
    file_len = be_lseek(json_fd, 0, SEEK_END);
    json_data = calloc(1, sizeof(char) * (file_len + 1));
    if (NULL == json_data) {
        be_close(json_fd);
        json_fd = -1;
        return NULL;
    }
    be_lseek(json_fd, 0, SEEK_SET);
    be_read(json_fd, json_data, file_len);
    be_close(json_fd);

    /* parser the package json data */
    root = cJSON_Parse(json_data);
    if (NULL == root) {
        free(json_data);
        be_error(TAG, "cJSON_Parse failed \n");
        return NULL;
    }

    /* find the test/xxx.js */
    item = cJSON_GetObjectItem(root, "test");
    if (NULL != item && cJSON_String == item->type &&
        strstr(item->valuestring, ".js")) {
        snprintf(js_app_file_name, sizeof(js_app_file_name), "%s/%s", BE_FS_ROOT_DIR, item->valuestring);
        if ((js_app_fd = be_open(js_app_file_name, O_RDONLY)) > 0) {
            be_close(js_app_fd);
            free(json_data);
            cJSON_Delete(root);
            be_debug(TAG, "find test index  %s \n", js_app_file_name);
            return js_app_file_name;
        }
    }

    /* find the main/xxx.js */
    item = cJSON_GetObjectItem(root, "main");
    if (NULL != item && cJSON_String == item->type &&
        strstr(item->valuestring, ".js")) {
        strncpy(js_app_file_name, item->valuestring, MAX_FILE_NAME_LEN);
        free(json_data);
        cJSON_Delete(root);
        printf("find main index  %s \n", js_app_file_name);
        return js_app_file_name;
    }

    free(json_data);
    cJSON_Delete(root);

    return NULL;
}

void be_jse_task_main_entrance() {
    printf("%s %d  ~~~~ Enter ~~~ \r\n", __FUNCTION__, __LINE__);

    /* 初始化JSE task */
    bone_engine_task_init();

    /* 初始化JSE */
    bone_engine_init();

    /* 查找并执行JS文件 */
    char *filename = search_js_app_main_entry();
    if (filename) {
        bone_engine_eval_file(filename);
    } else {
        be_error(TAG, "Run Js With TinyEngine JS apps failed\n");
    }

    while (1) {
        /* 处理JS异步操作 */
        bone_engine_task_yield(200);
    }

    printf("%s %d  ~~~~ Byte ~~~ \r\n", __FUNCTION__, __LINE__);
}

void tiny_engine_start() {
    printf("tiny_engine_start...\r\n");

    /* BoneEngine log重定向 */
    bone_engine_set_log_cb(be_log_cb);

    hal_system_kv_init();

#ifdef _HW_ADDON_WIFI
    hal_system_wifi_init();
#endif

    if( 0 != hal_system_fs_init()){
        be_debug(TAG, "fs init failed\r\n");
        return;
    }

#ifdef JSE_CLI_DEBUG
    /* be_cli_init(); */
    cli_cmd_register_js();
    cli_cmd_register_dev();
    cli_cmd_register_file();
    cli_cmd_register_app();
#endif

    if(0 != board_mgr_init(BE_FS_ROOT_DIR"/board.json")){
        be_debug(TAG, "read "BE_FS_ROOT_DIR"/board.json error\r\n");
        return;
    }

    be_osal_create_task("jse_task", be_jse_task_main_entrance, NULL, 1024 * 10,
                        JSE_TSK_PRIORITY, NULL);

#ifdef JSE_IDE_DEBUG
    char localip[32];
    while (1) {
        if (hal_system_get_ip(localip) == 0) {
            be_debuger_ssdp_start(localip);
            break;
        } else {
            be_osal_delay(500);
        }
    }
#endif
}