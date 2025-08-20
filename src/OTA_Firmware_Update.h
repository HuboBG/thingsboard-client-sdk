#ifndef OTA_Firmware_Update_h
#define OTA_Firmware_Update_h

// Local includes.
#include "Attribute_Request.h"
#include "Shared_Attribute_Update.h"
#include "OTA_Handler.h"
#include "IAPI_Implementation.h"

#if THINGSBOARD_ENABLE_STL
#include <functional>  // for std::bind
#endif

// caps for cached firmware identity (adjust to your needs)
static constexpr size_t MAX_FW_TITLE_LEN = 96;
static constexpr size_t MAX_FW_VERSION_LEN = 48;

// Keys & messages
static constexpr uint8_t OTA_ATTRIBUTE_KEYS_AMOUNT = 5U;
static constexpr char NO_FW_REQUEST_RESPONSE[] =
    "Did not receive requested shared attribute firmware keys. Ensure keys exist and device is connected";

static constexpr char CURR_FW_TITLE_KEY[] = "current_fw_title";
static constexpr char CURR_FW_VER_KEY[] = "current_fw_version";
static constexpr char FW_DEVICE_NAME_KEY[] = "deviceName";
static constexpr char FW_DEVICE_PROFILE_KEY[] = "deviceProfile";
static constexpr char FW_ERROR_KEY[] = "fw_error";
static constexpr char FW_STATE_KEY[] = "fw_state";
static constexpr char FW_VER_KEY[] = "fw_version";
static constexpr char FW_TITLE_KEY[] = "fw_title";
static constexpr char FW_CHKS_KEY[] = "fw_checksum";
static constexpr char FW_CHKS_ALGO_KEY[] = "fw_checksum_algorithm";
static constexpr char FW_SIZE_KEY[] = "fw_size";
static constexpr char CHECKSUM_AGORITM_MD5[] = "MD5";
static constexpr char CHECKSUM_AGORITM_SHA256[] = "SHA256";
static constexpr char CHECKSUM_AGORITM_SHA384[] = "SHA384";
static constexpr char CHECKSUM_AGORITM_SHA512[] = "SHA512";

static constexpr char NUMBER_PRINTF[] = "%u";
static constexpr char NO_FW[] = "Missing shared attribute firmware keys. Ensure you assigned an OTA update with binary";
static constexpr char EMPTY_FW[] = "Received shared attribute firmware keys were NULL";
static constexpr char FW_NOT_FOR_US[] = "Received firmware title (%s) is different and not meant for this device (%s)";
static constexpr char FW_CHKS_ALGO_NOT_SUPPORTED[] = "Received checksum algorithm (%s) is not supported";
static constexpr char NOT_ENOUGH_RAM[] =
    "Temporary allocating more internal client buffer failed, decrease OTA chunk size or decrease overall heap usage";
static constexpr char RESETTING_FAILED[] = "Preparing for OTA firmware updates failed, attributes might be NULL";

#if THINGSBOARD_ENABLE_DEBUG
static constexpr char PAGE_BREAK[] = "=================================";
static constexpr char NEW_FW[] = "A new Firmware is available:";
static constexpr char FROM_TOO[] = "(%s) => (%s)";
static constexpr char DOWNLOADING_FW[] = "Attempting to download over MQTT...";
#endif

// ---- MQTT topic formats (runtime-built from device access token) ----
// static constexpr char FIRMWARE_REQUEST_FMT[] = "v3/fw/request/%s/%s/%s/chunk/%u"; // token/title/version/chunk
static constexpr char FIRMWARE_REQUEST_FMT[] = "v3/fw/request/by-name/%s/%s/%s/chunk/%u"; // title/version/chunk

static constexpr char FW_RESPONSE_SUBSCRIBE_FMT[] = "v3/fw/response/by-name/%s/chunk/+";
static constexpr char FW_RESPONSE_PREFIX_FMT[] = "v3/fw/response/by-name/%s/chunk/";

