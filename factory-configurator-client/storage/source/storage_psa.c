// ----------------------------------------------------------------------------
// Copyright 2016-2017 ARM Ltd.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ----------------------------------------------------------------------------
#ifdef MBED_CONF_MBED_CLOUD_CLIENT_PSA_SUPPORT
#include <stdbool.h>
#include "key_slot_allocator.h"
#include "pv_error_handling.h"
#include "storage_items.h"
#include "storage_keys.h"
#include "storage_internal.h"
#include "pv_macros.h"

extern bool g_kcm_initialized;

static kcm_status_e import_key_to_psa(const uint8_t *key_name, size_t key_name_len, kcm_item_type_e key_type, const uint8_t *key, size_t key_size, bool is_factory, const kcm_security_desc_s kcm_item_info)
{
    kcm_status_e kcm_status;
    uint8_t raw_key[KCM_EC_SECP256R1_MAX_PUB_KEY_RAW_SIZE]; // should be bigger than KCM_EC_SECP256R1_MAX_PRIV_KEY_RAW_SIZE
    size_t raw_key_act_size;

    SA_PV_LOG_INFO_FUNC_ENTER_NO_ARGS();

    // Convert key from DER to RAW representation before importing to PSA
    if (key_type == KCM_PRIVATE_KEY_ITEM) {
        kcm_status = cs_priv_key_get_der_to_raw(key, key_size, raw_key, sizeof(raw_key), &raw_key_act_size);
    } else { //key_type == KCM_PUBLIC_KEY_ITEM
        kcm_status = cs_pub_key_get_der_to_raw(key, key_size, raw_key, sizeof(raw_key), &raw_key_act_size);
    }
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed converting EC key from DER to RAW");

    //Import the key data to PSA slot
    kcm_status = ksa_store_key_to_psa(key_name, key_name_len, key_type, raw_key, raw_key_act_size, KCM_SCHEME_EC_SECP256R1, is_factory, kcm_item_info);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status == KCM_STATUS_KEY_EXIST), KCM_STATUS_FILE_EXIST, "Key exist in PSA Key Slot Allocator");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed to import the key to PSA slot");

    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();

    return KCM_STATUS_SUCCESS;
}

static kcm_status_e export_key_from_psa(
    const uint8_t *key_name,
    size_t key_name_len,
    kcm_item_type_e key_type,
    uint8_t *key_data_out,
    size_t key_data_max_size,
    size_t *key_data_act_size_out)
{
    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
    uint8_t raw_key[KCM_EC_SECP256R1_MAX_PUB_KEY_RAW_SIZE];
    size_t raw_key_act_size;

    SA_PV_LOG_INFO_FUNC_ENTER_NO_ARGS();

    kcm_status = ksa_export_key_from_psa(key_name, key_name_len, key_type, raw_key, sizeof(raw_key), &raw_key_act_size);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed export PSA key data");

    // Convert key from RAW representation to DER
    kcm_status = cs_pub_key_get_raw_to_der(raw_key, raw_key_act_size, key_data_out, key_data_max_size, key_data_act_size_out);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed converting EC key from RAW to DER");

    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();

    return kcm_status;
}

