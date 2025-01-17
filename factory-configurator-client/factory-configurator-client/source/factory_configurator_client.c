// ----------------------------------------------------------------------------
// Copyright 2016-2018 ARM Ltd.
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

#include "factory_configurator_client.h"
#include "key_config_manager.h"
#include "pv_error_handling.h"
#include "fcc_verification.h"
#include "storage_items.h"
#include "fcc_defs.h"
#include "fcc_malloc.h"
#include "common_utils.h"
#include "pal.h"
#include "fcc_utils.h"
#if defined(MBED_CONF_MBED_CLOUD_CLIENT_PSA_SUPPORT) && defined(TARGET_LIKE_MBED)
#include "psa/lifecycle.h"
#endif
/**
* Device general info
*/
const char g_fcc_use_bootstrap_parameter_name[] = "mbed.UseBootstrap";
const char g_fcc_endpoint_parameter_name[] = "mbed.EndpointName";
const char g_fcc_account_id_parameter_name[] = "mbed.AccountID";
const char g_fcc_first_to_claim_parameter_name[] = "mbed.FirstToClaim";

/**
* Device meta data
*/
const char g_fcc_manufacturer_parameter_name[] = "mbed.Manufacturer";
const char g_fcc_model_number_parameter_name[] = "mbed.ModelNumber";
const char g_fcc_device_type_parameter_name[] = "mbed.DeviceType";
const char g_fcc_hardware_version_parameter_name[] = "mbed.HardwareVersion";
const char g_fcc_memory_size_parameter_name[] = "mbed.MemoryTotalKB";
const char g_fcc_device_serial_number_parameter_name[] = "mbed.SerialNumber";
/**
* Time Synchronization
*/
const char g_fcc_current_time_parameter_name[] = "mbed.CurrentTime";
const char g_fcc_device_time_zone_parameter_name[] = "mbed.Timezone";
const char g_fcc_offset_from_utc_parameter_name[] = "mbed.UTCOffset";
/**
* Bootstrap configuration
*/
const char g_fcc_bootstrap_server_ca_certificate_name[] = "mbed.BootstrapServerCACert";
const char g_fcc_bootstrap_server_crl_name[] = "mbed.BootstrapServerCRL";
const char g_fcc_bootstrap_server_uri_name[] = "mbed.BootstrapServerURI";
const char g_fcc_bootstrap_device_certificate_name[] = "mbed.BootstrapDeviceCert";
const char g_fcc_bootstrap_device_private_key_name[] = "mbed.BootstrapDevicePrivateKey";
/**
* LWm2m configuration
*/
const char g_fcc_lwm2m_server_ca_certificate_name[] = "mbed.LwM2MServerCACert";
const char g_fcc_lwm2m_server_crl_name[] = "mbed.LwM2MServerCRL";
const char g_fcc_lwm2m_server_uri_name[] = "mbed.LwM2MServerURI";
const char g_fcc_lwm2m_device_certificate_name[] = "mbed.LwM2MDeviceCert";
const char g_fcc_lwm2m_device_private_key_name[] = "mbed.LwM2MDevicePrivateKey";
/**
* Firmware update
*/
const char g_fcc_update_authentication_certificate_name[] = "mbed.UpdateAuthCert";
const char g_fcc_class_id_name[] = "mbed.ClassId";
const char g_fcc_vendor_id_name[] = "mbed.VendorId";

static bool g_is_fcc_initialized = false;
bool g_is_session_finished = true;

fcc_status_e fcc_init(void)
{
    palStatus_t pal_status;

    SA_PV_LOG_INFO_FUNC_ENTER_NO_ARGS();

    if (g_is_fcc_initialized) {
        // No need for second initialization
        return FCC_STATUS_SUCCESS;
    }
    
    pal_status = pal_init();
    SA_PV_ERR_RECOVERABLE_RETURN_IF((pal_status == PAL_ERR_INIT_SOTP_FAILED), FCC_STATUS_STORE_ERROR, "Failed initializing internal storage (%" PRIu32 ")", pal_status);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((pal_status != PAL_SUCCESS), FCC_STATUS_ERROR, "Failed initializing PAL (%" PRIu32 ")", pal_status);
    
    //Initialize output info handler
    fcc_init_output_info_handler();

    g_is_fcc_initialized = true;

    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();

    return FCC_STATUS_SUCCESS;
}

