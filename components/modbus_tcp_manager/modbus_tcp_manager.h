#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <string>
#include <vector>
#include <memory>

#ifdef USE_ESP32
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include <errno.h>
#endif

namespace esphome {
namespace modbus_tcp {

static const char *const TAG = "modbus_tcp_manager";

enum class ModbusFunction : uint8_t {
    READ_COILS = 0x01,
    READ_DISCRETE_INPUTS = 0x02,  
    READ_HOLDING_REGISTERS = 0x03,
    READ_INPUT_REGISTERS = 0x04,
    WRITE_SINGLE_COIL = 0x05,
    WRITE_SINGLE_REGISTER = 0x06,
    WRITE_MULTIPLE_COILS = 0x0F,
    WRITE_MULTIPLE_REGISTERS = 0x10
};

struct ModbusResponse {
    bool success;
    std::vector<uint16_t> data;
    std::string error_message;
};

class ModbusTCPManager : public Component {
public:
    ModbusTCPManager(const std::string &host, uint16_t port, uint8_t unit_id) 
        : host_(host), port_(port), unit_id_(unit_id), 
          is_connected_(false), last_connection_attempt_(0),
          watchdog_register_(0), watchdog_enabled_(false), 
          watchdog_interval_(10000), last_watchdog_time_(0),
          watchdog_counter_(0), safe_mode_active_(false) {}

    void setup() override {
        ESP_LOGD(TAG, "Setting up Modbus TCP Manager for %s:%d", host_.c_str(), port_);
    }

    // Configuration methods
    void set_watchdog_register(uint16_t reg) { 
        watchdog_register_ = reg; 
        watchdog_enabled_ = true;
        ESP_LOGD(TAG, "Watchdog enabled on register %d", reg);
    }
    
    void set_watchdog_interval(uint32_t interval) { 
        watchdog_interval_ = interval; 
    }
    
    void add_safe_mode_register(uint16_t reg, int16_t value) {
        safe_mode_registers_.push_back({reg, value});
        ESP_LOGD(TAG, "Added safe mode: register %d = %d", reg, value);
    }

    void loop() override {
        uint32_t now = millis();
        
        // Connection health check
        if (now - last_connection_attempt_ > 10000) {
            last_connection_attempt_ = now;
            check_connection();
        }
        
        // Watchdog handling
        if (watchdog_enabled_ && now - last_watchdog_time_ > watchdog_interval_) {
            handle_watchdog();
        }
        
        // Yield regularly for responsiveness during high-frequency operations
        if (now % 50 == 0) {
            yield();
        }
    }

    float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

    // Connection status
    bool is_connected() const { return is_connected_; }

    // Read single register
    ModbusResponse read_register(uint16_t address, ModbusFunction function = ModbusFunction::READ_HOLDING_REGISTERS) {
        return read_registers(address, 1, function);
    }

    // Read multiple registers  
    ModbusResponse read_registers(uint16_t start_address, uint16_t count, ModbusFunction function = ModbusFunction::READ_HOLDING_REGISTERS) {
        ModbusResponse response;
        response.success = false;

        int sock = create_connection();
        if (sock < 0) {
            response.error_message = "Connection failed";
            is_connected_ = false;
            return response;
        }

        // Build request
        std::vector<uint8_t> request = build_read_request(start_address, count, function);
        
        // Send with timeout protection
        if (!send_data(sock, request)) {
            ::close(sock);
            response.error_message = "Send failed";
            is_connected_ = false;
            return response;
        }

        // Receive response
        std::vector<uint8_t> resp_data = receive_data(sock);
        ::close(sock);

        if (resp_data.empty()) {
            response.error_message = "Receive failed";
            is_connected_ = false;
            return response;
        }

        // Parse response
        if (!parse_read_response(resp_data, response, function)) {
            is_connected_ = false;
            return response;
        }

        is_connected_ = true;
        response.success = true;
        return response;
    }

