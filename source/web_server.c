/*******************************************************************************
 * File Name: web_server.c
 *
 * Description: This file contains the necessary functions to configure the device
 *              in SoftAP mode and starts an HTTP server. The device can be
 *              provisioned to connect to an AP.
 *
 ********************************************************************************
 * Copyright 2021-2023, Cypress Semiconductor Corporation (an Infineon company) or
 * an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
 *
 * This software, including source code, documentation and related
 * materials ("Software") is owned by Cypress Semiconductor Corporation
 * or one of its affiliates ("Cypress") and is protected by and subject to
 * worldwide patent protection (United States and foreign),
 * United States copyright laws and international treaty provisions.
 * Therefore, you may use this Software only as provided in the license
 * agreement accompanying the software package from which you
 * obtained this Software ("EULA").
 * If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
 * non-transferable license to copy, modify, and compile the Software
 * source code solely for use in connection with Cypress's
 * integrated circuit products.  Any reproduction, modification, translation,
 * compilation, or representation of this Software except as specified
 * above is prohibited without the express written permission of Cypress.
 *
 * Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
 * reserves the right to make changes to the Software without notice. Cypress
 * does not assume any liability arising out of the application or use of the
 * Software or any product or circuit described in the Software. Cypress does
 * not authorize its products for use in any products where a malfunction or
 * failure of the Cypress product may reasonably be expected to result in
 * significant property damage, injury or death ("High Risk Product"). By
 * including Cypress's product in a High Risk Product, the manufacturer
 * of such system or application assumes all risk of such use and in doing
 * so agrees to indemnify Cypress against all liability.
 *******************************************************************************/

/* Header file includes */
#include "cyhal.h"
#include "cybsp.h"
#include "cy_retarget_io.h"

/* Secure Sockets header file */
#include "cy_secure_sockets.h"
#include "cy_tls.h"

/* Wi-Fi connection manager header files */
#include "cy_wcm.h"
#include "cy_wcm_error.h"

/* HTTP server task header file. */
#include "web_server.h"
#include "cy_http_server.h"
#include "cy_log.h"

/* Standard C header file */
#include <string.h>
#include <ctype.h>

/* HTTP server task header file. */
#include "cy_http_server.h"
#include "html_web_page.h"
#include "web_server.h"

/*******************************************************************************
 * Global Variables
 ********************************************************************************/
static const cy_wcm_ip_setting_t ap_sta_mode_ip_settings =
    {
        INITIALISER_IPV4_ADDRESS(.ip_address, SOFTAP_IP_ADDRESS),
        INITIALISER_IPV4_ADDRESS(.netmask, SOFTAP_NETMASK),
        INITIALISER_IPV4_ADDRESS(.gateway, SOFTAP_GATEWAY),
};

/* Holds the IP address and port number details of the socket for the HTTP server. */
cy_socket_sockaddr_t http_server_ip_address;

/* Pointer to HTTP event stream used to send device data to client. */
cy_http_response_stream_t *http_event_stream;

/* Wi-Fi network interface. */
cy_network_interface_t nw_interface;

/* HTTP server instance. */
cy_http_server_t http_ap_server;

/* HTTP server instance. */
cy_http_server_t http_sta_server;

/*Buffer to store SSID*/
uint8_t wifi_ssid[WIFI_SSID_LEN] = {0};

/*Buffer to store Password*/
uint8_t wifi_pwd[WIFI_PWD_LEN] = {0};

/*Buffer to store HTTP data*/
char buffer[BUFFER_LENGTH] = {0};

/* Holds the response handler for HTTP GET and POST request from the client
 * to implement Wi-Fi scan and Wi-Fi connect funtionality.
 */
cy_resource_dynamic_data_t http_wifi_resource;

/* Flag to indicate if device has been configured. */
volatile bool device_configured = false;

/*Variable to indicate re-configuration request*/
volatile int8_t reconfiguration_request = 0;