fcc_status_e fcc_finalize(void)
{
    fcc_status_e fcc_status = FCC_STATUS_SUCCESS;
    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;

    SA_PV_LOG_INFO_FUNC_ENTER_NO_ARGS();

    SA_PV_ERR_RECOVERABLE_RETURN_IF((!g_is_fcc_initialized), FCC_STATUS_NOT_INITIALIZED, "FCC not initialized");

    //FIXME: add relevant error handling - general task for all APIs.
    //It is okay to finalize KCM here since it's already initialized beforehand.
    kcm_status = kcm_finalize();
    if (kcm_status != KCM_STATUS_SUCCESS) {
        fcc_status = FCC_STATUS_ERROR;
        SA_PV_LOG_ERR("Failed finalizing KCM");
    }

    //Finalize output info handler
    fcc_clean_output_info_handler();

    //Finalize PAL
    pal_destroy();

    g_is_fcc_initialized = false;
    g_is_session_finished = true;

    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();

    return fcc_status;
}

fcc_status_e fcc_storage_delete()
{
    kcm_status_e status = KCM_STATUS_SUCCESS;

    SA_PV_LOG_INFO_FUNC_ENTER_NO_ARGS();

#ifndef MBED_CONF_MBED_CLOUD_CLIENT_EXTERNAL_SST_SUPPORT
    SA_PV_ERR_RECOVERABLE_RETURN_IF((!g_is_fcc_initialized), FCC_STATUS_NOT_INITIALIZED, "FCC not initialized");
#endif


    // Finalize KCM before deleting all the storage. This way KCM module will do a lazy init again (also re-initializing the PSA if used)
    status = kcm_finalize();
    SA_PV_ERR_RECOVERABLE_RETURN_IF((status != KCM_STATUS_SUCCESS), FCC_STATUS_KCM_STORAGE_ERROR, "Failed kcm_finalize");

    // This will delete the external storage such as certificates, etc
    // However, RBP data may remain in storage (in case of V7 or V8)
    // We remove the external storage first because some of its metadata may be contained inside the internal storage,
    // and we may need access to it when deleting the external storage
    status = storage_reset();
    SA_PV_ERR_RECOVERABLE_RETURN_IF((status == KCM_STATUS_ESFS_ERROR), FCC_STATUS_KCM_STORAGE_ERROR, "Failed in storage_reset. got ESFS error");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((status != KCM_STATUS_SUCCESS), FCC_STATUS_ERROR, "Failed storage reset");

    // If using PSA - change to clean state
#if defined(MBED_CONF_MBED_CLOUD_CLIENT_PSA_SUPPORT) && defined(TARGET_LIKE_MBED)
    psa_status_t psa_status;

    /* Go back to an empty storage state
     * * In case of non-PSA boards (such as K64F and K66F) with KVSTORE config, this is not really needed, as kv_reset() 
     *   called by storage_reset()) as PSA and RBP items are stored in the same TDBStore. In this case, the call will
     *   get us from an empty storage state to an empty storage state.
     * * In case of a user provided SST, we do not know whether pal_SSTReset() will also remove the PSA storage (probably
     *   not), so we probably need this call.
     * * In case of actual PSA boards, with KVSTORE config, we must call this function so the PSA storage is removed.
     * * Irrelevant for PSA over Linux
     */
    psa_status = mbed_psa_reboot_and_request_new_security_state(PSA_LIFECYCLE_ASSEMBLY_AND_TEST);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((psa_status != PSA_SUCCESS), FCC_STATUS_ERROR, "Failed storage reset");

#endif

    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();
    return FCC_STATUS_SUCCESS;
}