    // Write single register
    bool write_register(uint16_t address, int16_t value) {
        ESP_LOGD(TAG, "Writing value %d to register %d", value, address);
        
        int sock = create_connection();
        if (sock < 0) {
            is_connected_ = false;
            return false;
        }

        std::vector<uint8_t> request = build_write_request(address, value);
        
        bool success = send_data(sock, request);
        if (success) {
            std::vector<uint8_t> response = receive_data(sock);
            success = !response.empty() && response.size() >= 8;
        }
        
        ::close(sock);
        is_connected_ = success;
        
        if (success) {
            ESP_LOGD(TAG, "Successfully wrote value %d to register %d", value, address);
        } else {
            ESP_LOGW(TAG, "Failed to write to register %d", address);
        }
        
        return success;
    }

    // Write multiple registers
    bool write_registers(uint16_t start_address, const std::vector<int16_t>& values) {
        ESP_LOGD(TAG, "Writing %d values starting at register %d", values.size(), start_address);
        
        if (values.empty() || values.size() > 123) {
            ESP_LOGE(TAG, "Invalid value count: %d", values.size());
            return false;
        }

        int sock = create_connection();
        if (sock < 0) {
            is_connected_ = false;
            return false;
        }

        std::vector<uint8_t> request = build_write_multiple_request(start_address, values);
        
        bool success = send_data(sock, request);
        if (success) {
            std::vector<uint8_t> response = receive_data(sock);
            success = !response.empty() && response.size() >= 8;
        }
        
        ::close(sock);
        is_connected_ = success;
        
        if (success) {
            ESP_LOGD(TAG, "Successfully wrote %d values starting at register %d", values.size(), start_address);
        } else {
            ESP_LOGW(TAG, "Failed to write multiple registers starting at %d", start_address);
        }
        
        return success;
    }

private:
    std::string host_;
    uint16_t port_;
    uint8_t unit_id_;
    bool is_connected_;
    uint32_t last_connection_attempt_;
    uint16_t transaction_id_ = 1;
    
    // Watchdog variables
    uint16_t watchdog_register_;
    bool watchdog_enabled_;
    uint32_t watchdog_interval_;
    uint32_t last_watchdog_time_;
    uint16_t watchdog_counter_;
    bool safe_mode_active_;
    
    // Safe mode configuration
    struct SafeModeRegister {
        uint16_t register_addr;
        int16_t value;
    };
    std::vector<SafeModeRegister> safe_mode_registers_;

    void handle_watchdog() {
        last_watchdog_time_ = millis();
        
        if (!is_connected_) {
            ESP_LOGW(TAG, "Watchdog: Connection lost, activating safe mode");
            activate_safe_mode();
            return;
        }
        
        // Write watchdog counter
        watchdog_counter_++;
        bool write_success = write_register(watchdog_register_, watchdog_counter_);
        
        if (write_success) {
            // Read back the watchdog register after a short delay
            delay(100);
            ModbusResponse response = read_register(watchdog_register_);
            
            if (response.success && !response.data.empty()) {
                uint16_t read_value = response.data[0];
                
                // Check if remote device toggled the watchdog (incremented it)
                if (read_value != watchdog_counter_) {
                    ESP_LOGD(TAG, "Watchdog OK: wrote %d, read %d", watchdog_counter_, read_value);
                    watchdog_counter_ = read_value;  // Sync with remote value
                    
                    if (safe_mode_active_) {
                        ESP_LOGI(TAG, "Watchdog restored, deactivating safe mode");
                        safe_mode_active_ = false;
                    }
                } else {
                    ESP_LOGW(TAG, "Watchdog failed: remote device not responding");
                    activate_safe_mode();
                }
            } else {
                ESP_LOGW(TAG, "Watchdog read failed");
                activate_safe_mode();
            }
        } else {
            ESP_LOGW(TAG, "Watchdog write failed");
            activate_safe_mode();
        }
    }
    
    void activate_safe_mode() {
        if (safe_mode_active_) return;  // Already in safe mode
        
        ESP_LOGW(TAG, "Activating safe mode - writing %d safe values", safe_mode_registers_.size());
        safe_mode_active_ = true;
        
        // Write all configured safe mode values
        for (const auto& safe_reg : safe_mode_registers_) {
            write_register(safe_reg.register_addr, safe_reg.value);
            ESP_LOGI(TAG, "Safe mode: Set register %d = %d", safe_reg.register_addr, safe_reg.value);
        }
    }