/* Array to store Wi-Fi connect response. */
static char http_wifi_connect_response[WIFI_CONNECT_RESPONSE_LENGTH] = {0};

/*******************************************************************************
 * Function Name: softap_resource_handler
 *******************************************************************************
 * Summary:
 *  Handles HTTP GET, POST, and PUT requests from the client.
 *  HTTP GET sends the HTTP startup webpage as a response to the client.
 *  HTTP POST extracts the credentials from the HTTP data from the client
 *  and tries to connect to the AP.
 *  HTTP PUT sends an error message as a response to the client if the resource
 *  registration is unsuccessful.
 *
 * Parameters:
 *  url_path - Pointer to the HTTP URL path.
 *  url_parameters - Pointer to the HTTP URL query string.
 *  stream - Pointer to the HTTP response stream.
 *  arg - Pointer to the argument passed during HTTP resource registration.
 *  http_message_body - Pointer to the HTTP data from the client.
 *
 * Return:
 *  int32_t - Returns HTTP_REQUEST_HANDLE_SUCCESS if the request from the client
 *  was handled successfully. Otherwise, it returns HTTP_REQUEST_HANDLE_ERROR.
 *
 *******************************************************************************/
int32_t softap_resource_handler(const char *url_path,
                                const char *url_parameters,
                                cy_http_response_stream_t *stream,
                                void *arg,
                                cy_http_message_body_t *http_message_body)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    int32_t status = HTTP_REQUEST_HANDLE_SUCCESS;

    switch (http_message_body->request_type)
    {
    case CY_HTTP_REQUEST_GET:

        /* If device is not configured send the initial page */
        if (!device_configured)
        {
            /* The start up page of the HTTP client will be sent as an initial response
             * to the GET request.
             */
            result = cy_http_server_response_stream_write_payload(stream, HTTP_SOFTAP_STARTUP_WEBPAGE, sizeof(HTTP_SOFTAP_STARTUP_WEBPAGE) - 1);
            if (CY_RSLT_SUCCESS != result)
            {
                ERR_INFO(("Failed to send the HTTP GET response.\r\n"));
            }
        }
        else
        {
            /* Send the data of the device */
            result = cy_http_server_response_stream_write_payload(stream, SOFTAP_DEVICE_DATA, sizeof(SOFTAP_DEVICE_DATA) - 1);
            if (CY_RSLT_SUCCESS != result)
            {
                ERR_INFO(("Failed to send the HTTP GET response.\n"));
            }
        }
        break;

    case CY_HTTP_REQUEST_POST:

        if (!device_configured)
        {
            /* The device tries to connect to the AP using the credentials sent via HTTP
             * webpage.
             */
            result = wifi_extract_credentials(http_message_body->data, http_message_body->data_length, stream);
        }
        else
        {

            /* Send the HTTP response. */
            result = cy_http_server_response_stream_write_payload(stream, HTTP_HEADER_204, sizeof(HTTP_HEADER_204) - 1);
            if (CY_RSLT_SUCCESS != result)
            {
                ERR_INFO(("Failed to send the HTTP POST response.\n"));
            }
        }
        break;

    default:
        ERR_INFO(("Received invalid HTTP request method. Supported HTTP methods are GET, POST, and PUT.\n"));

        break;
    }

    if (CY_RSLT_SUCCESS != result)
    {
        status = HTTP_REQUEST_HANDLE_ERROR;
    }

    return status;
}

/********************************************************************************
 * Function Name: wifi_extract_credentials
 ********************************************************************************
 * Summary:
 *  The function extracts the credentials entered via HTTP webpage. Switches to STA
 *  mode then connects to the same credentials.
 *
 * Parameters:
 *  const uint8_t* data : The HTTP data that contains ssid and password that is
 *  entered from the HTTP webpage.
 *   uint32_t data_len : The length of the HTTP response.
 *
 * Return:
 *  void
 *
 *******************************************************************************/
