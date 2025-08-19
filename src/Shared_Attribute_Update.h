#ifndef Shared_Attribute_Update_h
#define Shared_Attribute_Update_h

// Local includes.
#include "Shared_Attribute_Callback.h"
#include "IAPI_Implementation.h"

// Log messages.
#if !THINGSBOARD_ENABLE_DYNAMIC
static constexpr char SHARED_ATTRIBUTE_UPDATE_SUBSCRIPTIONS[] = "shared attribute update";
#endif // !THINGSBOARD_ENABLE_DYNAMIC

// --- Topic format (deviceId injected at runtime) ---
// TB default (for reference): "v1/devices/me/attributes"
// static constexpr char ATTRIBUTE_TOPIC_FMT[] = "sensor/%s/attributes";
static constexpr char ATTRIBUTE_TOPIC_FMT[] = "sensor/%s/sattrs";

#if THINGSBOARD_ENABLE_DYNAMIC
template <typename Logger = DefaultLogger>



#else
template <size_t MaxSubscriptions = Default_Subscriptions_Amount,
          size_t MaxAttributes = Default_Attributes_Amount,
          typename Logger = DefaultLogger>
#endif
class Shared_Attribute_Update final : public IAPI_Implementation
{
public:
    Shared_Attribute_Update() = default;

    // ------------ Device identity setters ------------
    void SetDeviceID(char const* id) override { deviceId = id; }
    void SetDeviceAccessToken(char const* tok) override { deviceAccessToken = tok; } // reserved for future use

    /// @brief Subscribes multiple shared attribute callbacks
    template <typename InputIterator>
    bool Shared_Attributes_Subscribe(InputIterator const& first, InputIterator const& last)
    {
        Serial.println("Shared_Attributes_Subscribe 1");

#if !THINGSBOARD_ENABLE_DYNAMIC
        size_t const size = Helper::distance(first, last);
        if (m_shared_attribute_update_callbacks.size() + size > m_shared_attribute_update_callbacks.capacity())
        {
            Logger::printfln(MAX_SUBSCRIPTIONS_EXCEEDED, MAX_SUBSCRIPTIONS_TEMPLATE_NAME,
                             SHARED_ATTRIBUTE_UPDATE_SUBSCRIPTIONS);
            return false;
        }
#endif
        char topic[128];
        if (!Build_Attribute_Topic(topic, sizeof(topic)))
        {
            Logger::printfln(SUBSCRIBE_TOPIC_FAILED, "attributes(topic too long)");
            return false;
        }
        (void)m_subscribe_topic_callback.Call_Callback(topic);

        m_shared_attribute_update_callbacks.insert(m_shared_attribute_update_callbacks.end(), first, last);
        return true;
    }

    /// @brief Subscribe one shared attribute callback
#if THINGSBOARD_ENABLE_DYNAMIC
    bool Shared_Attributes_Subscribe(Shared_Attribute_Callback const& callback)
#else
    bool Shared_Attributes_Subscribe(Shared_Attribute_Callback<MaxAttributes> const& callback)
#endif
    {
        Serial.println("Shared_Attributes_Subscribe 2");

#if !THINGSBOARD_ENABLE_DYNAMIC
        if (m_shared_attribute_update_callbacks.size() + 1U > m_shared_attribute_update_callbacks.capacity())
        {
            Logger::printfln(MAX_SUBSCRIPTIONS_EXCEEDED, MAX_SUBSCRIPTIONS_TEMPLATE_NAME,
                             SHARED_ATTRIBUTE_UPDATE_SUBSCRIPTIONS);
            return false;
        }
#endif
        // Only subscribe the MQTT topic when the first callback is added, to avoid duplicates.
        if (m_shared_attribute_update_callbacks.empty())
        {
            char topic[128];
            if (!Build_Attribute_Topic(topic, sizeof(topic)))
            {
                Logger::printfln(SUBSCRIBE_TOPIC_FAILED, "attributes(topic too long)");
                return false;
            }
            (void)m_subscribe_topic_callback.Call_Callback(topic);
        }

        m_shared_attribute_update_callbacks.push_back(callback);
        return true;
    }