// single shared stack buffer size for topics (token is typically <= 64)
// static constexpr size_t TOPIC_BUF_SIZE = 192;

// --------------------------------------------------------------------------------------
// Concrete adapter so Attribute_Request is not abstract (adds SetDeviceID/SetDeviceAccessToken)
// --------------------------------------------------------------------------------------
#if !THINGSBOARD_ENABLE_DYNAMIC
template <typename Logger>
class Attribute_Request_Concrete final : public Attribute_Request<1U, OTA_ATTRIBUTE_KEYS_AMOUNT, Logger>
{
public:
    using Base = Attribute_Request<1U, OTA_ATTRIBUTE_KEYS_AMOUNT, Logger>;

    Attribute_Request_Concrete() : Base()
    {
    }

    void SetDeviceID(char const* /*device_id*/) override
    {
    }

    void SetDeviceProfile(char const* /*device_profile*/) override
    {
    }

    void SetDeviceAccessToken(char const* /*access_token*/) override
    {
    }
};
#else
template <typename Logger>
class Attribute_Request_Concrete : public Attribute_Request<Logger>
{
public:
    using Base = Attribute_Request<Logger>;

    Attribute_Request_Concrete() : Base()
    {
    }

    void SetDeviceID(char const* /*device_id*/) override
    {
    }

    void SetDeviceProfile(char const* /*device_profile*/) override
    {
    }

    void SetDeviceAccessToken(char const* /*access_token*/) override
    {
    }
};
#endif

/// @brief OTA over MQTT
template <typename Logger = DefaultLogger>
class OTA_Firmware_Update final : public IAPI_Implementation
{
public:
    OTA_Firmware_Update()
        : m_deviceId(nullptr)
          , m_deviceProfile(nullptr)
          , m_deviceToken(nullptr)
#if THINGSBOARD_ENABLE_STL
          , m_ota(std::bind(&OTA_Firmware_Update::Publish_Chunk_Request, this,
                            std::placeholders::_1, std::placeholders::_2),
                  std::bind(&OTA_Firmware_Update::Firmware_Send_State,
                            this
                            ,
                            std::placeholders::_1
                            ,
                            std::placeholders::_2
                  )
                  ,
                  std::bind(&OTA_Firmware_Update::Firmware_OTA_Unsubscribe
                            ,
                            this
                  )
          )
#else
    , m_ota(OTA_Firmware_Update::staticPublishChunk,
            OTA_Firmware_Update::staticFirmwareSend,
            OTA_Firmware_Update::staticUnsubscribe)
#endif
    {
#if !THINGSBOARD_ENABLE_STL
        m_subscribedInstance = nullptr;
#endif
    }

    // ---------- identity setters ----------

    void SetDeviceID(char const* device_id) override
    {
        m_deviceId = device_id;
        Serial.println("SetDeviceID: " + String(m_deviceId));

        // forward to inner helpers so they can build topics like sensor/<id>/attributes
        m_fw_attribute_update.SetDeviceID(device_id);
        m_fw_attribute_request.SetDeviceID(device_id);
    }
    void SetDeviceProfile(char const* device_profile) override
    {
        m_deviceProfile = device_profile;
        Serial.println("SetDeviceProfile: " + String(device_profile));

        // forward to inner helpers so they can build topics like sensor/<id>/attributes
        m_fw_attribute_update.SetDeviceProfile(device_profile);
        m_fw_attribute_request.SetDeviceProfile(device_profile);
    }

    void SetDeviceAccessToken(char const* token) override
    {
        m_deviceToken = token;
        Serial.println("SetDeviceAccessToken: " + String(token));

        // forward token as well (kept for future use / symmetry)
        m_fw_attribute_update.SetDeviceAccessToken(token);
        m_fw_attribute_request.SetDeviceAccessToken(token);
    }

