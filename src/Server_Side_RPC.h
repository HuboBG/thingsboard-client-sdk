#ifndef Server_Side_RPC_h
#define Server_Side_RPC_h

// Local includes.
#include "RPC_Callback.h"
#include "IAPI_Implementation.h"

#if THINGSBOARD_ENABLE_STL
#include <algorithm>
#endif

// --- Topic format strings (runtime-composed using deviceId) ---
// ThingsBoard defaults (for reference):
// static constexpr char TB_RPC_SUBSCRIBE_FMT[]      = "v1/devices/me/rpc/request/+";
// static constexpr char TB_RPC_REQUEST_PREFIX_FMT[] = "v1/devices/me/rpc/request/";
// static constexpr char TB_RPC_RESPONSE_FMT[]       = "v1/devices/me/rpc/response/%u";

// Custom sensor topics (deviceId injected):
static constexpr char RPC_SUBSCRIBE_FMT[] = "sensor/%s/request/+";
static constexpr char RPC_REQUEST_PREFIX_FMT[] = "sensor/%s/request/";
static constexpr char RPC_RESPONSE_FMT[] = "sensor/%s/response/%u";

// Shared, safe stack buffer for topics (avoid VLAs)
static constexpr size_t TOPIC_BUF_SIZE = 256;

// Log messages.
static constexpr char RPC_RESPONSE_OVERFLOWED[] = "Server-side RPC response overflowed, increase MaxRPC (%u)";
#if !THINGSBOARD_ENABLE_DYNAMIC
static constexpr char SERVER_SIDE_RPC_SUBSCRIPTIONS[] = "server-side RPC";
#endif
#if THINGSBOARD_ENABLE_DEBUG
static constexpr char SERVER_RPC_METHOD_NULL[] = "Server-side RPC method name is NULL";
static constexpr char RPC_RESPONSE_NULL[] = "Response JsonDocument is NULL, skipping sending";
static constexpr char NO_RPC_PARAMS_PASSED[] = "No parameters passed with RPC, passing null JSON";
static constexpr char CALLING_RPC_CB[] = "Calling subscribed callback for rpc with methodname (%s)";
#endif

#if THINGSBOARD_ENABLE_DYNAMIC
template <typename Logger = DefaultLogger>

#else
// See arduinojson.org assistant to size MaxRPC (divide recommended bytes by 16)
template <size_t MaxSubscriptions = Default_Subscriptions_Amount,
          size_t MaxRPC = Default_RPC_Amount,
          typename Logger = DefaultLogger>
#endif
class Server_Side_RPC final : public IAPI_Implementation
{
public:
    /// @brief Constructor
    Server_Side_RPC() = default;

    // ------------ Device identity setters ------------
    void SetDeviceID(char const* id) override { deviceId = id; }
    void SetDeviceProfile(char const* id) override { deviceProfile = id; }
    void SetDeviceAccessToken(char const* tok) override { deviceAccessToken = tok; } // kept for future use

    /// @brief Subscribes multiple RPC callbacks
    template <typename InputIterator>
    bool RPC_Subscribe(InputIterator const& first, InputIterator const& last)
    {
        Serial.println("RPC_Subscribe called");

#if !THINGSBOARD_ENABLE_DYNAMIC
        const size_t size = Helper::distance(first, last);
        if (m_rpc_callbacks.size() + size > m_rpc_callbacks.capacity())
        {
            Logger::printfln(MAX_SUBSCRIPTIONS_EXCEEDED, MAX_SUBSCRIPTIONS_TEMPLATE_NAME,
                             SERVER_SIDE_RPC_SUBSCRIPTIONS);
            return false;
        }
#endif
        char topic[TOPIC_BUF_SIZE];
        Build_Subscribe_Topic(topic, sizeof(topic));
        (void)m_subscribe_topic_callback.Call_Callback(topic);

        m_rpc_callbacks.insert(m_rpc_callbacks.end(), first, last);
        return true;
    }