kcm_status_e storage_key_store(
    const uint8_t *kcm_key_name,
    size_t kcm_key_name_len,
    kcm_item_type_e kcm_key_type,
    bool kcm_item_is_factory,
    storage_item_prefix_type_e item_prefix_type,
    const uint8_t *kcm_item_data,
    size_t kcm_item_data_size,
    const kcm_security_desc_s kcm_item_info)
{
    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
    char kcm_complete_name[KCM_MAX_FILENAME_SIZE] = { 0 };
    size_t kcm_complete_name_len = 0;

    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_key_type != KCM_PRIVATE_KEY_ITEM && kcm_key_type != KCM_PUBLIC_KEY_ITEM), KCM_STATUS_INVALID_PARAMETER, "key type not supported");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_key_name == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid kcm_key_name");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_key_name_len == 0), KCM_STATUS_INVALID_PARAMETER, "Invalid kcm_key_name_len");
    SA_PV_LOG_INFO_FUNC_ENTER("kcm_key_name_len =%" PRIu32 " kcm_item_data_size =%" PRIu32 "", (uint32_t)kcm_key_name_len, (uint32_t)kcm_item_data_size);
    SA_PV_ERR_RECOVERABLE_RETURN_IF(((kcm_item_data == NULL) && (kcm_item_data_size > 0)), KCM_STATUS_INVALID_PARAMETER, "Provided kcm_item_data NULL and kcm_item_data_size greater than 0");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_item_data_size == 0), KCM_STATUS_ITEM_IS_EMPTY, "The data of current item is empty!");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((item_prefix_type != STORAGE_ITEM_PREFIX_KCM && item_prefix_type != STORAGE_ITEM_PREFIX_CE), KCM_STATUS_INVALID_PARAMETER, "Invalid origin_type");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((item_prefix_type == STORAGE_ITEM_PREFIX_CE && kcm_item_is_factory == true), KCM_STATUS_INVALID_PARAMETER, "Invalid kcm_item_is_factory parameter");

    kcm_status = storage_build_complete_working_item_name(kcm_key_type, item_prefix_type, kcm_key_name, kcm_key_name_len, kcm_complete_name, &kcm_complete_name_len, NULL);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed to build complete data name");

    kcm_status = import_key_to_psa((const uint8_t *)kcm_complete_name, kcm_complete_name_len, kcm_key_type, kcm_item_data, kcm_item_data_size, kcm_item_is_factory, kcm_item_info);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed importing key to PSA store (%u)", kcm_status);

    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();

    return kcm_status;
}

kcm_status_e storage_key_get_data(
    const uint8_t *kcm_item_name,
    size_t kcm_item_name_len,
    kcm_item_type_e kcm_key_type,
    storage_item_prefix_type_e item_prefix_type,
    uint8_t *key_data_out,
    size_t key_data_max_size,
    size_t *key_data_act_size_out)
{
    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
    char kcm_complete_name[KCM_MAX_FILENAME_SIZE] = { 0 };
    size_t kcm_complete_name_len = 0;

    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_key_type != KCM_PUBLIC_KEY_ITEM), KCM_STATUS_INVALID_PARAMETER, "Only public key is permitted");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_item_name == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid kcm_item_name");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_item_name_len == 0), KCM_STATUS_INVALID_PARAMETER, "Invalid kcm_item_name_len");
    SA_PV_LOG_INFO_FUNC_ENTER("item name = %.*s len = %" PRIu32 ", data max size = %" PRIu32 "", (int)kcm_item_name_len, (char*)kcm_item_name, (uint32_t)kcm_item_name_len, (uint32_t)key_data_max_size);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((item_prefix_type != STORAGE_ITEM_PREFIX_KCM && item_prefix_type != STORAGE_ITEM_PREFIX_CE), KCM_STATUS_INVALID_PARAMETER, "Invalid origin_type");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((key_data_act_size_out == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid kcm_item_data_act_size_out");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((key_data_out == NULL), KCM_STATUS_INVALID_PARAMETER, "Provided kcm_item_data is NULL");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((key_data_max_size == 0), KCM_STATUS_INVALID_PARAMETER, "Provided kcm_item_data is empty");

    kcm_status = storage_build_complete_working_item_name(kcm_key_type, item_prefix_type, kcm_item_name, kcm_item_name_len, kcm_complete_name, &kcm_complete_name_len, NULL);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed to build complete data name");

    kcm_status = export_key_from_psa((const uint8_t *)kcm_complete_name, kcm_complete_name_len, kcm_key_type, key_data_out, key_data_max_size, key_data_act_size_out);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed exporting key from PSA store (%u)", kcm_status);

    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();

    return kcm_status;
}

kcm_status_e storage_key_get_data_size(
    const uint8_t *kcm_item_name,
    size_t kcm_item_name_len,
    kcm_item_type_e kcm_key_type,
    storage_item_prefix_type_e item_prefix_type,
    size_t *key_data_act_size_out)
{
    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
    uint8_t key_data[KCM_EC_SECP256R1_MAX_PUB_KEY_DER_SIZE];
    size_t key_data_act_size = 0;

    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_key_type != KCM_PUBLIC_KEY_ITEM), KCM_STATUS_INVALID_PARAMETER, "Only public key is permitted");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_item_name == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid kcm_item_name");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_item_name_len == 0), KCM_STATUS_INVALID_PARAMETER, "Invalid kcm_item_name_len");
    SA_PV_LOG_INFO_FUNC_ENTER("item name = %.*s len=%" PRIu32 "", (int)kcm_item_name_len, (char*)kcm_item_name, (uint32_t)kcm_item_name_len);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((key_data_act_size_out == NULL), KCM_STATUS_INVALID_PARAMETER, "Kcm size out pointer is NULL");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((item_prefix_type != STORAGE_ITEM_PREFIX_KCM && item_prefix_type != STORAGE_ITEM_PREFIX_CE), KCM_STATUS_INVALID_PARAMETER, "Invalid origin_type");

    kcm_status = storage_key_get_data(kcm_item_name, kcm_item_name_len, kcm_key_type, item_prefix_type, key_data, sizeof(key_data), &key_data_act_size);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed getting key data from PSA store (%u)", kcm_status);

    // Set the effective data size for the caller
    *key_data_act_size_out = key_data_act_size;

    SA_PV_LOG_INFO_FUNC_EXIT("key_data_act_size_out =%" PRIu32 "", (uint32_t)(*key_data_act_size_out));

    return kcm_status;
}