    // Expose firmware identity captured from attributes
    char m_fw_title[MAX_FW_TITLE_LEN] = {};
    char m_fw_version[MAX_FW_VERSION_LEN] = {};

    // ---------- start / subscribe ----------
    bool Start_Firmware_Update(OTA_Update_Callback const& callback)
    {
        if (!Prepare_Firmware_Settings(callback))
        {
            Logger::printfln(RESETTING_FAILED);
            return false;
        }

        constexpr char const* keys[OTA_ATTRIBUTE_KEYS_AMOUNT] =
            {FW_CHKS_KEY, FW_CHKS_ALGO_KEY, FW_SIZE_KEY, FW_TITLE_KEY, FW_VER_KEY};

#if THINGSBOARD_ENABLE_DYNAMIC
#if THINGSBOARD_ENABLE_STL
        const Attribute_Request_Callback fw_request_cb(
            std::bind(&OTA_Firmware_Update::Firmware_Shared_Attribute_Received, this, std::placeholders::_1),
            callback.Get_Timeout(),
            std::bind(&OTA_Firmware_Update::Request_Timeout, this),
            keys + 0U, keys + OTA_ATTRIBUTE_KEYS_AMOUNT);
#else
        const Attribute_Request_Callback fw_request_cb(
            OTA_Firmware_Update::onStaticFirmwareReceived,
            callback.Get_Timeout(),
            OTA_Firmware_Update::onStaticRequestTimeout,
            keys + 0U, keys + OTA_ATTRIBUTE_KEYS_AMOUNT);
#endif
#else
#if THINGSBOARD_ENABLE_STL
        const Attribute_Request_Callback<OTA_ATTRIBUTE_KEYS_AMOUNT> fw_request_cb(
            std::bind(&OTA_Firmware_Update::Firmware_Shared_Attribute_Received, this, std::placeholders::_1),
            callback.Get_Timeout(),
            std::bind(&OTA_Firmware_Update::Request_Timeout, this),
            keys + 0U, keys + OTA_ATTRIBUTE_KEYS_AMOUNT);
#else
        const Attribute_Request_Callback<OTA_ATTRIBUTE_KEYS_AMOUNT> fw_request_cb(
            OTA_Firmware_Update::onStaticFirmwareReceived,
            callback.Get_Timeout(),
            OTA_Firmware_Update::onStaticRequestTimeout,
            keys + 0U, keys + OTA_ATTRIBUTE_KEYS_AMOUNT);
#endif
#endif
        return m_fw_attribute_request.Shared_Attributes_Request(fw_request_cb);
    }

    bool Subscribe_Firmware_Update(OTA_Update_Callback const& callback)
    {
        Serial.println("Subscribe_Firmware_Update");

        if (!Prepare_Firmware_Settings(callback))
        {
            Logger::printfln(RESETTING_FAILED);
            return false;
        }

        char const* keys[OTA_ATTRIBUTE_KEYS_AMOUNT] =
            {FW_CHKS_KEY, FW_CHKS_ALGO_KEY, FW_SIZE_KEY, FW_TITLE_KEY, FW_VER_KEY};

#if THINGSBOARD_ENABLE_DYNAMIC
#if THINGSBOARD_ENABLE_STL
        const Shared_Attribute_Callback fw_update_cb(
            std::bind(&OTA_Firmware_Update::Firmware_Shared_Attribute_Received, this, std::placeholders::_1),
            keys + 0U, keys + OTA_ATTRIBUTE_KEYS_AMOUNT);
#else
        const Shared_Attribute_Callback fw_update_cb(
            OTA_Firmware_Update::onStaticFirmwareReceived,
            keys + 0U, keys + OTA_ATTRIBUTE_KEYS_AMOUNT);
#endif
#else
#if THINGSBOARD_ENABLE_STL
        const Shared_Attribute_Callback<OTA_ATTRIBUTE_KEYS_AMOUNT> fw_update_cb(
            std::bind(&OTA_Firmware_Update::Firmware_Shared_Attribute_Received, this, std::placeholders::_1),
            keys + 0U, keys + OTA_ATTRIBUTE_KEYS_AMOUNT);
#else
        const Shared_Attribute_Callback<OTA_ATTRIBUTE_KEYS_AMOUNT> fw_update_cb(
            OTA_Firmware_Update::onStaticFirmwareReceived,
            keys + 0U, keys + OTA_ATTRIBUTE_KEYS_AMOUNT);
#endif
#endif
        return m_fw_attribute_update.Shared_Attributes_Subscribe(fw_update_cb);
    }

