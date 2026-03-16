# Firebolt C++ Transport
An abstraction for transport layer being used across C++ Firebolt Clients.

## Header Support

The transport layer supports custom HTTP headers during WebSocket connection setup and allows retrieval of response headers from the server.

### Sending Custom Headers

You can specify custom headers in the `Firebolt::Config` struct:

```cpp
Firebolt::Config config;
config.wsUrl = "ws://localhost:9002";
config.headers["Authorization"] = "Bearer <token>";
config.headers["X-Custom-Header"] = "value";
```

Pass the config to the gateway's `connect` method:

```cpp
Firebolt::Transport::IGateway& gateway = Firebolt::Transport::GetGatewayInstance();
gateway.connect(config, onConnectionChange);
```

### Retrieving Response Headers

After a successful connection, you can retrieve response headers sent by the server:

```cpp
std::optional<std::string> value = gateway.getResponseHeader("Server");
if (value) {
    std::cout << "Server header: " << *value << std::endl;
}
```

- If the header is present, its value is returned.
- If the header is not present, `std::nullopt` is returned.

#### Thread Safety
Header operations are thread-safe. Access to response headers is protected by a mutex internally.

#### API Reference
- [Config struct](include/firebolt/config.h.in)
- [IGateway interface](include/firebolt/gateway.h)
- [websocketpp connection API - get_response_header](https://docs.websocketpp.org/classwebsocketpp_1_1connection.html#a72e0c94609844078fc611716c39791de)

