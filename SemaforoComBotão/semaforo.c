#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "ws2818b.pio.h"
#include "hardware/pwm.h"

#define LED_RED 13 // Definições do semáforo
#define LED_GREEN 11
#define LED_BLUE 12
#define BOTAO_PEDESTRE_A 5
#define BOTAO_PEDESTRE_B 6

// Definições da matriz NeoPixel
#define PIO_NEO_PIN 7 // Pino de controle da matriz
#define MATRIX_WIDTH 5
#define MATRIX_HEIGHT 5
#define LED_COUNT (MATRIX_WIDTH * MATRIX_HEIGHT)

#define BUZZER_PIN 21 // Pino do buzzer

// definições do tempo de cada fase do semáforo
#define tVerde1 4000
#define tVerde2 6000
#define tAmarelo 3000
#define tVermelho 4000
#define tVermelhoAdicional 6000

typedef enum estado_semaforo
{ // estados do semaforo
    VERDE_OBRIGATORIO,
    VERDE_FLEXIVEL,
    AMARELO,
    VERMELHO
} estado_semaforo;

// Definição de pixel GRB
typedef struct pixel_t
{
    uint8_t G, R, B; // Três valores de 8-bits compõem um pixel.
} pixel_t;

typedef pixel_t npLED_t; // Mudança de nome de "struct pixel_t" para "npLED_t" por clareza.

// variaveis globais
uint red_slice, green_slice, blue_slice;   // Slices PWM para cada LED
uint red_channel, green_channel, blue_channel; // Canais PWM

volatile bool solicitacao_pedestre = false;       // flag para solicitação de pedestre
volatile uint32_t ultimo_acionamento = 0;         // para gerenciar acionamento do botão
const uint32_t debounce_time_ms = 50;             // Tempo de debounce
estado_semaforo estado_atual = VERDE_OBRIGATORIO; // inicializa o ciclo do semaforo
npLED_t leds[LED_COUNT];                          // Buffer de cores dos LEDs da matriz de leds
// Variáveis para uso da máquina PIO.
PIO np_pio;
uint sm;

uint buzzer_slice;     // Slice PWM do buzzer
uint buzzer_channel;   // Canal PWM do buzzer

// Protótipos de funções
void set_pins();
void set_rgb_color(bool red, bool green, bool blue);
void set_rgb_intensity(float red, float green, float blue);
void callback_botao(uint gpio, uint32_t events);
bool check_button_during_delay(uint32_t delay_ms);

void neopixel_init(uint pin);
void npSetLED(const uint index, const uint8_t r, const uint8_t g, const uint8_t b);
void npClear();
void npWrite();
void set_pixel(uint x, uint y, uint8_t r, uint8_t g, uint8_t b);
bool exibir_sinal_pedestre(bool livre);

void buzzer_set_freq(int frequencia);
void buzzer_beep(int frequencia, int duration_ms);

// Símbolos para pedestres (5x5)
const bool sinal_livre[5][5] = { // seta liberando pedestre
    {0, 0, 1, 0, 0},
    {0, 1, 1, 1, 0},
    {1, 0, 1, 0, 1},
    {0, 0, 1, 0, 0},
    {0, 0, 1, 0, 0}};
const bool sinal_stop[5][5] = { // sinal vermelho para pedestre
    {0, 1, 1, 1, 0},
    {1, 0, 0, 0, 1},
    {1, 0, 0, 0, 1},
    {1, 0, 0, 0, 1},
    {0, 1, 1, 1, 0}};