    void Stop_Firmware_Update() { m_ota.Stop_Firmware_Update(); }

    // ---------- telemetry helpers ----------
    bool Firmware_Send_Info(char const* current_fw_title, char const* current_fw_version) const
    {
        Serial.println("Firmware_Send_Info");

        StaticJsonDocument<JSON_OBJECT_SIZE(4)> doc;
        doc[CURR_FW_TITLE_KEY] = current_fw_title;
        doc[CURR_FW_VER_KEY] = current_fw_version;
        doc[FW_DEVICE_NAME_KEY] = m_deviceId;
        doc[FW_DEVICE_PROFILE_KEY] = m_deviceProfile;
        return m_send_json_callback.Call_Callback(TELEMETRY_TOPIC, doc, Helper::Measure_Json(doc));
    }

    bool Firmware_Send_State(char const* current_fw_state, char const* fw_error = "") const
    {
        Serial.println("Firmware_Send_State: " + String(current_fw_state) + ", error: " + String(fw_error));

        StaticJsonDocument<JSON_OBJECT_SIZE(4)> doc;
        doc[FW_ERROR_KEY] = fw_error;
        doc[FW_STATE_KEY] = current_fw_state;
        doc[FW_DEVICE_NAME_KEY] = m_deviceId;
        doc[FW_DEVICE_PROFILE_KEY] = m_deviceProfile;
        return m_send_json_callback.Call_Callback(TELEMETRY_TOPIC, doc, Helper::Measure_Json(doc));
    }

    // ---------- IAPI_Implementation ----------
    API_Process_Type Get_Process_Type() const override { return API_Process_Type::RAW; }

    void Process_Response(char const* topic, uint8_t* payload, unsigned int length) override
    {
        Serial.println(String("OTA Process_Response: ") + topic);

        char prefix[TOPIC_BUF_SIZE];
        Build_Response_Prefix(prefix, sizeof(prefix));

        if (strncmp(prefix, topic, strlen(prefix)) != 0) return;

        char const* chunk_str = topic + strlen(prefix);
        const unsigned long chunk = strtoul(chunk_str, nullptr, 10);

        Serial.println(String("OTA chunk=") + chunk);
        m_ota.Process_Firmware_Packet(static_cast<size_t>(chunk), payload, length);
    }

    void Process_Json_Response(char const* /*topic*/, JsonDocument const& /*data*/) override
    {
        Serial.println("Process_Json_Response (unused for OTA)");
    }

    bool Compare_Response_Topic(char const* topic) const override
    {
        char prefix[TOPIC_BUF_SIZE];
        Build_Response_Prefix(prefix, sizeof(prefix));
        return strncmp(prefix, topic, strlen(prefix)) == 0;
    }

    bool Unsubscribe() override
    {
        Serial.println("OTA Unsubscribe");

        Stop_Firmware_Update();
        return Firmware_OTA_Unsubscribe();
    }

    bool Resubscribe_Topic() override
    {
        Serial.println("OTA Resubscribe_Topic");

        return Firmware_OTA_Subscribe();
    }

#if !THINGSBOARD_USE_ESP_TIMER
    void loop() override { m_ota.update(); }
#endif

    void Initialize() override
    {
        m_subscribe_api_callback.Call_Callback(m_fw_attribute_update);
        m_subscribe_api_callback.Call_Callback(m_fw_attribute_request);
    }

