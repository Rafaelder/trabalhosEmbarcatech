#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "pico/cyw43_arch.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"

// Configurações de Wi-Fi
#define WIFI_SSID "nome"
#define WIFI_PASSWORD "senha"

// Definição dos pinos
#define BUTTON1_PIN 5    // GPIO5 - Botão A
#define BUTTON2_PIN 6    // GPIO6 - Botão B
#define DEBOUNCE_DELAY_MS 50

// Estrutura para armazenar o estado dos botões e temperatura
typedef struct {
    bool button1_pressed;
    bool button2_pressed;
    float temperature;
    absolute_time_t last_update;
} device_state_t;

// Variáveis globais
device_state_t current_state = {false, false, 0.0};
static bool last_button1_state = false;
static bool last_button2_state = false;
static absolute_time_t last_button1_time = 0;
static absolute_time_t last_button2_time = 0;

// Protótipos de funções
static float read_temperature();
static bool debounce_button(int pin, bool *last_state, absolute_time_t *last_time);
static void update_device_state();
static err_t tcp_client_connected(void *arg, struct tcp_pcb *tpcb, err_t err);
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);

// Função de debounce para os botões
static bool debounce_button(int pin, bool *last_state, absolute_time_t *last_time) {
    absolute_time_t now = get_absolute_time();
    bool current_state = !gpio_get(pin); // Lê o estado invertido (pull-up)
    
    if (current_state != *last_state) {
        *last_time = now;
        *last_state = current_state;
        return current_state;
    }
    
    if (absolute_time_diff_us(*last_time, now) > DEBOUNCE_DELAY_MS * 1000) {
        *last_state = current_state;
    }
    
    return *last_state;
}

// Lê a temperatura do sensor interno
static float read_temperature() {
    adc_select_input(4);
    uint16_t raw_value = adc_read();
    const float conversion_factor = 3.3f / (1 << 12);
    return 27.0f - ((raw_value * conversion_factor - 0.706f) / 0.001721f);
}

// Atualiza o estado dos dispositivos com debounce
static void update_device_state() {
    current_state.button1_pressed = debounce_button(BUTTON1_PIN, &last_button1_state, &last_button1_time);
    current_state.button2_pressed = debounce_button(BUTTON2_PIN, &last_button2_state, &last_button2_time);
    current_state.temperature = read_temperature();
    current_state.last_update = get_absolute_time();
    
    printf("[DEBUG] B1: %d (GPIO: %d), B2: %d (GPIO: %d), Temp: %.2f°C\n",
           current_state.button1_pressed, gpio_get(BUTTON1_PIN),
           current_state.button2_pressed, gpio_get(BUTTON2_PIN),
           current_state.temperature);
}

// Callback para recebimento de dados no servidor
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) {
        tcp_close(tpcb);
        tcp_recv(tpcb, NULL);
        return ERR_OK;
    }

    update_device_state(); // Atualiza estado antes de responder

    char html[2048];
    snprintf(html, sizeof(html),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Connection: close\r\n\r\n"
        "<!DOCTYPE html><html><head>"
        "<meta charset=\"UTF-8\">\n"
        "<title>BitDogLab Monitor</title>"
        "<meta http-equiv='refresh' content='1'>"
        "<style>"
        "body {font-family: Arial, text-align: center; margin-top: 50px;}"
        ".status {padding: 20px; margin: 10px auto; width: 300px; border-radius: 10px;}"
        ".pressed {background: #4CAF50; color: white;}"
        ".released {background: #f44336; color: white;}"
        ".temp {font-size: 24px; margin-top: 20px;}"
        "</style></head>"
        "<body>"
        "<h1>Monitor BitDogLab</h1>"
        "<div class='status %s'>Botão 1: %s</div>"
        "<div class='status %s'>Botão 2: %s</div>"
        "<div class='temp'>Temperatura: %.2f°C</div>"
        "</body></html>",
        current_state.button1_pressed ? "pressed" : "released",
        current_state.button1_pressed ? "Ativo" : "Inativo",
        current_state.button2_pressed ? "pressed" : "released",
        current_state.button2_pressed ? "Ativo" : "Inativo",
        current_state.temperature);

    tcp_write(tpcb, html, strlen(html), TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);
    pbuf_free(p);
    return ERR_OK;
}

// Callback para novas conexões no servidor
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    tcp_recv(newpcb, tcp_server_recv);
    return ERR_OK;
}

int main() {
    stdio_init_all();
    printf("Inicializando sistema...\n");

    // Configuração de GPIO
    gpio_init(BUTTON1_PIN);
    gpio_init(BUTTON2_PIN);
    gpio_set_dir(BUTTON1_PIN, GPIO_IN);
    gpio_set_dir(BUTTON2_PIN, GPIO_IN);
    gpio_pull_up(BUTTON1_PIN);
    gpio_pull_up(BUTTON2_PIN);

    // Configuração do ADC
    adc_init();
    adc_set_temp_sensor_enabled(true);

    // Conexão Wi-Fi
    if (cyw43_arch_init()) {
        printf("Erro na inicialização do Wi-Fi\n");
        return 1;
    }
    cyw43_arch_enable_sta_mode();

    printf("Conectando a %s...\n", WIFI_SSID);
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 20000)) {
        printf("Falha na conexão Wi-Fi\n");
        return 1;
    }
    printf("Conectado! IP: %s\n", ip4addr_ntoa(netif_ip4_addr(netif_default)));

    // Servidor Web
    struct tcp_pcb *pcb = tcp_new();
    tcp_bind(pcb, IP_ANY_TYPE, 80);
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, tcp_server_accept);

    printf("Servidor web iniciado na porta 80\n");

    // Loop principal
    while (true) {
        update_device_state();
        cyw43_arch_poll();
        sleep_ms(100); // Verificação mais rápida para melhor resposta
    }

    cyw43_arch_deinit();
    return 0;
}