//---------------------------------------- Função principal
int main()
{
    set_pins(); // Inicializa pinos
    // configura interrupção para os botões
    gpio_set_irq_enabled_with_callback(BOTAO_PEDESTRE_A, GPIO_IRQ_EDGE_FALL, true, &callback_botao);
    gpio_set_irq_enabled_with_callback(BOTAO_PEDESTRE_B, GPIO_IRQ_EDGE_FALL, true, &callback_botao);
    // Inicializa matriz de LEDs NeoPixel.
    neopixel_init(PIO_NEO_PIN);
    npClear();
    npWrite();
    sleep_ms(1000); // Espera 1 segundo
    exibir_sinal_pedestre(false); // Passagem proibida
    npWrite();
    while (1)
    { // loop infinito
        switch (estado_atual)
        {
        case VERDE_OBRIGATORIO:
        {                                      // Fase verde obrigatória 
            set_rgb_intensity(0.0f, 0.5f, 0.0f); // Verde a 30% de intensidade

            uint32_t start = to_ms_since_boot(get_absolute_time());
            while (to_ms_since_boot(get_absolute_time()) - start < tVerde1); // 5 segundos de verde

            // Passa para fase verde opcional ou amarelo(caso tenha solicitação)
            if (solicitacao_pedestre)
            {
                estado_atual = AMARELO;
            }
            else
            {
                estado_atual = VERDE_FLEXIVEL;
            }
            break;
        }
        case VERDE_FLEXIVEL:
        {
            // Fase verde que permite interrupções (segundos verificando botões, pedestre pode adiantar amarelo a qualquer momento)
            
            // Se receber solicitação durante fase opcional, muda para amarelo mais cedo
            if (check_button_during_delay(tVerde2))
            {
                estado_atual = AMARELO;
            }
            else
            {
                estado_atual = AMARELO;
            }
            break;
        }
        case AMARELO:
            set_rgb_intensity(0.5f, 0.5f, 0.0f); // Amarelo (vermelho + verde a 30%)
            sleep_ms(tAmarelo);
            estado_atual = VERMELHO;
            break;
        case VERMELHO:
        set_rgb_intensity(0.5f, 0.0f, 0.0f); // Vermelho a 30% de intensidade
            // Três bips curtos (sinal aberto) (600ms)
            buzzer_beep(1000, 200);
            sleep_ms(100);
            buzzer_beep(1000, 200);
            sleep_ms(100);
            buzzer_beep(1000, 200);
            exibir_sinal_pedestre(true); // Pedestre pode atravessar  
            sleep_ms(tVermelho);           
            if (solicitacao_pedestre)
            {
                solicitacao_pedestre = false; // Limpa a solicitação
                sleep_ms(tVermelhoAdicional);               // tempo adicional para pedestre
            }
            exibir_sinal_pedestre(false); // Passagem proibida //fecha antes do semáforo mudar
            buzzer_beep(500, 800); // Um bip longo (sinal fechado) /tempo de seguranca para o sinal de pedestre
            estado_atual = VERDE_OBRIGATORIO;
            break;
        }
    }
}
//------------- Inicializa os pinos do semáforo e dos botões
void set_pins()
{
    // Configuração do LED Vermelho (PWM)
    gpio_set_function(LED_RED, GPIO_FUNC_PWM);
    red_slice = pwm_gpio_to_slice_num(LED_RED);
    red_channel = pwm_gpio_to_channel(LED_RED);
    // Configuração do LED Verde (PWM)
    gpio_set_function(LED_GREEN, GPIO_FUNC_PWM);
    green_slice = pwm_gpio_to_slice_num(LED_GREEN);
    green_channel = pwm_gpio_to_channel(LED_GREEN);
    // Configuração do LED Azul (PWM)
    gpio_set_function(LED_BLUE, GPIO_FUNC_PWM);
    blue_slice = pwm_gpio_to_slice_num(LED_BLUE);
    blue_channel = pwm_gpio_to_channel(LED_BLUE);

    // inicializa os 2 botões e configura como entrada
    gpio_init(BOTAO_PEDESTRE_A);
    gpio_set_dir(BOTAO_PEDESTRE_A, GPIO_IN);
    gpio_init(BOTAO_PEDESTRE_B);
    gpio_set_dir(BOTAO_PEDESTRE_B, GPIO_IN);
    // garante o nivel logico alto enquanto o botão não for pressionado
    gpio_pull_up(BOTAO_PEDESTRE_A);
    gpio_pull_up(BOTAO_PEDESTRE_B);
    // Configura os pinos dos buzzer como PWM
    gpio_set_function(BUZZER_PIN, GPIO_FUNC_PWM); 
    buzzer_slice = pwm_gpio_to_slice_num(BUZZER_PIN);
    buzzer_channel = pwm_gpio_to_channel(BUZZER_PIN);
}
//------------- Acende o LED RGB de acordo com a cor
void set_rgb_color(bool red, bool green, bool blue)
{
    gpio_put(LED_RED, red);
    gpio_put(LED_GREEN, green);
    gpio_put(LED_BLUE, blue);
}

void set_rgb_intensity(float red, float green, float blue) {
    // Define a frequência do PWM (ex: 1kHz)
    // Vermelho (Slice 6)
    pwm_set_clkdiv(red_slice, 125.0f);    // 125 MHz / 125 = 1 MHz
    pwm_set_wrap(red_slice, 999);         // 1 MHz / 1000 = 1 kHz

    // Verde (Slice 5)
    pwm_set_clkdiv(green_slice, 125.0f);
    pwm_set_wrap(green_slice, 999);

    // Azul (Slice 6)
    pwm_set_clkdiv(blue_slice, 125.0f); 
    pwm_set_wrap(blue_slice, 999);

    // Aplica 50% de intensidade (0.5 * 1000 = 500)
    pwm_set_chan_level(red_slice, red_channel, (uint16_t)(red * 1000 * 1.0f));
    pwm_set_chan_level(green_slice, green_channel, (uint16_t)(green * 1000 * 1.0f));
    pwm_set_chan_level(blue_slice, blue_channel, (uint16_t)(blue * 1000 * 1.0f));

    // Habilita os canais PWM
    pwm_set_enabled(red_slice, true);
    pwm_set_enabled(green_slice, true);
    pwm_set_enabled(blue_slice, true);
}