    /// @brief Subscribe a single RPC callback
    bool RPC_Subscribe(RPC_Callback const& callback)
    {
        Serial.println("RPC_Subscribe 2 called");

#if !THINGSBOARD_ENABLE_DYNAMIC
        if (m_rpc_callbacks.size() + 1 > m_rpc_callbacks.capacity())
        {
            Logger::printfln(MAX_SUBSCRIPTIONS_EXCEEDED, MAX_SUBSCRIPTIONS_TEMPLATE_NAME,
                             SERVER_SIDE_RPC_SUBSCRIPTIONS);
            return false;
        }
#endif
        char topic[TOPIC_BUF_SIZE];
        Build_Subscribe_Topic(topic, sizeof(topic));
        (void)m_subscribe_topic_callback.Call_Callback(topic);

        m_rpc_callbacks.push_back(callback);
        return true;
    }

    /// @brief Unsubscribe all RPC callbacks and topic
    bool RPC_Unsubscribe()
    {
        Serial.println("RPC_Unsubscribe called");
        m_rpc_callbacks.clear();

        char topic[TOPIC_BUF_SIZE];
        Build_Subscribe_Topic(topic, sizeof(topic));
        return m_unsubscribe_topic_callback.Call_Callback(topic);
    }

    API_Process_Type Get_Process_Type() const override { return API_Process_Type::JSON; }

    void Process_Response(char const* /*topic*/, uint8_t* /*payload*/, unsigned int /*length*/) override
    {
        // Nothing to do for raw payload here.
        Serial.println("Process_Response called");
    }