    void Set_Client_Callbacks(const Callback<void, IAPI_Implementation&>::function subscribe_api_callback,
                              const Callback<bool, char const* const, JsonDocument const&, size_t const&>::function
                              send_json_callback,
                              const Callback<bool, char const* const, char const* const>::function
                              send_json_string_callback,
                              const Callback<bool, char const* const>::function subscribe_topic_callback,
                              const Callback<bool, char const* const>::function unsubscribe_topic_callback,
                              const Callback<uint16_t>::function get_receive_size_callback,
                              const Callback<uint16_t>::function get_send_size_callback,
                              const Callback<bool, uint16_t, uint16_t>::function set_buffer_size_callback,
                              const Callback<size_t*>::function get_request_id_callback) override
    {
        m_subscribe_api_callback.Set_Callback(subscribe_api_callback);
        m_send_json_callback.Set_Callback(send_json_callback);
        m_send_json_string_callback.Set_Callback(send_json_string_callback);
        m_subscribe_topic_callback.Set_Callback(subscribe_topic_callback);
        m_unsubscribe_topic_callback.Set_Callback(unsubscribe_topic_callback);
        m_get_receive_size_callback.Set_Callback(get_receive_size_callback);
        m_get_send_size_callback.Set_Callback(get_send_size_callback);
        m_set_buffer_size_callback.Set_Callback(set_buffer_size_callback);
        m_get_request_id_callback.Set_Callback(get_request_id_callback);
    }

private:
    // ---------- internals ----------
    bool Prepare_Firmware_Settings(OTA_Update_Callback const& callback)
    {
        Serial.println("Prepare_Firmware_Settings");

        /*if (Helper::stringIsNullorEmpty(m_deviceToken))
        {
            Logger::printfln("Device access token not set");
            return false;
        }*/

        char const* current_fw_title = callback.Get_Firmware_Title();
        char const* current_fw_version = callback.Get_Firmware_Version();
        if (Helper::stringIsNullorEmpty(current_fw_title) || Helper::stringIsNullorEmpty(current_fw_version))
        {
            return false;
        }
        if (!Firmware_Send_Info(current_fw_title, current_fw_version))
        {
            return false;
        }

        size_t* p_request_id = m_get_request_id_callback.Call_Callback();
        if (p_request_id == nullptr)
        {
            Logger::printfln(REQUEST_ID_NULL);
            return false;
        }
        auto& request_id = *p_request_id;

        m_fw_callback = callback;
        m_fw_callback.Set_Request_ID(++request_id);
        return true;
    }

    bool Firmware_OTA_Subscribe()
    {
        char subscribeTopic[TOPIC_BUF_SIZE];
        Build_Response_Subscribe(subscribeTopic, sizeof(subscribeTopic));

        Serial.println("Firmware_OTA_Subscribe: " + String(subscribeTopic));

        if (!m_subscribe_topic_callback.Call_Callback(subscribeTopic))
        {
            Logger::printfln(SUBSCRIBE_TOPIC_FAILED, subscribeTopic);
            // ReSharper disable once CppExpressionWithoutSideEffects
            Firmware_Send_State(FW_STATE_FAILED, SUBSCRIBE_TOPIC_FAILED);
            return false;
        }
        return true;
    }

    bool Firmware_OTA_Unsubscribe()
    {
        Serial.println("Firmware_OTA_Unsubscribe");

        // ReSharper disable once CppDFAConstantConditions
        if (m_changed_buffer_size)
        {
            // ReSharper disable once CppDFAUnreachableCode
            (void)m_set_buffer_size_callback.Call_Callback(
                m_previous_buffer_size,
                m_get_send_size_callback.Call_Callback());
        }
        m_fw_callback = OTA_Update_Callback();

        char subscribeTopic[TOPIC_BUF_SIZE];
        Build_Response_Subscribe(subscribeTopic, sizeof(subscribeTopic));
        return m_unsubscribe_topic_callback.Call_Callback(subscribeTopic);
    }

