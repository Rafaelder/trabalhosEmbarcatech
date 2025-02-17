#include "pico/stdlib.h"

#define LED_RED 13
#define LED_GREEN 11
#define LED_BLUE 12
#define BOTAO_PEDESTRE 5

void set_rgb_color(bool red, bool green, bool blue) {
    gpio_put(LED_RED, red);
    gpio_put(LED_GREEN, green);
    gpio_put(LED_BLUE, blue);
}

int main() {
    stdio_init_all();
    
    // Configuração do LED RGB
    gpio_init(LED_RED);
    gpio_init(LED_GREEN);
    gpio_init(LED_BLUE);
    gpio_set_dir(LED_RED, GPIO_OUT);
    gpio_set_dir(LED_GREEN, GPIO_OUT);
    gpio_set_dir(LED_BLUE, GPIO_OUT);

    // Configuração do botão
    gpio_init(BOTAO_PEDESTRE);
    gpio_set_dir(BOTAO_PEDESTRE, GPIO_IN);
    gpio_pull_up(BOTAO_PEDESTRE);

    bool pedestre_solicitado = false;

    while(1) {
        // Estado normal: verde
        set_rgb_color(false, true, false);
        
        // Verifica se o botão foi pressionado
        if(!gpio_get(BOTAO_PEDESTRE)) {
            pedestre_solicitado = true;
        }

        if(pedestre_solicitado) {
            // Amarelo (vermelho + verde)
            set_rgb_color(true, true, false);
            sleep_ms(2000);
            
            // Vermelho
            set_rgb_color(true, false, false);
            sleep_ms(5000);
            
            // Reset do sistema
            pedestre_solicitado = false;
        }     
        sleep_ms(100);
    }
}