    /// @brief Unsubscribes all shared attribute callbacks and topic
    bool Shared_Attributes_Unsubscribe()
    {
        Serial.println("Shared_Attributes_Unsubscribe");

        m_shared_attribute_update_callbacks.clear();

        char topic[128];
        if (!Build_Attribute_Topic(topic, sizeof(topic)))
        {
            // If the topic can't be built, we've effectively nothing valid to unsubscribe from.
            return false;
        }
        return m_unsubscribe_topic_callback.Call_Callback(topic);
    }

    API_Process_Type Get_Process_Type() const override
    {
        return API_Process_Type::JSON;
    }

    void Process_Response(char const* /*topic*/, uint8_t* /*payload*/, unsigned int /*length*/) override
    {
        // Nothing to do
    }

    void Process_Json_Response(char const* /*topic*/, JsonDocument const& data) override
    {
        Serial.println("Shared_Attributes :: Process_Json_Response 1");

        auto object = data.as<JsonObjectConst>();
        if (object.containsKey(SHARED_RESPONSE_KEY))
        {
            object = object[SHARED_RESPONSE_KEY];
        }

#if THINGSBOARD_ENABLE_STL
#if THINGSBOARD_ENABLE_CXX20
#if THINGSBOARD_ENABLE_DYNAMIC
        auto filtered_shared_attribute_update_callbacks =
            m_shared_attribute_update_callbacks | std::views::filter(
                [&object](Shared_Attribute_Callback const& shared_attribute) {
#else
        auto filtered_shared_attribute_update_callbacks =
            m_shared_attribute_update_callbacks | std::views::filter(
                [&object](Shared_Attribute_Callback<MaxAttributes> const& shared_attribute) {
#endif // THINGSBOARD_ENABLE_DYNAMIC
#else
#if THINGSBOARD_ENABLE_DYNAMIC
        Vector<Shared_Attribute_Callback> filtered_shared_attribute_update_callbacks = {};
        std::copy_if(m_shared_attribute_update_callbacks.begin(), m_shared_attribute_update_callbacks.end(),
                     std::back_inserter(filtered_shared_attribute_update_callbacks),
                     [&object](Shared_Attribute_Callback const& shared_attribute) {
#else
        Array<Shared_Attribute_Callback<MaxAttributes>, MaxSubscriptions> filtered_shared_attribute_update_callbacks =
            {};
        std::copy_if(m_shared_attribute_update_callbacks.begin(), m_shared_attribute_update_callbacks.end(),
                     std::back_inserter(filtered_shared_attribute_update_callbacks),
                     [&object](Shared_Attribute_Callback<MaxAttributes> const& shared_attribute)
                     {
#endif // THINGSBOARD_ENABLE_DYNAMIC
#endif // THINGSBOARD_ENABLE_CXX20
                         return shared_attribute.Get_Attributes().empty() ||
                             std::find_if(shared_attribute.Get_Attributes().begin(),
                                          shared_attribute.Get_Attributes().end(),
                                          [&object](const char* att)
                                          {
                                              return object.containsKey(att);
                                          }) != shared_attribute.Get_Attributes().end();
                     });

        Serial.println("Shared_Attributes :: Process_Json_Response 2");

        for (auto const& shared_attribute : filtered_shared_attribute_update_callbacks)
        {
#else
            for (auto const& shared_attribute : m_shared_attribute_update_callbacks)
            {
                if (shared_attribute.Get_Attributes().empty())
                {
                    // No specific keys were subscribed, call for any update
                    Serial.println("Shared_Attributes :: Process_Json_Response 3 - no attributes");
                    shared_attribute.Call_Callback(object);
                    continue;
                }

                char const* requested_att = nullptr;
                for (auto const& att : shared_attribute.Get_Attributes())
                {
                    if (Helper::stringIsNullorEmpty(att))
                    {
                        Serial.println("Shared_Attributes :: Process_Json_Response 4 - empty attribute");
                        continue;
                    }
                    if (object.containsKey(att))
                    {
                        requested_att = att;
                        break;
                    }
                }

                if (requested_att == nullptr)
                {
                    Serial.println("Shared_Attributes :: Process_Json_Response 5 - no requested attribute");
                    continue;
                }

                Serial.println(
                    "Shared_Attributes :: Process_Json_Response 6 - requested attribute found: object contains " +
                    String(requested_att));
#endif // THINGSBOARD_ENABLE_STL
            shared_attribute.Call_Callback(object);
        }
    }

    bool Compare_Response_Topic(char const* topic) const override
    {
        char built[128];
        if (!Build_Attribute_Topic(built, sizeof(built)))
        {
            return false;
        }
        return strncmp(built, topic, strlen(built) + 1) == 0;
    }

    bool Unsubscribe() override
    {
        return Shared_Attributes_Unsubscribe();
    }

    bool Resubscribe_Topic() override
    {
        Serial.println("Shared_Attributes :: Resubscribe_Topic");

        if (!m_shared_attribute_update_callbacks.empty())
        {
            char topic[128];
            if (!Build_Attribute_Topic(topic, sizeof(topic)))
            {
                Logger::printfln(SUBSCRIBE_TOPIC_FAILED, "attributes(topic too long)");
                return false;
            }
            if (!m_subscribe_topic_callback.Call_Callback(topic))
            {
                Logger::printfln(SUBSCRIBE_TOPIC_FAILED, topic);
                return false;
            }
        }
        return true;
    }

#if !THINGSBOARD_USE_ESP_TIMER
    void loop() override
    {
        /* Nothing to do */
    }
#endif

    void Initialize() override
    {
        /* Nothing to do */
    }

    void Set_Client_Callbacks(Callback<void, IAPI_Implementation&>::function /*subscribe_api_callback*/,
                              Callback<bool, char const* const, JsonDocument const&, size_t const&>::function
                              /*send_json_callback*/,
                              Callback<bool, char const* const, char const* const>::function
                              /*send_json_string_callback*/,
                              const Callback<bool, char const* const>::function subscribe_topic_callback,
                              const Callback<bool, char const* const>::function unsubscribe_topic_callback,
                              Callback<uint16_t>::function /*get_receive_size_callback*/,
                              Callback<uint16_t>::function /*get_send_size_callback*/,
                              Callback<bool, uint16_t, uint16_t>::function /*set_buffer_size_callback*/,
                              Callback<size_t*>::function /*get_request_id_callback*/) override
    {
        m_subscribe_topic_callback.Set_Callback(subscribe_topic_callback);
        m_unsubscribe_topic_callback.Set_Callback(unsubscribe_topic_callback);
    }

private:
    // Build "sensor/<deviceId>/attributes" into out; returns true if it fits
    bool Build_Attribute_Topic(char* out, const size_t outLen) const
    {
        char const* id = deviceId && *deviceId ? deviceId : "unknown";
        const int need = snprintf(nullptr, 0, ATTRIBUTE_TOPIC_FMT, id) + 1; // include NUL
        if (need <= 0 || static_cast<size_t>(need) > outLen)
        {
            if (out && outLen) out[0] = '\0';
            return false;
        }
        (void)snprintf(out, outLen, ATTRIBUTE_TOPIC_FMT, id);
        return true;
    }

    // Stored as non-owning pointers; ensure lifetime managed by caller
    const char* deviceId = nullptr;
    const char* deviceAccessToken = nullptr;

    Callback<bool, char const* const> m_subscribe_topic_callback = {}; // Subscribe mqtt topic client callback
    Callback<bool, char const* const> m_unsubscribe_topic_callback = {}; // Unsubscribe mqtt topic client callback

#if THINGSBOARD_ENABLE_DYNAMIC
    Vector<Shared_Attribute_Callback> m_shared_attribute_update_callbacks = {};
#else
    Array<Shared_Attribute_Callback<MaxAttributes>, MaxSubscriptions> m_shared_attribute_update_callbacks = {};
#endif
};

#endif // Shared_Attribute_Update_h