    bool Publish_Chunk_Request(size_t const& request_id, size_t const& request_chunk)
    {
        (void)request_id; // v3 token-based API doesn't use request_id in the topic
        Serial.println("Publish_Chunk_Request");

        if (Helper::stringIsNullorEmpty(m_deviceId) ||
            Helper::stringIsNullorEmpty(m_fw_title) ||
            Helper::stringIsNullorEmpty(m_fw_version))
        {
            Logger::printfln("Missing device_id/title/version for chunk request");
            return false;
        }

        uint16_t const& chunk_size = m_fw_callback.Get_Chunk_Size();

        char sizeStr[Helper::detectSize(NUMBER_PRINTF, chunk_size)] = {};
        (void)snprintf(sizeStr, sizeof(sizeStr), NUMBER_PRINTF, chunk_size);

        char topic[Helper::detectSize(FIRMWARE_REQUEST_FMT, m_deviceId, m_fw_title, m_fw_version,
                                      static_cast<unsigned>(request_chunk))] = {};
        (void)snprintf(topic, sizeof(topic), FIRMWARE_REQUEST_FMT,
                       m_deviceId, m_fw_title, m_fw_version,
                       static_cast<unsigned>(request_chunk));

        return m_send_json_string_callback.Call_Callback(topic, sizeStr);
    }

    void Request_Timeout()
    {
        Serial.println("Request_Timeout");
        Logger::printfln(NO_FW_REQUEST_RESPONSE);
        // ReSharper disable once CppExpressionWithoutSideEffects
        Firmware_Send_State(FW_STATE_FAILED, NO_FW_REQUEST_RESPONSE);
    }