cy_rslt_t wifi_extract_credentials(const uint8_t *data, uint32_t data_len, cy_http_response_stream_t *stream)
{
    int8_t ssid_buff_index, buff_index = 0;
    cy_rslt_t result = CY_RSLT_SUCCESS;
    char *response = http_wifi_connect_response;

    /*decode the url encoded data using the function url_decode()*/
    url_decode(buffer, data);

    if (!strncmp("SSID", buffer, 4))
    {
        /* Extract SSID and Password - skip to SSID*/
        while ((buffer[buff_index++] != EQUALS_OPERATOR_ASCII_VALUE))
            ;

        ssid_buff_index = 0;
        /*skip '&' */
        while ((buffer[buff_index] != AMPERSAND_OPERATOR_ASCII_VALUE))
            wifi_ssid[ssid_buff_index++] = buffer[buff_index++];

        buff_index++;
        /* skip to Password */
        while ((buffer[buff_index++] != EQUALS_OPERATOR_ASCII_VALUE))
            ;

        ssid_buff_index = 0;
        while ((buff_index < data_len))
        {
            if (buffer[buff_index] == AMPERSAND_OPERATOR_ASCII_VALUE)
                break;

            wifi_pwd[ssid_buff_index++] = buffer[buff_index++];
        }
    }
    result = cy_http_server_response_stream_write_payload(stream, WIFI_CONNECT_IN_PROGRESS, sizeof(WIFI_CONNECT_IN_PROGRESS));
    if (CY_RSLT_SUCCESS != result)
    {
        ERR_INFO(("Failed to send the HTTP POST response.\n"));
    }

    result = start_sta_mode();
    if (CY_RSLT_SUCCESS != result)
    {
        sprintf(response, WIFI_CONNECT_RESPONSE_START);
        response += strlen(WIFI_CONNECT_RESPONSE_START);
        sprintf(response, WIFI_CONNECT_FAIL_RESPONSE_END);
        response += strlen(WIFI_CONNECT_FAIL_RESPONSE_END);
        result = cy_http_server_response_stream_write_payload(stream, http_wifi_connect_response, sizeof(http_wifi_connect_response));
        if (CY_RSLT_SUCCESS != result)
        {
            ERR_INFO(("Failed to send the HTTP POST response.\n"));
        }
    }
    else
    {
        sprintf(response, WIFI_CONNECT_RESPONSE_START);
        response += strlen(WIFI_CONNECT_RESPONSE_START);
        sprintf(response, WIFI_CONNECT_SUCCESS_RESPONSE_END);
        response += strlen(WIFI_CONNECT_SUCCESS_RESPONSE_END);
        result = cy_http_server_response_stream_write_payload(stream, http_wifi_connect_response, sizeof(http_wifi_connect_response));
        if (CY_RSLT_SUCCESS != result)
        {
            ERR_INFO(("Failed to send the HTTP POST response.\n"));
        }
    }
    return result;
}

/********************************************************************************
 * Function Name: start_ap_mode
 ********************************************************************************
 * Summary:
 *  The function configures device in Concurrent AP + STA mode and initialises
 *  a SoftAP with the given credentials (SOFTAP_SSID, SOFTAP_PASSWORD and  security
 *  CY_WCM_SOFTAP_PASSWORD_WPA2_AES_PSK).
 *
 * Parameters:
 *  void
 *
 * Return:
 *  cy_rslt_t: Returns CY_RSLT_SUCCESS if the SoftAP is started successfully,
 *  a WCM error code otherwise.
 *
 *******************************************************************************/
