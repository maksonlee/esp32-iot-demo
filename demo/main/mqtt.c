#include <nvs_flash.h>
#include <esp_log.h>

#include "mqtt_client.h"
#include <dht.h>

#define DHT11_PIN 23

static const char *TAG = "mqtt";
static bool mqtt_connected = false;

static int alloc_and_read_from_nvs(nvs_handle handle, const char *key, char **value)
{
    size_t required_size = 0;
    int error;
    if ((error = nvs_get_blob(handle, key, NULL, &required_size)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read key %s with error %d size %d\n", key, error, required_size);
        return -1;
    }
    *value = calloc(1, required_size + 1);  /* The extra byte is for the NULL termination */
    if (*value) {
        nvs_get_blob(handle, key, *value, &required_size);
        ESP_LOGI(TAG, "Read key:%s, value:%s\n", key, *value);
        return 0;
    }
    return -1;
}

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            mqtt_connected = true;
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            mqtt_connected = false;
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}

void mqtt_app_start(void)
{
    char *server,*server_cert, *client_cert, *client_key;

    nvs_handle fctry_handle;
    if (nvs_flash_init_partition("fctry") != ESP_OK) {
        ESP_LOGE(TAG, "NVS Flash init failed");
        return;
    }

    if (nvs_open_from_partition("fctry", "mfg_ns",
                                NVS_READONLY, &fctry_handle) != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed");
        return;
    }
    if (alloc_and_read_from_nvs(fctry_handle, "server", &server) != 0) {
        return;
    }
    server[strcspn(server, "\n")] = 0;
    if (alloc_and_read_from_nvs(fctry_handle, "server_cert", &server_cert) != 0) {
        return;
    }
    if (alloc_and_read_from_nvs(fctry_handle, "client_cert", &client_cert) != 0) {
        return;
    }
    if (alloc_and_read_from_nvs(fctry_handle, "client_key", &client_key) != 0) {
        return;
    }
    nvs_close(fctry_handle);

    const esp_mqtt_client_config_t mqtt_cfg = {
        .uri = server,
        .client_cert_pem = client_cert,
        .client_key_pem = client_key,
        .cert_pem = server_cert,
    };

    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);

    char data[80];
    int16_t temperature;
    int16_t humidity;

    while (1) {
    	if(mqtt_connected)
    	{
    		if(mqtt_connected && dht_read_data(DHT_TYPE_DHT11, (gpio_num_t)DHT11_PIN, &humidity, &temperature) == ESP_OK)
    		{
    			printf("Humidify: %d%% Temperature: %dC\n", humidity / 10, temperature / 10);
    			sprintf(data, "{\"temperature\":%f,\"humidity\":%f}", temperature / 10.0, humidity / 10.0);
    			esp_mqtt_client_publish(client, "v1/devices/me/telemetry", data, 0, 0, 0);
    		}
    		else
    		{
    			printf("Could not read data from sensor\n");
    		}
    	}
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