    void Firmware_Shared_Attribute_Received(JsonObjectConst const& data)
    {
        Serial.println("Firmware_Shared_Attribute_Received");

        if (!data.containsKey(FW_VER_KEY) || !data.containsKey(FW_TITLE_KEY) ||
            !data.containsKey(FW_CHKS_KEY) || !data.containsKey(FW_CHKS_ALGO_KEY) ||
            !data.containsKey(FW_SIZE_KEY))
        {
            Logger::printfln(NO_FW);
            // ReSharper disable once CppExpressionWithoutSideEffects
            Firmware_Send_State(FW_STATE_FAILED, NO_FW);
            return;
        }

        char const* fw_title = data[FW_TITLE_KEY];
        char const* fw_version = data[FW_VER_KEY];
        char const* fw_checksum = data[FW_CHKS_KEY];
        char const* fw_algorithm = data[FW_CHKS_ALGO_KEY];
        size_t const fw_size = data[FW_SIZE_KEY];

        char const* curr_fw_title = m_fw_callback.Get_Firmware_Title();
        char const* curr_fw_version = m_fw_callback.Get_Firmware_Version();

        if (!fw_title || !fw_version || !curr_fw_title || !curr_fw_version || !fw_algorithm || !fw_checksum)
        {
            Logger::printfln(EMPTY_FW);
            // ReSharper disable once CppExpressionWithoutSideEffects
            Firmware_Send_State(FW_STATE_FAILED, EMPTY_FW);
            return;
        }
        if (strncmp(curr_fw_title, fw_title, strlen(curr_fw_title)) == 0 &&
            strncmp(curr_fw_version, fw_version, strlen(curr_fw_version)) == 0)
        {
            // ReSharper disable once CppExpressionWithoutSideEffects
            Firmware_Send_State(FW_STATE_UPDATED);
            return;
        }
        if (strncmp(curr_fw_title, fw_title, strlen(curr_fw_title)) != 0)
        {
            char message[strlen(FW_NOT_FOR_US) + strlen(fw_title) + strlen(curr_fw_title) + 3] = {};
            (void)snprintf(message, sizeof(message), FW_NOT_FOR_US, fw_title, curr_fw_title);
            Logger::printfln(message);
            // ReSharper disable once CppExpressionWithoutSideEffects
            Firmware_Send_State(FW_STATE_FAILED, message);
            return;
        }

        auto fw_checksum_algorithm = mbedtls_md_type_t{};
        if (strncmp(CHECKSUM_AGORITM_MD5, fw_algorithm, strlen(CHECKSUM_AGORITM_MD5)) == 0)
            fw_checksum_algorithm =
                MBEDTLS_MD_MD5;
        else if (strncmp(CHECKSUM_AGORITM_SHA256, fw_algorithm, strlen(CHECKSUM_AGORITM_SHA256)) == 0)
            fw_checksum_algorithm = MBEDTLS_MD_SHA256;
        else if (strncmp(CHECKSUM_AGORITM_SHA384, fw_algorithm, strlen(CHECKSUM_AGORITM_SHA384)) == 0)
            fw_checksum_algorithm = MBEDTLS_MD_SHA384;
        else if (strncmp(CHECKSUM_AGORITM_SHA512, fw_algorithm, strlen(CHECKSUM_AGORITM_SHA512)) == 0)
            fw_checksum_algorithm = MBEDTLS_MD_SHA512;
        else
        {
            char message[strlen(FW_CHKS_ALGO_NOT_SUPPORTED) + strlen(fw_algorithm) + 2] = {};
            (void)snprintf(message, sizeof(message), FW_CHKS_ALGO_NOT_SUPPORTED, fw_algorithm);
            Logger::printfln(message);
            // ReSharper disable once CppExpressionWithoutSideEffects
            Firmware_Send_State(FW_STATE_FAILED, message);
            return;
        }

        // subscribe for chunks for this token
        if (!Firmware_OTA_Subscribe())
        {
            m_fw_callback.Call_Callback(false);
            return;
        }

#if THINGSBOARD_ENABLE_DEBUG
        Logger::printfln(PAGE_BREAK);
        Logger::printfln(NEW_FW);
        char firmware[strlen(FROM_TOO) + strlen(curr_fw_version) + strlen(fw_version) + 3] = {};
        (void)snprintf(firmware, sizeof(firmware), FROM_TOO, curr_fw_version, fw_version);
        Logger::printfln(firmware);
        Logger::printfln(DOWNLOADING_FW);
#endif

        // cache for requests
        strncpy(m_fw_title, fw_title, sizeof(m_fw_title) - 1);
        m_fw_title[sizeof(m_fw_title) - 1] = '\0';
        strncpy(m_fw_version, fw_version, sizeof(m_fw_version) - 1);
        m_fw_version[sizeof(m_fw_version) - 1] = '\0';

        // buffer sizing for larger chunks
        const uint16_t& chunk_size = m_fw_callback.Get_Chunk_Size();
        m_previous_buffer_size = m_get_receive_size_callback.Call_Callback();

        // m_changed_buffer_size = m_previous_buffer_size < chunk_size + 50U;
        const uint16_t need = chunk_size + 128U; // a bit of margin
        m_changed_buffer_size = m_previous_buffer_size < need;

        // if (m_changed_buffer_size &&
        //     !m_set_buffer_size_callback.Call_Callback(chunk_size + 50U, m_get_send_size_callback.Call_Callback()))
        // {
        if (m_changed_buffer_size &&
            !m_set_buffer_size_callback.Call_Callback(need, m_get_send_size_callback.Call_Callback()))
        {
            Logger::printfln(NOT_ENOUGH_RAM);
            // ReSharper disable once CppExpressionWithoutSideEffects
            Firmware_Send_State(FW_STATE_FAILED, NOT_ENOUGH_RAM);
            m_fw_callback.Call_Callback(false);
            return;
        }

        m_ota.Start_Firmware_Update(m_fw_callback, fw_size, fw_checksum, fw_checksum_algorithm);
    }