cy_rslt_t start_ap_mode()
{
    cy_rslt_t result;
    cy_wcm_ap_config_t ap_conf;
    cy_wcm_ip_address_t ipv4_addr;

    memset(&ap_conf, 0, sizeof(cy_wcm_ap_config_t));
    memset(&ipv4_addr, 0, sizeof(cy_wcm_ip_address_t));

    ap_conf.channel = 1;
    memcpy(ap_conf.ap_credentials.SSID, SOFTAP_SSID, strlen(SOFTAP_SSID) + 1);
    memcpy(ap_conf.ap_credentials.password, SOFTAP_PASSWORD, strlen(SOFTAP_PASSWORD) + 1);
    ap_conf.ap_credentials.security = SOFTAP_SECURITY_TYPE;
    ap_conf.ip_settings.ip_address = ap_sta_mode_ip_settings.ip_address;
    ap_conf.ip_settings.netmask = ap_sta_mode_ip_settings.netmask;
    ap_conf.ip_settings.gateway = ap_sta_mode_ip_settings.gateway;

    result = cy_wcm_start_ap(&ap_conf);
    PRINT_AND_ASSERT(result, "cy_wcm_start_ap failed...! \n");

    /* Get IPV4 address for AP */
    result = cy_wcm_get_ip_addr(CY_WCM_INTERFACE_TYPE_AP, &ipv4_addr);
    PRINT_AND_ASSERT(result, "cy_wcm_get_ip_addr failed...! \n");

    return result;
}

/*******************************************************************************
 * Function Name: start_sta_mode
 *******************************************************************************
 * Summary:
 *  The function attempts to connect to Wi-Fi until a connection is made or
 *  MAX_WIFI_RETRY_COUNT attempts have been made.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  cy_rslt_t: Returns CY_RSLT_SUCCESS if the HTTP server is configured
 *  successfully, otherwise, it returns CY_RSLT_TYPE_ERROR.
 *
 *******************************************************************************/
cy_rslt_t start_sta_mode()
{
    cy_rslt_t result;
    cy_wcm_connect_params_t connect_param;
    cy_wcm_ip_address_t ip_address;
    bool wifi_conct_stat = false;

    /*Disconnect from the currently connected AP if any*/
    wifi_conct_stat = cy_wcm_is_connected_to_ap();
    if (wifi_conct_stat)
    {
        cy_wcm_disconnect_ap();
    }

    memset(&connect_param, 0, sizeof(cy_wcm_connect_params_t));
    memset(&ip_address, 0, sizeof(cy_wcm_ip_address_t));

    memcpy(connect_param.ap_credentials.SSID, wifi_ssid, sizeof(wifi_ssid));
    memcpy(connect_param.ap_credentials.password, wifi_pwd, sizeof(wifi_pwd));
    connect_param.ap_credentials.security = CY_WCM_SECURITY_WPA2_AES_PSK;

    /* Attempt to connect to Wi-Fi until a connection is made or
     * MAX_WIFI_RETRY_COUNT attempts have been made.
     */
    for (uint32_t conn_retries = 0; conn_retries < MAX_WIFI_RETRY_COUNT; conn_retries++)
    {
        result = cy_wcm_connect_ap(&connect_param, &ip_address);
        if (result == CY_RSLT_SUCCESS)
        {
            APP_INFO(("Successfully connected to Wi-Fi network '%s'.\n", connect_param.ap_credentials.SSID));
            break;
        }
        ERR_INFO(("Connection to Wi-Fi network failed with error code %d. Retrying in %d ms...\n", (int)result, WIFI_CONN_RETRY_INTERVAL_MSEC));

        cy_rtos_delay_milliseconds(WIFI_CONN_RETRY_INTERVAL_MSEC);
    }

    return result;
}

/*******************************************************************************
 * Function Name: configure_http_server
 *******************************************************************************
 * Summary:
 *  The function registers a softap_resource_handler to handle HTTP requests
 *  received by http_ap_server.
 *
 * Parameters:
 *  void
 *
 * Return:
 *  cy_rslt_t: Returns CY_RSLT_SUCCESS if the HTTP server is configured
 *  successfully, otherwise, it returns CY_RSLT_TYPE_ERROR.
 *
 *******************************************************************************/