fcc_output_info_s* fcc_get_error_and_warning_data(void)
{
    SA_PV_LOG_INFO_FUNC_ENTER_NO_ARGS();

    SA_PV_ERR_RECOVERABLE_RETURN_IF((!g_is_fcc_initialized), NULL, "FCC not initialized");

    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();

    return get_output_info();
}

bool fcc_is_session_finished(void)
{
    SA_PV_LOG_INFO_FUNC_ENTER_NO_ARGS();

    return g_is_session_finished;
}

fcc_status_e fcc_verify_device_configured_4mbed_cloud(void)
{
    fcc_status_e  fcc_status =  FCC_STATUS_SUCCESS;
    bool use_bootstrap = false;
    bool success = false;

    SA_PV_LOG_INFO_FUNC_ENTER_NO_ARGS();

    SA_PV_ERR_RECOVERABLE_RETURN_IF((!g_is_fcc_initialized), FCC_STATUS_NOT_INITIALIZED, "FCC not initialized");

    /*Initialize fcc_output_info_s structure.
    In case output indo struct is not empty in the beginning of the verify process we will clean it.*/
    fcc_clean_output_info_handler();

    //Check entropy initialization
    success = fcc_is_entropy_initialized();
    SA_PV_ERR_RECOVERABLE_RETURN_IF((success != true), fcc_status = FCC_STATUS_ENTROPY_ERROR, "Entropy is not initialized");

    //Check time synchronization
    fcc_status = fcc_check_time_synchronization();
    SA_PV_ERR_RECOVERABLE_RETURN_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status, "Failed to check time synhronization");

    //Get bootstrap mode
    fcc_status = fcc_get_bootstrap_mode(&use_bootstrap);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status, "Failed to get bootstrap mode");

    // Check general info
    fcc_status = fcc_check_device_general_info();
    SA_PV_ERR_RECOVERABLE_RETURN_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status, "Failed to check general info");

    //Check device meta-data
    fcc_status = fcc_check_device_meta_data();
    SA_PV_ERR_RECOVERABLE_RETURN_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status, "Failed to check configuration parameters");

    //Check device security objects
    fcc_status = fcc_check_device_security_objects(use_bootstrap);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status, "Failed to check device security objects");

    //Check firmware integrity
    fcc_status = fcc_check_firmware_update_integrity();
    SA_PV_ERR_RECOVERABLE_RETURN_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status, "Failed to check device security objects");

    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();

    return fcc_status;
}

fcc_status_e fcc_entropy_set(const uint8_t *buf, size_t buf_size)
{
    palStatus_t pal_status;
    SA_PV_LOG_INFO_FUNC_ENTER("buf_size = %" PRIu32, (uint32_t)buf_size);

    SA_PV_ERR_RECOVERABLE_RETURN_IF((!g_is_fcc_initialized), FCC_STATUS_NOT_INITIALIZED, "FCC not initialized");
    SA_PV_ERR_RECOVERABLE_RETURN_IF(buf_size != FCC_ENTROPY_SIZE, FCC_STATUS_INVALID_PARAMETER, "Size of entropy provided is %" PRIu32 ", Should be %" PRIu32 , (uint32_t)buf_size, (uint32_t)FCC_ENTROPY_SIZE);

    pal_status = pal_osEntropyInject(buf, buf_size); 
    SA_PV_ERR_RECOVERABLE_RETURN_IF((pal_status != PAL_SUCCESS), fcc_convert_pal_to_fcc_status(pal_status), "Failed to set entropy, pal status =%" PRId32, pal_status);

    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();
    return FCC_STATUS_SUCCESS;
}