    // ----- topic builders -----
    // Returns needed size including NUL when out==nullptr
    size_t Build_Response_Subscribe(char* out, const size_t outLen) const
    {
        char const* deviceId = m_deviceId && *m_deviceId ? m_deviceId : "unknown";
        const int need = snprintf(nullptr, 0, FW_RESPONSE_SUBSCRIBE_FMT, deviceId) + 1;
        if (out && outLen) { (void)snprintf(out, outLen, FW_RESPONSE_SUBSCRIBE_FMT, deviceId); }
        return static_cast<size_t>(need);
    }

    size_t Build_Response_Prefix(char* out, const size_t outLen) const
    {
        char const* deviceId = m_deviceId && *m_deviceId ? m_deviceId : "unknown";
        const int need = snprintf(nullptr, 0, FW_RESPONSE_PREFIX_FMT, deviceId) + 1;
        if (out && outLen) { (void)snprintf(out, outLen, FW_RESPONSE_PREFIX_FMT, deviceId); }
        return static_cast<size_t>(need);
    }

#if !THINGSBOARD_ENABLE_STL
    // static trampolines
    static void onStaticFirmwareReceived(JsonDocument const& data)
    {
        if (m_subscribedInstance) m_subscribedInstance->Firmware_Shared_Attribute_Received(data.as<JsonObjectConst>());
    }
    static void onStaticRequestTimeout()
    {
        if (m_subscribedInstance) m_subscribedInstance->Request_Timeout();
    }
    static bool staticPublishChunk(size_t const& request_id, size_t const& request_chunk)
    {
        return m_subscribedInstance ? m_subscribedInstance->Publish_Chunk_Request(request_id, request_chunk) : false;
    }
    static bool staticFirmwareSend(char const* current_fw_state, char const* fw_error = nullptr)
    {
        return m_subscribedInstance ? m_subscribedInstance->Firmware_Send_State(current_fw_state, fw_error) : false;
    }
    static bool staticUnsubscribe()
    {
        return m_subscribedInstance ? m_subscribedInstance->Firmware_OTA_Unsubscribe() : false;
    }
    static OTA_Firmware_Update* m_subscribedInstance;
#endif

    // callbacks to core client
    Callback<void, IAPI_Implementation&> m_subscribe_api_callback = {};
    Callback<bool, char const* const, JsonDocument const&, size_t const&> m_send_json_callback = {};
    Callback<bool, char const* const, char const* const> m_send_json_string_callback = {};
    Callback<bool, char const* const> m_subscribe_topic_callback = {};
    Callback<bool, char const* const> m_unsubscribe_topic_callback = {};
    Callback<uint16_t> m_get_receive_size_callback = {};
    Callback<uint16_t> m_get_send_size_callback = {};
    Callback<bool, uint16_t, uint16_t> m_set_buffer_size_callback = {};
    Callback<size_t*> m_get_request_id_callback = {};

    OTA_Update_Callback m_fw_callback = {};
    uint16_t m_previous_buffer_size = {};
    bool m_changed_buffer_size = {};
    OTA_Handler<Logger> m_ota; // now correctly constructed

#if !THINGSBOARD_ENABLE_DYNAMIC
    Shared_Attribute_Update<1U, OTA_ATTRIBUTE_KEYS_AMOUNT, Logger> m_fw_attribute_update = {};
    Attribute_Request_Concrete<Logger> m_fw_attribute_request = {};
#else
    Shared_Attribute_Update<Logger> m_fw_attribute_update = {};
    Attribute_Request_Concrete<Logger> m_fw_attribute_request = {};
#endif

    const char* m_deviceId; // non-owning
    const char* m_deviceProfile; // non-owning
    const char* m_deviceToken; // non-owning
};

#if !THINGSBOARD_ENABLE_STL
template <typename Logger>
OTA_Firmware_Update<Logger>* OTA_Firmware_Update<Logger>::m_subscribedInstance = nullptr;
#endif

#endif // OTA_Firmware_Update_h
