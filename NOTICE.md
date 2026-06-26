# ThingsBoard Client SDK — Hubo fork

Fork of https://github.com/thingsboard/thingsboard-client-sdk at **v0.15.0** (MIT).
Modifications vs upstream: custom per-device MQTT topic scheme, `parseRequestId`
fix, OTA restructure, debug logging, and ArduinoJson-v6 alignment.

Consumed transitively by `hubo-device-framework` via a pinned git-URL dependency
(`#v0.15.0-hubo.1`). Public so apps resolve it without auth.