fcc_status_e fcc_rot_set(const uint8_t *buf, size_t buf_size)
{
    fcc_status_e fcc_status = FCC_STATUS_SUCCESS;
    palStatus_t pal_status = PAL_SUCCESS;

    SA_PV_LOG_INFO_FUNC_ENTER("buf_size = %" PRIu32 , (uint32_t)buf_size);

    SA_PV_ERR_RECOVERABLE_RETURN_IF((!g_is_fcc_initialized), FCC_STATUS_NOT_INITIALIZED, "FCC not initialized");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((buf == NULL || buf_size != FCC_ROT_SIZE), FCC_STATUS_INVALID_PARAMETER, "Invalid params");

    pal_status = pal_osSetRoT((uint8_t*)buf, buf_size);

    SA_PV_ERR_RECOVERABLE_RETURN_IF((pal_status == PAL_ERR_ITEM_EXIST), fcc_status  = FCC_STATUS_ROT_ERROR, "RoT already exist in storage");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((pal_status == PAL_ERR_INVALID_ARGUMENT), fcc_status = FCC_STATUS_INVALID_PARAMETER, "Failed to set RoT");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((pal_status != PAL_SUCCESS), fcc_status = FCC_STATUS_ROT_ERROR, "Failed to set RoT");

    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();
    return fcc_status;
}

fcc_status_e fcc_time_set(uint64_t time)
{
    palStatus_t pal_status;

    SA_PV_LOG_INFO_FUNC_ENTER_NO_ARGS();
    SA_PV_ERR_RECOVERABLE_RETURN_IF((!g_is_fcc_initialized), FCC_STATUS_NOT_INITIALIZED, "FCC not initialized");

    pal_status = pal_osSetStrongTime(time);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((pal_status != PAL_SUCCESS), FCC_STATUS_ERROR, "Failed to set new EPOCH time (pal_status = %" PRIu32 ")", pal_status);

    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();
    return FCC_STATUS_SUCCESS;
}

fcc_status_e fcc_is_factory_disabled(bool *is_factory_disabled)
{

    int64_t factory_disable_flag = 0;
    size_t data_actual_size_out = 0;
    palStatus_t pal_status = PAL_SUCCESS;

    SA_PV_LOG_INFO_FUNC_ENTER_NO_ARGS();
    SA_PV_ERR_RECOVERABLE_RETURN_IF((!g_is_fcc_initialized), FCC_STATUS_NOT_INITIALIZED, "FCC not initialized");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((is_factory_disabled == NULL), FCC_STATUS_INVALID_PARAMETER, "Invalid param is_factory_disabled");

    pal_status = storage_rbp_read(STORAGE_RBP_FACTORY_DONE_NAME, (uint8_t *)(&factory_disable_flag), sizeof(factory_disable_flag), &data_actual_size_out);
    SA_PV_LOG_INFO("pal_status:%" PRId32", factory_disable_flag:%" PRIuMAX "\n", pal_status, factory_disable_flag);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((pal_status != PAL_SUCCESS && pal_status != PAL_ERR_ITEM_NOT_EXIST), fcc_convert_pal_to_fcc_status(pal_status), "Failed for storage_rbp_read");
    SA_PV_ERR_RECOVERABLE_RETURN_IF(((factory_disable_flag != 0) && (factory_disable_flag != 1)), FCC_STATUS_FACTORY_DISABLED_ERROR, "Failed for storage_rbp_read");

    // If we get here - it must be either "0" or "1"
    *is_factory_disabled = (factory_disable_flag == 1) ? true : false;
    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();
    return FCC_STATUS_SUCCESS;
}