    void Process_Json_Response(char const* topic, JsonDocument const& data) override
    {
        Serial.println("Process_Json_Response called");

        if (!data.containsKey(RPC_METHOD_KEY))
        {
#if THINGSBOARD_ENABLE_DEBUG
            Logger::printfln(SERVER_RPC_METHOD_NULL);
#endif
            return;
        }
        char const* method_name = data[RPC_METHOD_KEY];

#if THINGSBOARD_ENABLE_STL
        auto it = std::find_if(m_rpc_callbacks.begin(), m_rpc_callbacks.end(),
                               [&method_name](RPC_Callback const& rpc)
                               {
                                   char const* subscribedMethodName = rpc.Get_Name();
                                   return !Helper::stringIsNullorEmpty(subscribedMethodName) &&
                                       strncmp(subscribedMethodName, method_name, strlen(subscribedMethodName)) == 0;
                               });
        if (it != m_rpc_callbacks.end())
        {
            auto& rpc = *it;
#else
            for (auto const& rpc : m_rpc_callbacks)
            {
                char const* subscribedMethodName = rpc.Get_Name();
                if (Helper::stringIsNullorEmpty(subscribedMethodName) ||
                    strncmp(subscribedMethodName, method_name, strlen(subscribedMethodName)) != 0)
                {
                    continue;
                }
#endif
#if THINGSBOARD_ENABLE_DEBUG
            if (!data.containsKey(RPC_PARAMS_KEY))
            {
                Logger::printfln(NO_RPC_PARAMS_PASSED);
            }
            Logger::printfln(CALLING_RPC_CB, method_name);
#endif

            JsonVariantConst const param = data[RPC_PARAMS_KEY];

#if THINGSBOARD_ENABLE_DYNAMIC
            size_t const& rpc_response_size = rpc.Get_Response_Size();
            TBJsonDocument json_buffer(rpc_response_size);
#else
            static constexpr size_t rpc_response_size = MaxRPC;
            StaticJsonDocument<JSON_OBJECT_SIZE(MaxRPC)> json_buffer;
#endif
            rpc.Call_Callback(param, json_buffer);

            if (json_buffer.isNull())
            {
#if THINGSBOARD_ENABLE_DEBUG
                Logger::printfln(RPC_RESPONSE_NULL);
#endif
                return;
            }

            if (json_buffer.overflowed())
            {
                Logger::printfln(RPC_RESPONSE_OVERFLOWED, static_cast<unsigned>(rpc_response_size));
                return;
            }

            // Build request prefix, parse request id from current topic
            char reqPrefix[TOPIC_BUF_SIZE];
            Build_Request_Prefix(reqPrefix, sizeof(reqPrefix));
            size_t const request_id = Helper::parseRequestId(reqPrefix, topic);

            // Build response topic with that request id
            char responseTopic[TOPIC_BUF_SIZE];
            Build_Response_Topic(responseTopic, sizeof(responseTopic), request_id);

            (void)m_send_json_callback.Call_Callback(responseTopic, json_buffer, Helper::Measure_Json(json_buffer));
        }
    }

    bool Compare_Response_Topic(char const* topic) const override
    {
        char reqPrefix[TOPIC_BUF_SIZE];
        Build_Request_Prefix(reqPrefix, sizeof(reqPrefix));
        return strncmp(reqPrefix, topic, strlen(reqPrefix)) == 0;
    }

    bool Unsubscribe() override { return RPC_Unsubscribe(); }

    bool Resubscribe_Topic() override
    {
        if (!m_rpc_callbacks.empty())
        {
            char topic[TOPIC_BUF_SIZE];
            Build_Subscribe_Topic(topic, sizeof(topic));
            if (!m_subscribe_topic_callback.Call_Callback(topic))
            {
                Logger::printfln(SUBSCRIBE_TOPIC_FAILED, static_cast<char const*>(topic));
                return false;
            }
        }
        return true;
    }

#if !THINGSBOARD_USE_ESP_TIMER
    void loop() override
    {
        /* nothing */
    }
#endif

    void Initialize() override
    {
        /* nothing */
    }

    void Set_Client_Callbacks(Callback<void, IAPI_Implementation&>::function /*subscribe_api_callback*/,
                              const Callback<bool, char const* const, JsonDocument const&, size_t const&>::function
                              send_json_callback,
                              Callback<bool, char const* const, char const* const>::function
                              /*send_json_string_callback*/,
                              const Callback<bool, char const* const>::function subscribe_topic_callback,
                              const Callback<bool, char const* const>::function unsubscribe_topic_callback,
                              Callback<uint16_t>::function /*get_receive_size_callback*/,
                              Callback<uint16_t>::function /*get_send_size_callback*/,
                              Callback<bool, uint16_t, uint16_t>::function /*set_buffer_size_callback*/,
                              Callback<size_t*>::function /*get_request_id_callback*/) override
    {
        m_send_json_callback.Set_Callback(send_json_callback);
        m_subscribe_topic_callback.Set_Callback(subscribe_topic_callback);
        m_unsubscribe_topic_callback.Set_Callback(unsubscribe_topic_callback);
    }

private:
    // --- Helpers to build topics safely (handles missing deviceId) ---
    size_t Build_Subscribe_Topic(char* out, const size_t outLen) const
    {
        char const* id = deviceId && *deviceId ? deviceId : "unknown";
        const int need = snprintf(nullptr, 0, RPC_SUBSCRIBE_FMT, id) + 1;
        if (out && outLen) { (void)snprintf(out, outLen, RPC_SUBSCRIBE_FMT, id); }
        return static_cast<size_t>(need);
    }

    size_t Build_Request_Prefix(char* out, const size_t outLen) const
    {
        char const* id = deviceId && *deviceId ? deviceId : "unknown";
        const int need = snprintf(nullptr, 0, RPC_REQUEST_PREFIX_FMT, id) + 1;
        if (out && outLen) { (void)snprintf(out, outLen, RPC_REQUEST_PREFIX_FMT, id); }
        return static_cast<size_t>(need);
    }

    size_t Build_Response_Topic(char* out, const size_t outLen, const size_t request_id) const
    {
        char const* id = deviceId && *deviceId ? deviceId : "unknown";
        const int need = snprintf(nullptr, 0, RPC_RESPONSE_FMT, id, static_cast<unsigned>(request_id)) + 1;
        if (out && outLen) { (void)snprintf(out, outLen, RPC_RESPONSE_FMT, id, static_cast<unsigned>(request_id)); }
        return static_cast<size_t>(need);
    }

    // NOTE: we store pointers (no ownership) to avoid heap use on MCUs.
    // Ensure the strings remain valid for the lifetime of this instance.
    const char* deviceId = nullptr;
    const char* deviceProfile = nullptr;
    const char* deviceAccessToken = nullptr;

    // Client callbacks
    Callback<bool, char const* const, JsonDocument const&, size_t const&> m_send_json_callback = {};
    Callback<bool, char const* const> m_subscribe_topic_callback = {};
    Callback<bool, char const* const> m_unsubscribe_topic_callback = {};

#if THINGSBOARD_ENABLE_DYNAMIC
    Vector<RPC_Callback> m_rpc_callbacks = {};
#else
    Array<RPC_Callback, MaxSubscriptions> m_rpc_callbacks = {};
#endif
};

#endif // Server_Side_RPC_h