    int create_connection() {
        int sock = ::socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            ESP_LOGV(TAG, "Could not create socket: %d", errno);
            return -1;
        }

        // Very short timeouts for 1-second polling
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 500000;  // 500ms timeout
        ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        ::setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        struct sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port_);
        
        if (::inet_aton(host_.c_str(), &server_addr.sin_addr) == 0) {
            struct hostent *he = ::gethostbyname(host_.c_str());
            if (he == nullptr) {
                ESP_LOGV(TAG, "DNS resolution failed: %s", host_.c_str());
                ::close(sock);
                return -1;
            }
            memcpy(&server_addr.sin_addr, he->h_addr, sizeof(server_addr.sin_addr));
        }

        if (::connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            ESP_LOGV(TAG, "Connection failed to %s:%d", host_.c_str(), port_);
            ::close(sock);
            return -1;
        }

        ESP_LOGVV(TAG, "Connected to %s:%d", host_.c_str(), port_);
        return sock;
    }

    bool send_data(int sock, const std::vector<uint8_t>& data) {
        int sent = ::send(sock, data.data(), data.size(), 0);
        if (sent != (int)data.size()) {
            ESP_LOGV(TAG, "Send failed: %d/%d bytes", sent, data.size());
            return false;
        }
        return true;
    }

    std::vector<uint8_t> receive_data(int sock) {
        std::vector<uint8_t> data;
        uint8_t buffer[256];
        
        int len = ::recv(sock, buffer, sizeof(buffer), 0);
        if (len > 0) {
            data.assign(buffer, buffer + len);
        }
        
        return data;
    }

    void check_connection() {
        int sock = create_connection();
        if (sock >= 0) {
            ::close(sock);
            if (!is_connected_) {
                ESP_LOGI(TAG, "Modbus connection restored");
                is_connected_ = true;
            }
        } else {
            if (is_connected_) {
                ESP_LOGW(TAG, "Modbus connection lost");
                is_connected_ = false;
            }
        }
    }

    std::vector<uint8_t> build_read_request(uint16_t address, uint16_t count, ModbusFunction function) {
        return {
            static_cast<uint8_t>((transaction_id_ >> 8) & 0xFF),  // Transaction ID High
            static_cast<uint8_t>(transaction_id_++ & 0xFF),      // Transaction ID Low
            0x00, 0x00,  // Protocol ID
            0x00, 0x06,  // Length
            unit_id_,    // Unit ID
            static_cast<uint8_t>(function),  // Function Code
            static_cast<uint8_t>((address >> 8) & 0xFF),   // Address High
            static_cast<uint8_t>(address & 0xFF),          // Address Low
            static_cast<uint8_t>((count >> 8) & 0xFF),     // Count High
            static_cast<uint8_t>(count & 0xFF)             // Count Low
        };
    }

    std::vector<uint8_t> build_write_request(uint16_t address, int16_t value) {
        return {
            static_cast<uint8_t>((transaction_id_ >> 8) & 0xFF),  // Transaction ID High
            static_cast<uint8_t>(transaction_id_++ & 0xFF),      // Transaction ID Low
            0x00, 0x00,  // Protocol ID
            0x00, 0x06,  // Length
            unit_id_,    // Unit ID
            0x06,        // Function Code (Write Single Register)
            static_cast<uint8_t>((address >> 8) & 0xFF),   // Address High
            static_cast<uint8_t>(address & 0xFF),          // Address Low
            static_cast<uint8_t>((value >> 8) & 0xFF),     // Value High
            static_cast<uint8_t>(value & 0xFF)             // Value Low
        };
    }

