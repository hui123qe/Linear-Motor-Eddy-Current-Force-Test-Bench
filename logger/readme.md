# Logger

The logger is split into small layers:

- `base_logger`: wraps spdlog and implements structured events.
- `logconfig`: parses and validates `config/logging.json`.
- `logcontext`: stores the loaded configuration and supports activation/reload.
- `registry`: builds, gets, and replaces long-lived domain loggers.
- `logrouter`: binds domains to shared sink instances.
- `logsink`: creates console, basic-file, and rotating-file sinks.
- `specificlogsink`: placeholder implementation for sinks such as `remote`.
- `logger`: business-facing macros and `Loggable<Id>`.

## Lifecycle

```cpp
std::string error;
auto& context = logging::LogContext::instance();
if (!context.initialize("config/logging.json", &error)) {
    // Report through stderr or another bootstrap channel.
}

// Reparse the same file and replace active logger backends.
context.reload(&error);
context.shutdown();
```

`load()` changes only the pending configuration. `activate()` creates a
candidate router and logger set, then replaces active backends only after all
sinks and loggers were constructed successfully.

## Domain binding

```cpp
class ModbusService
    : public logging::Loggable<
          logging::Id::Modbus> {
public:
    void connect(int station)
    {
        APP_LOG_INFO("connect station {}", station);
        APP_EVENT_INFO("modbus.connected", "station {} connected", station)
            << logging::Field("station", station);
    }
};
```

Choose a logger explicitly when the domain is selected at the call site:

```cpp
APP_LOG_TO_LOGGER(
    logging::Registry::getLog(
        logging::Id::Alarm),
    error,
    "device communication failed");
```

Edit `loggerids.def` to change compile-time IDs. The identifier spelling must
match the domain name in the JSON configuration.
