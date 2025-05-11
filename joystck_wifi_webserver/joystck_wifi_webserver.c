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
#define WIFI_SSID "RODOLFO"
#define WIFI_PASSWORD "elder2021"

// Definição dos pinos do joystick
#define JOYSTICK_Y_PIN 26  // GPIO26 - VRy
#define JOYSTICK_X_PIN 27  // GPIO27 - VRx
#define JOYSTICK_SW_PIN 22 // GPIO22 - Botão SW

// Thresholds para determinar a posição do joystick
#define JOYSTICK_CENTER_MIN 1800
#define JOYSTICK_CENTER_MAX 2200

// Estrutura para armazenar os dados do joystick
typedef struct {
    uint16_t x_raw;
    uint16_t y_raw;
    int x_position;
    int y_position;
    bool button_pressed;
    char direction[10];  // Norte, Sul, Leste, Oeste, etc.
} joystick_data_t;

// Função para ler o joystick e determinar a direção
void read_joystick(joystick_data_t *data) {
    // Leitura do eixo X (VRx - GPIO27)
    adc_select_input(1);  // ADC1 - Eixo X (GPIO27)
    data->x_raw = adc_read();
    
    // Leitura do eixo Y (VRy - GPIO26)
    adc_select_input(0);  // ADC0 - Eixo Y (GPIO26)
    data->y_raw = adc_read();
    
    // Leitura do botão SW
    data->button_pressed = !gpio_get(JOYSTICK_SW_PIN);  // Botão normalmente está em HIGH, LOW quando pressionado
    
    // Convertendo para posições relativas (-100 a 100)
    data->x_position = ((int)data->x_raw - 2048) * 100 / 2048;
    data->y_position = ((int)data->y_raw - 2048) * 100 / 2048;
    
    // Determinar a direção com base nos valores do joystick
    if (data->x_raw < JOYSTICK_CENTER_MIN && data->y_raw < JOYSTICK_CENTER_MIN) {
        strcpy(data->direction, "Sudoeste");
    } else if (data->x_raw < JOYSTICK_CENTER_MIN && data->y_raw > JOYSTICK_CENTER_MAX) {
        strcpy(data->direction, "Noroeste");
    } else if (data->x_raw > JOYSTICK_CENTER_MAX && data->y_raw < JOYSTICK_CENTER_MIN) {
        strcpy(data->direction, "Sudeste");
    } else if (data->x_raw > JOYSTICK_CENTER_MAX && data->y_raw > JOYSTICK_CENTER_MAX) {
        strcpy(data->direction, "Nordeste");
    } else if (data->x_raw < JOYSTICK_CENTER_MIN) {
        strcpy(data->direction, "Oeste");
    } else if (data->x_raw > JOYSTICK_CENTER_MAX) {
        strcpy(data->direction, "Leste");
    } else if (data->y_raw < JOYSTICK_CENTER_MIN) {
        strcpy(data->direction, "Sul");
    } else if (data->y_raw > JOYSTICK_CENTER_MAX) {
        strcpy(data->direction, "Norte");
    } else {
        strcpy(data->direction, "Centro");
    }
}

// Função de callback para processar requisições HTTP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) {
        tcp_close(tpcb);
        tcp_recv(tpcb, NULL);
        return ERR_OK;
    }

    // Leitura do joystick
    joystick_data_t joystick_data;
    read_joystick(&joystick_data);

    // Cria a resposta HTML
    char html[1024];
    snprintf(html, sizeof(html),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html\r\n"
             "\r\n"
             "<!DOCTYPE html>\n"
             "<html>\n"
             "<head>\n"
             "<title>Joystick Monitor</title>\n"
             "<style>\n"
             "body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }\n"
             "h1 { font-size: 64px; margin-bottom: 30px; }\n"
             ".joystick-data { font-size: 64px; margin: 50px auto; padding: 20px; background-color: #f0f0f0; border-radius: 15px; max-width: 800px; }\n"
             ".direction { font-size: 72px; color: #e91e63; font-weight: bold; margin-top: 30px; }\n"
             ".button-status { font-size: 48px; margin-top: 20px; color: #2196F3; }\n"
             "</style>\n"
             "<meta http-equiv=\"refresh\" content=\"1\">\n"
             "</head>\n"
             "<body>\n"
             "<h1>Joystick Monitor</h1>\n"
             "<div class=\"joystick-data\">\n"
             "  <div>posição X: %d       posição Y: %d</div>\n"
             "  <div class=\"direction\">rosa dos ventos: %s</div>\n"
             "</div>\n"
             "</body>\n"
             "</html>\n",
             joystick_data.x_position, joystick_data.y_position, joystick_data.direction);

    tcp_write(tpcb, html, strlen(html), TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);

    pbuf_free(p);

    return ERR_OK;
}

// Função de callback ao aceitar conexões TCP
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    tcp_recv(newpcb, tcp_server_recv);
    return ERR_OK;
}

// Função principal
int main() {
    stdio_init_all();
    
    // Configuração do botão do joystick como entrada com pull-up
    gpio_init(JOYSTICK_SW_PIN);
    gpio_set_dir(JOYSTICK_SW_PIN, GPIO_IN);
    gpio_pull_up(JOYSTICK_SW_PIN);

    // Inicializa o ADC
    adc_init();
    adc_gpio_init(JOYSTICK_X_PIN);  // Configura GPIO para ADC (eixo X - VRx)
    adc_gpio_init(JOYSTICK_Y_PIN);  // Configura GPIO para ADC (eixo Y - VRy)

    // Inicializa Wi-Fi
    while (cyw43_arch_init()) {
        printf("Falha ao inicializar Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }

    cyw43_arch_enable_sta_mode();

    printf("Conectando ao Wi-Fi...\n");
    while (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 20000)) {
        printf("Falha ao conectar ao Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }

    printf("Conectado ao Wi-Fi\n");

    if (netif_default) {
        printf("IP do dispositivo: %s\n", ipaddr_ntoa(&netif_default->ip_addr));
    }

    // Configura o servidor TCP
    struct tcp_pcb *server = tcp_new();
    if (!server) {
        printf("Falha ao criar servidor TCP\n");
        return -1;
    }

    if (tcp_bind(server, IP_ADDR_ANY, 80) != ERR_OK) {
        printf("Falha ao associar servidor TCP à porta 80\n");
        return -1;
    }

    server = tcp_listen(server);
    tcp_accept(server, tcp_server_accept);

    printf("Servidor ouvindo na porta 80\n");

    // Loop principal
    while (true) {
        // Teste de leitura do joystick no console
        joystick_data_t test_data;
        read_joystick(&test_data);
        printf("Joystick - X: %d, Y: %d, Direção: %s, Botão: %s\n", 
               test_data.x_position, test_data.y_position, test_data.direction,
               test_data.button_pressed ? "Pressionado" : "Não pressionado");
        
        cyw43_arch_poll();
        sleep_ms(100);  // Pequeno delay para não sobrecarregar o console
    }

    cyw43_arch_deinit();
    return 0;
}