//------------- Função de callback para interrupção dos botões
void callback_botao(uint gpio, uint32_t events)
{
    uint32_t agora = to_ms_since_boot(get_absolute_time());
    // Verifica se o tempo desde o último acionamento é maior que o debounce
    if (agora - ultimo_acionamento > debounce_time_ms)
    {
        solicitacao_pedestre = true; // Marca a solicitação
        ultimo_acionamento = agora;
    }
}
//------------- Verifica se o botão foi pressionado durante um delay e retorna true se foi
bool check_button_during_delay(uint32_t delay_ms)
{
    uint32_t tempo_inicio = to_ms_since_boot(get_absolute_time());
    while (to_ms_since_boot(get_absolute_time()) - tempo_inicio < delay_ms)
    {
        if (solicitacao_pedestre)
        { // botáo foi pressionado
            return true;
        }
        sleep_ms(100); // verifica a cada 100ms
    }
    return false; // botão não foi pressionado
}

//------------- Inicializa a máquina PIO para controle da matriz de LEDs.
void neopixel_init(uint pin)
{
    // Cria programa PIO.
    uint offset = pio_add_program(pio0, &ws2818b_program);
    np_pio = pio0;

    // Toma posse de uma máquina PIO.
    sm = pio_claim_unused_sm(np_pio, false);
    if (sm < 0)
    {
        np_pio = pio1;
        sm = pio_claim_unused_sm(np_pio, true); // Se nenhuma máquina estiver livre, panic!
    }
    // Inicia programa na máquina PIO obtida.
    ws2818b_program_init(np_pio, sm, offset, pin, 800000.f);
    // Limpa buffer de pixels.
    for (uint i = 0; i < LED_COUNT; ++i)
    {
        leds[i].R = 0;
        leds[i].G = 0;
        leds[i].B = 0;
    }
}

//------------- Atribui uma cor RGB a um LED da matriz, reduzindo para 30% de intensidade
void npSetLED(const uint index, const uint8_t r, const uint8_t g, const uint8_t b)
{
    // Aplica redução de 30% usando operações inteiras
    leds[index].R = (r * 3) / 10;
    leds[index].G = (g * 3) / 10;
    leds[index].B = (b * 3) / 10;

}

//------------- Limpa o buffer de pixels.
void npClear()
{
    for (uint i = 0; i < LED_COUNT; ++i)
        npSetLED(i, 0, 0, 0);
}

//------------- Escreve os dados do buffer nos LEDs.
void npWrite()
{
    // Escreve cada dado de 8-bits dos pixels em sequência no buffer da máquina PIO.
    for (uint i = 0; i < LED_COUNT; ++i)
    {
        pio_sm_put_blocking(np_pio, sm, leds[i].G);
        pio_sm_put_blocking(np_pio, sm, leds[i].R);
        pio_sm_put_blocking(np_pio, sm, leds[i].B);
    }
    sleep_us(100); // Espera 100us, sinal de RESET do datasheet.
}

//------------- Define a cor de um led
void set_pixel(uint x, uint y, uint8_t r, uint8_t g, uint8_t b)
{
    if (x >= MATRIX_WIDTH || y >= MATRIX_HEIGHT)
        return;
    int index = y * MATRIX_WIDTH + x;
    npSetLED(index, r, g, b);
}

//------------- exibe o símbolo do pedestre na matriz de leds
bool exibir_sinal_pedestre(bool livre)
{
    const bool(*sinal)[5] = livre ? sinal_livre : sinal_stop;
    uint32_t color = livre ? 0x00FF00 : 0xFF0000; // Verde ou Vermelho (formato 0xRRGGBB)

    // Extrai componentes de cor
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    npClear();
    for (int y = 0; y < 5; y++)
    {
        for (int x = 0; x < 5; x++)
        {
            if (sinal[y][x])
            {
                set_pixel(x, 4 - y, r, g, b);
            } // 4 - y corrige inversão da matriz
        }
    }
    npWrite();
    return true;
}

void buzzer_set_freq(int frequencia) { 
    if (frequencia <= 0) {
        pwm_set_enabled(buzzer_slice, false);
        return;
    }
    uint32_t clock = 125000000; // Clock do sistema (125 MHz)
    uint32_t divider16 = clock / (frequencia * 4096) + 1;
    if (divider16 / 16 == 0) divider16 = 1;
    uint32_t wrap = (clock * 16) / (divider16 * frequencia) - 1;

    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv_int_frac(&config, divider16 / 16, divider16 % 16);
    pwm_config_set_wrap(&config, wrap);
    pwm_init(buzzer_slice, &config, true);
    pwm_set_chan_level(buzzer_slice, buzzer_channel, wrap / 2); // 50% duty cycle
}

void buzzer_beep(int frequencia, int duration_ms) {
    buzzer_set_freq(frequencia);
    pwm_set_enabled(buzzer_slice, true);
    sleep_ms(duration_ms);
    pwm_set_enabled(buzzer_slice, false);
}