kcm_status_e storage_key_delete(
    const uint8_t *kcm_item_name,
    size_t kcm_item_name_len,
    kcm_item_type_e kcm_item_type,
    storage_item_prefix_type_e item_prefix_type)
{
    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
    char kcm_complete_name[KCM_MAX_FILENAME_SIZE] = { 0 };
    size_t kcm_complete_name_len = 0;

    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_item_type != KCM_PRIVATE_KEY_ITEM && kcm_item_type != KCM_PUBLIC_KEY_ITEM), KCM_STATUS_INVALID_PARAMETER, "key type not supported");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_item_name == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid kcm_item_name");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_item_name_len == 0), KCM_STATUS_INVALID_PARAMETER, "Invalid kcm_item_name_len");
    SA_PV_LOG_INFO_FUNC_ENTER("item name = %.*s len = %" PRIu32 "", (int)kcm_item_name_len, (char*)kcm_item_name, (uint32_t)kcm_item_name_len);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((item_prefix_type != STORAGE_ITEM_PREFIX_KCM && item_prefix_type != STORAGE_ITEM_PREFIX_CE), KCM_STATUS_INVALID_PARAMETER, "Invalid origin_type");

    kcm_status = storage_build_complete_working_item_name(kcm_item_type, item_prefix_type, kcm_item_name, kcm_item_name_len, kcm_complete_name, &kcm_complete_name_len, NULL);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed to build complete data name");

    kcm_status = ksa_destroy_key((const uint8_t *)kcm_complete_name, kcm_complete_name_len);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed destorying PSA key (%u)", kcm_status);

    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();

    return kcm_status;
}