fcc_status_e fcc_factory_disable(void)
{
    palStatus_t pal_status = PAL_SUCCESS;
    int64_t factory_disable_flag = 1;
    size_t data_actual_size_out = 0;

    SA_PV_LOG_INFO_FUNC_ENTER_NO_ARGS();

    SA_PV_ERR_RECOVERABLE_RETURN_IF((!g_is_fcc_initialized), FCC_STATUS_NOT_INITIALIZED, "FCC not initialized");

    pal_status = storage_rbp_write(STORAGE_RBP_FACTORY_DONE_NAME, (uint8_t *)(&factory_disable_flag), sizeof(factory_disable_flag), true);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((pal_status == PAL_ERR_ITEM_EXIST), FCC_STATUS_FACTORY_DISABLED_ERROR, "FCC already disabled in storage");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((pal_status == PAL_ERR_INVALID_ARGUMENT), FCC_STATUS_INVALID_PARAMETER, "Failed to set storage_rbp_write");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((pal_status != PAL_SUCCESS), fcc_convert_pal_to_fcc_status(pal_status), "Failed to set storage_rbp_write");

    //Check FACTORY_DONE written correctly
    pal_status = storage_rbp_read(STORAGE_RBP_FACTORY_DONE_NAME, (uint8_t *)(&factory_disable_flag), sizeof(factory_disable_flag), &data_actual_size_out);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((pal_status != PAL_SUCCESS || data_actual_size_out != sizeof(factory_disable_flag)), FCC_STATUS_FACTORY_DISABLED_ERROR, "Failed to set storage_rbp_write");


    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();
    return FCC_STATUS_SUCCESS;
}


fcc_status_e fcc_trust_ca_cert_id_set(void)
{
    fcc_status_e fcc_status = FCC_STATUS_SUCCESS;
    palStatus_t pal_status = PAL_SUCCESS;
    fcc_status_e output_info_fcc_status = FCC_STATUS_SUCCESS;
    uint8_t attribute_data[PAL_CERT_ID_SIZE] __attribute__((aligned(4))) = { 0 };
    size_t size_of_attribute_data = 0;
    bool use_bootstrap = false;

    SA_PV_LOG_INFO_FUNC_ENTER_NO_ARGS();

    SA_PV_ERR_RECOVERABLE_RETURN_IF((!g_is_fcc_initialized), FCC_STATUS_NOT_INITIALIZED, "FCC not initialized");

    fcc_status = fcc_get_bootstrap_mode(&use_bootstrap);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status, "Failed to get bootstrap mode");


    //For now this API relevant only for bootstrap certificate.
    if (use_bootstrap == true) {
        fcc_status = fcc_get_certificate_attribute_by_name((const uint8_t*)g_fcc_bootstrap_server_ca_certificate_name,
            (size_t)(strlen(g_fcc_bootstrap_server_ca_certificate_name)),
            CS_CERT_ID_ATTR,
            attribute_data,
            sizeof(attribute_data),
            &size_of_attribute_data);
        SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, exit, "Failed to get ca id");
 
        pal_status = storage_rbp_write(STORAGE_RBP_TRUSTED_TIME_SRV_ID_NAME, attribute_data, size_of_attribute_data, true);
        SA_PV_ERR_RECOVERABLE_GOTO_IF((pal_status == PAL_ERR_ITEM_EXIST), (fcc_status = FCC_STATUS_CA_ERROR), exit, "CA already exist in storage");
        SA_PV_ERR_RECOVERABLE_GOTO_IF((pal_status == PAL_ERR_INVALID_ARGUMENT), fcc_status = FCC_STATUS_INVALID_PARAMETER, exit, "Failed to set ca id");
        SA_PV_ERR_RECOVERABLE_GOTO_IF((pal_status != PAL_SUCCESS), fcc_status = fcc_convert_pal_to_fcc_status(pal_status), exit, "Failed to setca id");
    }

exit:
    if (fcc_status != FCC_STATUS_SUCCESS) {
        output_info_fcc_status = fcc_store_error_info((const uint8_t*)g_fcc_bootstrap_server_ca_certificate_name, strlen(g_fcc_bootstrap_server_ca_certificate_name), fcc_status);
        SA_PV_ERR_RECOVERABLE_RETURN_IF((output_info_fcc_status != FCC_STATUS_SUCCESS),
            fcc_status = FCC_STATUS_OUTPUT_INFO_ERROR,
            "Failed to set ca identifier error  %d",
            fcc_status);
    }

    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();
    return fcc_status;
}

bool fcc_is_initialized()
{
    return g_is_fcc_initialized;
}