    std::vector<uint8_t> build_write_multiple_request(uint16_t address, const std::vector<int16_t>& values) {
        uint16_t count = values.size();
        uint8_t byte_count = count * 2;
        
        std::vector<uint8_t> request = {
            static_cast<uint8_t>((transaction_id_ >> 8) & 0xFF),  // Transaction ID High
            static_cast<uint8_t>(transaction_id_++ & 0xFF),      // Transaction ID Low
            0x00, 0x00,  // Protocol ID
            static_cast<uint8_t>(((7 + byte_count) >> 8) & 0xFF),  // Length High
            static_cast<uint8_t>((7 + byte_count) & 0xFF),        // Length Low
            unit_id_,    // Unit ID
            0x10,        // Function Code (Write Multiple Registers)
            static_cast<uint8_t>((address >> 8) & 0xFF),   // Address High
            static_cast<uint8_t>(address & 0xFF),          // Address Low
            static_cast<uint8_t>((count >> 8) & 0xFF),     // Count High
            static_cast<uint8_t>(count & 0xFF),            // Count Low
            byte_count   // Byte Count
        };
        
        // Add values
        for (int16_t value : values) {
            request.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));  // High byte
            request.push_back(static_cast<uint8_t>(value & 0xFF));         // Low byte
        }
        
        return request;
    }

    bool parse_read_response(const std::vector<uint8_t>& data, ModbusResponse& response, ModbusFunction function) {
        if (data.size() < 9) {
            response.error_message = "Response too short";
            return false;
        }

        if (data[7] != static_cast<uint8_t>(function)) {
            response.error_message = "Invalid function code";
            return false;
        }

        uint8_t byte_count = data[8];
        if (data.size() < 9 + byte_count) {
            response.error_message = "Incomplete response";
            return false;
        }

        // Parse register values
        response.data.clear();
        for (int i = 0; i < byte_count; i += 2) {
            uint16_t value = (data[9 + i] << 8) | data[9 + i + 1];
            response.data.push_back(value);
        }

        return true;
    }
};

// Sensor class
class ModbusTCPSensor : public PollingComponent, public sensor::Sensor {
public:
    ModbusTCPSensor(ModbusTCPManager *parent, uint16_t register_address, 
                    uint8_t function_code, float scale, float offset, uint32_t update_interval) 
        : parent_(parent), register_address_(register_address), 
          function_code_(function_code), scale_(scale), offset_(offset) {
        this->set_update_interval(update_interval);
    }

    void setup() override {
        ESP_LOGD(TAG, "Setting up Modbus sensor for register %d", register_address_);
    }

    void update() override {
        if (!parent_->is_connected()) {
            ESP_LOGV(TAG, "Modbus not connected, skipping update for register %d", register_address_);
            return;
        }

        ModbusFunction func = (function_code_ == 4) ? 
            ModbusFunction::READ_INPUT_REGISTERS : 
            ModbusFunction::READ_HOLDING_REGISTERS;

        ModbusResponse response = parent_->read_register(register_address_, func);
        
        if (response.success && !response.data.empty()) {
            // Convert uint16 to int16 for proper signed handling
            int16_t raw_value = static_cast<int16_t>(response.data[0]);
            float scaled_value = (raw_value * scale_) + offset_;
            
            ESP_LOGD(TAG, "Register %d: raw=%d, scaled=%.2f", register_address_, raw_value, scaled_value);
            this->publish_state(scaled_value);
        } else {
            ESP_LOGV(TAG, "Failed to read register %d: %s", register_address_, response.error_message.c_str());
        }
    }

private:
    ModbusTCPManager *parent_;
    uint16_t register_address_;
    uint8_t function_code_;
    float scale_;
    float offset_;
};

// Connection status sensor
class ModbusTCPConnectionSensor : public PollingComponent, public binary_sensor::BinarySensor {
public:
    ModbusTCPConnectionSensor(ModbusTCPManager *parent) : parent_(parent) {
        this->set_update_interval(2000);  // Check every 2 seconds
    }

    void setup() override {
        ESP_LOGD(TAG, "Setting up Modbus connection status sensor");
    }

    void update() override {
        bool connected = parent_->is_connected();
        this->publish_state(connected);
        ESP_LOGV(TAG, "Modbus connection status: %s", connected ? "Connected" : "Disconnected");
    }

private:
    ModbusTCPManager *parent_;
};

}  // namespace modbus_tcp
}  // namespace esphome