kcm_status_e storage_key_get_handle(
    const uint8_t *key_name,
    size_t key_name_len,
    kcm_item_type_e key_type,
    storage_item_prefix_type_e item_prefix_type,
    kcm_key_handle_t *key_h_out)
{
    char kcm_complete_name[KCM_MAX_FILENAME_SIZE] = { 0 };
    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
    psa_key_id_t key_id;
    bool is_key_exist;
    size_t kcm_complete_name_len = 0;
    psa_key_handle_t key_handle;

    SA_PV_ERR_RECOVERABLE_RETURN_IF((key_type != KCM_PRIVATE_KEY_ITEM && key_type != KCM_PUBLIC_KEY_ITEM), KCM_STATUS_INVALID_PARAMETER, "key type not supported");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((key_name == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid key_name");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((key_name_len == 0), KCM_STATUS_INVALID_PARAMETER, "Invalid key_name_len");
    SA_PV_LOG_INFO_FUNC_ENTER("item name = %.*s len = %" PRIu32 "", (int)key_name_len, (char*)key_name, (uint32_t)key_name_len);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((item_prefix_type != STORAGE_ITEM_PREFIX_KCM && item_prefix_type != STORAGE_ITEM_PREFIX_CE), KCM_STATUS_INVALID_PARAMETER, "Invalid key_source_type");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((key_type != KCM_PRIVATE_KEY_ITEM && key_type != KCM_PUBLIC_KEY_ITEM), KCM_STATUS_INVALID_PARAMETER, "Invalid key type");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((key_h_out == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid key_h_out");

    *key_h_out = 0;

    // Check if KCM initialized, if not initialize it
    if (!g_kcm_initialized) {
        kcm_status = kcm_init();
        SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "KCM initialization failed\n");
    }
    //Build complete data name
    kcm_status = storage_build_complete_working_item_name(key_type, item_prefix_type, key_name, key_name_len, kcm_complete_name, &kcm_complete_name_len, NULL);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed to to build complete name");

    //Check if current key exists in the storage
    kcm_status = ksa_is_key_exists((const uint8_t *)kcm_complete_name, kcm_complete_name_len, &is_key_exist, &key_id);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed during ksa_is_key_exist (%d)", kcm_status);

    if (!is_key_exist) {
        // no, this key has not found in KSA
        kcm_status = KCM_STATUS_ITEM_NOT_FOUND;
        goto Exit;
    }

    //Get key handle
    kcm_status = ksa_key_get_handle(key_id, &key_handle);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed to get key handle (%d)", kcm_status);

    *key_h_out = (kcm_key_handle_t)key_handle;

    SA_PV_LOG_INFO_FUNC_EXIT("kcm_item_h_out = %" PRIu32 "", (uint32_t)(*key_h_out));

Exit:
    return kcm_status;
}

kcm_status_e storage_key_close_handle(kcm_key_handle_t *key_handle)
{
    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
	
    SA_PV_ERR_RECOVERABLE_RETURN_IF((key_handle == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid key_handle");
    SA_PV_LOG_INFO_FUNC_ENTER_NO_ARGS();

    if (*key_handle == 0) {
        return KCM_STATUS_SUCCESS;
    }

    kcm_status = ksa_key_close_handle((psa_key_handle_t)*key_handle);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed to close key handle (%d)", kcm_status);

    //Reset handle value
    *key_handle = 0;

    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();

    return kcm_status;
}

kcm_status_e storage_key_pair_generate_and_store(
    const kcm_crypto_key_scheme_e     key_scheme,
    const uint8_t                     *private_key_name,
    size_t                            private_key_name_len,
    const uint8_t                     *public_key_name,
    size_t                            public_key_name_len,
    storage_item_prefix_type_e        item_prefix_type,
    bool                              is_factory)
{
    char kcm_complete_priv_name[KCM_MAX_FILENAME_SIZE] = { 0 };
    size_t kcm_complete_priv_name_size = 0;
    char kcm_complete_pub_name[KCM_MAX_FILENAME_SIZE] = { 0 };
    size_t kcm_complete_pub_name_size = 0;
    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
    kcm_status_e kcm_del_status;
    psa_key_id_t key_id = 0;
    bool is_keypair_exist;
    uint8_t raw_pub_key[KCM_EC_SECP256R1_MAX_PUB_KEY_RAW_SIZE];
    size_t raw_pub_key_size;

    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();

    //Create complete data names
    kcm_status = storage_build_complete_working_item_name(KCM_PRIVATE_KEY_ITEM,
                                                          item_prefix_type,
                                                          private_key_name,
                                                          private_key_name_len,
                                                          kcm_complete_priv_name,
                                                          &kcm_complete_priv_name_size,
                                                          NULL);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed to to build complete name");
    
    if (public_key_name != NULL) {
        kcm_status = storage_build_complete_working_item_name(KCM_PUBLIC_KEY_ITEM,
                                                              item_prefix_type,
                                                              public_key_name,
                                                              public_key_name_len,
                                                              kcm_complete_pub_name,
                                                              &kcm_complete_pub_name_size,
                                                              NULL);
        SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed to to build complete name");
    }

    //Check if current private exists in the storage
    kcm_status = ksa_is_key_exists((const uint8_t *)kcm_complete_priv_name, kcm_complete_priv_name_size, &is_keypair_exist, &key_id);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((kcm_status != KCM_STATUS_SUCCESS), (kcm_status = kcm_status), Exit, "Failed during ksa_is_key_exists (%d)", kcm_status);

    if (!is_keypair_exist && public_key_name != NULL) {
        //Check if current public exists in the storage
        kcm_status = ksa_is_key_exists((const uint8_t *)kcm_complete_pub_name, kcm_complete_pub_name_size, &is_keypair_exist, &key_id);
        SA_PV_ERR_RECOVERABLE_GOTO_IF((kcm_status != KCM_STATUS_SUCCESS), (kcm_status = kcm_status), Exit, "Failed during ksa_is_key_exists (%d)", kcm_status);

    }

    if (is_keypair_exist) {
        //If current private or public key already exists return the error
        kcm_status = KCM_STATUS_KEY_EXIST;
        goto Exit;
    }

    //Generate and import the generated keypair to PSA slot
    kcm_status = ksa_store_key_to_psa((const uint8_t *)kcm_complete_priv_name, strlen(kcm_complete_priv_name), KCM_PRIVATE_KEY_ITEM, NULL, 0, key_scheme, is_factory, NULL);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed to import the key to PSA slot");

    if (public_key_name != NULL) {
        // read public key from keypair using kcm complete private name
        kcm_status = ksa_export_key_from_psa((const uint8_t *)kcm_complete_priv_name, strlen(kcm_complete_priv_name), KCM_PUBLIC_KEY_ITEM, raw_pub_key, sizeof(raw_pub_key), &raw_pub_key_size);
        SA_PV_ERR_RECOVERABLE_GOTO_IF((kcm_status != KCM_STATUS_SUCCESS), (kcm_status = kcm_status), delete_priv_and_exit, "failed to export public key from pair");

        // store public key using different slot
        kcm_status = ksa_store_key_to_psa((const uint8_t *)kcm_complete_pub_name, kcm_complete_pub_name_size, KCM_PUBLIC_KEY_ITEM, raw_pub_key, raw_pub_key_size, key_scheme, is_factory, NULL);
        SA_PV_ERR_RECOVERABLE_GOTO_IF((kcm_status != KCM_STATUS_SUCCESS), (kcm_status = kcm_status), delete_priv_and_exit, "failed to import public key");
    }

Exit:
    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();
    return kcm_status;

delete_priv_and_exit:
    // Failed to store public, remove stored private key
    kcm_del_status = ksa_destroy_key((const uint8_t *)kcm_complete_priv_name, kcm_complete_priv_name_size);
    if (kcm_del_status != KCM_STATUS_SUCCESS) {
        SA_PV_LOG_ERR("failed destorying PSA key during cleanup (%u)", kcm_del_status);
    }
    return kcm_status;
}

kcm_status_e storage_init(void)
{
    kcm_status_e kcm_status;

    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();

    kcm_status = storage_specific_init();
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed initializing storage specific backend (kcm_status %d)", kcm_status);

    // FIXME: at this stage it is quite safe to set this flag to "true", it is necessary since the KSA module is currently
    // making use of KCM APIs and by *NOT* setting this flag we are assuring endless init() loop because of the lazy initializing process.
    // A better solution would be to disconnect the KCM <-> KSA relationship.
    g_kcm_initialized = true;

    kcm_status = ksa_init();
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed initializing KSA (kcm_status %d)", kcm_status);

    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();

    return kcm_status;
}

kcm_status_e storage_finalize(void)
{
    kcm_status_e kcm_status;

    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();

    kcm_status = ksa_fini();
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed finalizing KSA (kcm_status %d)", kcm_status);

    kcm_status = storage_specific_finalize();
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed finalizing storage specific backend (kcm_status %d)", kcm_status);

    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();

    return kcm_status;
}

kcm_status_e storage_reset_to_factory_state(void)
{
    kcm_status_e kcm_status;

    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();

    kcm_status = ksa_factory_reset();
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed for KSA factory reset");

    kcm_status = storage_factory_reset();
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed perform factory reset");

    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();
    return kcm_status;
}

#endif //MBED_CONF_MBED_CLOUD_CLIENT_PSA_SUPPORT