cy_rslt_t configure_http_server(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    cy_wcm_ip_address_t ip_addr;

    /* Holds the response handler for HTTP GET and POST request from the client. */
    cy_resource_dynamic_data_t http_get_post_resource;

    /* IP address of SoftAp. */
    result = cy_wcm_get_ip_addr(CY_WCM_INTERFACE_TYPE_AP, &ip_addr);
    PRINT_AND_ASSERT(result, "cy_wcm_get_ip_addr failed for creating HTTP server...! \n");

    http_server_ip_address.ip_address.ip.v4 = ip_addr.ip.v4;
    http_server_ip_address.ip_address.version = CY_SOCKET_IP_VER_V4;

    /* Add IP address information to network interface object. */
    nw_interface.object = (void *)&http_server_ip_address;
    nw_interface.type = CY_NW_INF_TYPE_WIFI;

    /* Initialize secure socket library. */
    result = cy_http_server_network_init();

    /* Allocate memory needed for secure HTTP server. */
    result = cy_http_server_create(&nw_interface, HTTP_PORT, MAX_SOCKETS, NULL, &http_ap_server);
    PRINT_AND_ASSERT(result, "Failed to allocate memory for the HTTP server.\n");

    /* Configure dynamic resource handler. */
    http_get_post_resource.resource_handler = softap_resource_handler;
    http_get_post_resource.arg = NULL;

    /* Register all the resources with the secure HTTP server. */
    result = cy_http_server_register_resource(http_ap_server,
                                              (uint8_t *)"/",
                                              (uint8_t *)"text/html",
                                              CY_DYNAMIC_URL_CONTENT,
                                              &http_get_post_resource);
    PRINT_AND_ASSERT(result, "Failed to register a resource.\n");

    return result;
}

/*******************************************************************************
 * Function Name: url_decode
 ********************************************************************************
 *
 * Summary:
 * This function is used to decode the url encoded HTTP data received.
 *
 * Parameters:
 * char* dst: Pointer to to whch decoded data should be copied.
 * const uint8_t* src: Pointer to url encoded data .
 *
 * Return:
 * void: Returns void .
 *
 *******************************************************************************/
void url_decode(char *dst, const uint8_t *src)
{
    char current_char, next_char;
    /* Ensure that the received character is a valid character. */
    while ((*src) && ((*src) < VALID_CHARACTER_ASCII_VALUE))
    {
        /* URL encoding replaces unsafe ASCII characters with a "%" followed
         * by two hexadecimal digits. Check for character "%" followed by
         * two hexadecimal digits to decode unsafe ASCII characters.
         */
        if ((*src == MODUS_OPERATOR_ASCII_VALUE) && ((current_char = src[1]) && (next_char = src[2])) && (isxdigit(current_char) && isxdigit(next_char)))
        {
            if (current_char >= SMALL_LETTER_A_ASCII_VALUE)
            {
                current_char -= SMALL_LETTER_A_ASCII_VALUE - CAPITAL_LETTER_A_ASCII_VALUE;
            }
            if (current_char >= CAPITAL_LETTER_A_ASCII_VALUE)
            {
                current_char -= (CAPITAL_LETTER_A_ASCII_VALUE - LF_OPERATOR_ASCII_VALUE);
            }
            else
            {
                current_char -= NUMBER_ZERO_ASCII_VALUE;
            }
            if (next_char >= SMALL_LETTER_A_ASCII_VALUE)
            {
                next_char -= SMALL_LETTER_A_ASCII_VALUE - CAPITAL_LETTER_A_ASCII_VALUE;
            }
            if (next_char >= CAPITAL_LETTER_A_ASCII_VALUE)
            {
                next_char -= (CAPITAL_LETTER_A_ASCII_VALUE - LF_OPERATOR_ASCII_VALUE);
            }
            else
            {
                next_char -= NUMBER_ZERO_ASCII_VALUE;
            }
            *dst++ = DLE_OPERATOR_ASCII_VALUE * current_char + next_char;
            src += URL_DECODE_ASCII_OFFSET_VALUE;
        }
        /* A space character is URL encoded as "+". Decode space character. */
        else if (*src == PLUS_OPERATOR_ASCII_VALUE)
        {
            *dst++ = SPACE_CHARACTER_ASCII_VALUE;
            src++;
        }
        /* Decode other characters. */
        else
        {
            *dst++ = *src++;
        }
    }

    *dst++ = NULL_CHARACTER_ASCII_VALUE;
}

