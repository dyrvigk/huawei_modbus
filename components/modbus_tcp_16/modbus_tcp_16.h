#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/log.h"
#include <string>
#include <vector>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

namespace esphome {
namespace modbus_tcp {

static const char *const TAG = "modbus_tcp_16";

class ModbusTCP16Sensor : public PollingComponent, public sensor::Sensor {
public:
    ModbusTCP16Sensor(const std::string &host, uint16_t port, uint8_t function_code, 
                      uint16_t register_address, float scale, uint32_t update_interval) 
        : host_(host), port_(port), function_code_(function_code), 
          register_address_(register_address), scale_(scale) {
        this->set_update_interval(update_interval);
    }

    void setup() override {
        ESP_LOGD(TAG, "Setting up Modbus TCP 16-bit sensor at register %d", register_address_);
    }

    void update() override {
        ESP_LOGV(TAG, "Updating Modbus TCP sensor");
        
        // Create socket
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            ESP_LOGE(TAG, "Could not create socket: %d", errno);
            this->publish_state(NAN);
            return;
        }

        // Set socket timeout
        struct timeval timeout;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        // Resolve hostname
        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port_);
        
        if (inet_aton(host_.c_str(), &server_addr.sin_addr) == 0) {
            struct hostent *he = gethostbyname(host_.c_str());
            if (he == nullptr) {
                ESP_LOGE(TAG, "Could not resolve hostname: %s", host_.c_str());
                close(sock);
                this->publish_state(NAN);
                return;
            }
            memcpy(&server_addr.sin_addr, he->h_addr, sizeof(server_addr.sin_addr));
        }

        // Connect
        if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            ESP_LOGE(TAG, "Could not connect to %s:%d - %d", host_.c_str(), port_, errno);
            close(sock);
            this->publish_state(NAN);
            return;
        }

        ESP_LOGV(TAG, "Connected to %s:%d", host_.c_str(), port_);

        // Build Modbus TCP request
        std::vector<uint8_t> request = build_modbus_request();
        
        // Send request
        if (send(sock, request.data(), request.size(), 0) < 0) {
            ESP_LOGE(TAG, "Could not send data: %d", errno);
            close(sock);
            this->publish_state(NAN);
            return;
        }

        // Read response
        uint8_t response[256];
        int len = recv(sock, response, sizeof(response), 0);
        close(sock);

        if (len < 9) {
            ESP_LOGE(TAG, "Invalid response length: %d", len);
            this->publish_state(NAN);
            return;
        }

        // Parse response
        if (response[7] != function_code_) {
            ESP_LOGE(TAG, "Invalid function code in response: %d", response[7]);
            this->publish_state(NAN);
            return;
        }

        uint8_t byte_count = response[8];
        if (byte_count < 2 || len < 9 + byte_count) {
            ESP_LOGE(TAG, "Invalid byte count in response: %d", byte_count);
            this->publish_state(NAN);
            return;
        }

        // Extract 16-bit value (big endian)
        int16_t raw_value = (response[9] << 8) | response[10];
        float scaled_value = raw_value * scale_;
        
        ESP_LOGD(TAG, "Register %d: raw=%d, scaled=%.2f", register_address_, raw_value, scaled_value);
        
        this->publish_state(scaled_value);
    }

private:
    std::string host_;
    uint16_t port_;
    uint8_t function_code_;
    uint16_t register_address_;
    float scale_;

    std::vector<uint8_t> build_modbus_request() {
        return {
            0x00, 0x01,  // Transaction ID
            0x00, 0x00,  // Protocol ID  
            0x00, 0x06,  // Length
            0x01,        // Unit ID
            function_code_,  // Function Code
            static_cast<uint8_t>((register_address_ >> 8) & 0xFF),  // Start Address High
            static_cast<uint8_t>(register_address_ & 0xFF),         // Start Address Low
            0x00, 0x01   // Quantity (1 register = 16 bits)
        };
    }
};

}  // namespace modbus_tcp
}  // namespace esphome