/*******************************************************************************
 * Function Name: server_task
 ********************************************************************************
 * Summary:
 *  Task that initializes the device as SoftAp, and starts the HTTP server
 *
 * Parameters:
 *  arg - Unused.
 *
 * Return:
 *  None.
 *
 *******************************************************************************/
void server_task(cy_thread_arg_t arg)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    (void)arg;

    /* Initialize the Wi-Fi device as a STA.*/
    cy_wcm_config_t config = {.interface = CY_WCM_INTERFACE_TYPE_AP_STA};

    /* Initialize the Wi-Fi device, Wi-Fi transport, and lwIP network stack.*/
    result = cy_wcm_init(&config);
    PRINT_AND_ASSERT(result, "cy_wcm_init failed...!\n");

    result = start_ap_mode();
    PRINT_AND_ASSERT(result, "start SoftAP failed...!\n");

    result = configure_http_server();
    PRINT_AND_ASSERT(result, "Failed to configure the HTTP server...!\n");

    /* Start the HTTP server. */
    result = cy_http_server_start(http_ap_server);
    PRINT_AND_ASSERT(result, "Failed to start the HTTP server.\n");

    display_configuration();

    /* Waits for queue message to register a new HTTP page resource.*/
    while (true)
    {
        cy_rtos_delay_milliseconds(2000);
    }
}

/*******************************************************************************
 * Function Name: display_configuration
 ********************************************************************************
 * Summary:
 *  Displays details 
 *
 * Parameters:
 *  void
 *
 * Return:
 *  void
 *
 *******************************************************************************/
void display_configuration(void)
{
    cy_wcm_ip_address_t ip_address;
    cy_rslt_t result = CY_RSLT_SUCCESS;

    uint32_t ip_addr;
    char display_ip_buffer[DISPLAY_BUFFER_LENGTH];
    char http_url[URL_LENGTH] = {0};

    /* IP address of SoftAp. */
    result = cy_wcm_get_ip_addr(CY_WCM_INTERFACE_TYPE_AP, &ip_address);
    PRINT_AND_ASSERT(result, "Failed to retrieveSoftAP IP address\n");

    /*Print message to connect to that ip address*/
    ip_addr = ip_address.ip.v4;
    sprintf(display_ip_buffer, "%u.%u.%u.%u", (unsigned char)((ip_addr >> 0) & 0xff),
            (unsigned char)((ip_addr >> 8) & 0xff),
            (unsigned char)((ip_addr >> 16) & 0xff),
            (unsigned char)((ip_addr >> 24) & 0xff));

    sprintf(http_url, "http://%s:%d", display_ip_buffer, HTTP_PORT);

    APP_INFO(("****************************************************************************\r\n"));
    APP_INFO(("Using another device, connect to the following Wi-Fi network:\r\n"));
    APP_INFO(("SSID     : %s\r\n", SOFTAP_SSID));
    APP_INFO(("Password : %s\r\n", SOFTAP_PASSWORD));
    APP_INFO(("Open a web browser of your choice and enter the URL %s\r\n", http_url));
    APP_INFO(("This opens up the the home page for web server application.\r\n"));
    APP_INFO(("You can enter Wi-Fi network name and password directly\r\n"));
    APP_INFO(("****************************************************************************\r\n"));
}

/* [] END OF FILE */
