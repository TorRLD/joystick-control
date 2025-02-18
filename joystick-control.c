/**
     O joystick fornecerá valores analógicos correspondentes aos eixos X e Y, que serão utilizados para:
    Controlar a intensidade luminosa dos LEDs RGB, onde:
        • O LED Azul terá seu brilho ajustado conforme o valor do eixo Y. Quando o joystick estiver solto
        (posição central - valor 2048), o LED permanecerá apagado. À medida que o joystick for movido para
        cima (valores menores) ou para baixo (valores maiores), o LED aumentará seu brilho gradualmente,
        atingindo a intensidade máxima nos extremos (0 e 4095).
        • O LED Vermelho seguirá o mesmo princípio, mas de acordo com o eixo X. Quando o joystick estiver
        solto (posição central - valor 2048), o LED estará apagado. Movendo o joystick para a esquerda
        (valores menores) ou para a direita (valores maiores), o LED aumentará de brilho, sendo mais intenso
        nos extremos (0 e 4095).
        • Os LEDs serão controlados via PWM para permitir variação suave da intensidade luminosa.
        Exibir no display SSD1306 um quadrado de 8x8 pixels, inicialmente centralizado, que se moverá
        proporcionalmente aos valores capturados pelo joystick.
        Adicionalmente, o botão do joystick terá as seguintes funcionalidades:
        • Alternar o estado do LED Verde a cada acionamento.
        • Modificar a borda do display para indicar quando foi pressionado, alternando entre diferentes estilos
        de borda a cada novo acionamento.

    Finalmente, o botão A terá a seguinte funcionalidade:
        • Ativar ou desativar os LED PWM a cada acionamento.

    Por: Heitor Rodrigues Lemos Dias.
    Fevereiro de 2025

 */

 #include <stdio.h>
 #include "pico/stdlib.h"
 #include "hardware/gpio.h"
 #include "hardware/adc.h"
 #include "hardware/pwm.h"
 #include "hardware/i2c.h"
 #include "include/ssd1306.h"    // Biblioteca do display OLED SSD1306
 #include "include/font.h"       // Fonte utilizada no display
 
 // ----- Definições de pinos -----
 // LEDs RGB
 #define LED_RED_PIN     13    // PWM controlado pelo eixo X
 #define LED_GREEN_PIN   11    // Digital (liga/desliga via botão do joystick)
 #define LED_BLUE_PIN    12    // PWM controlado pelo eixo Y
 
 // Joystick
 #define JOYSTICK_X_PIN  26    // ADC0 (na simulação, usaremos este canal como eixo Y)
 #define JOYSTICK_Y_PIN  27    // ADC1 (na simulação, usaremos este canal como eixo X)
 #define JOYSTICK_BTN_PIN 22   // Botão do joystick (ativo em nível baixo)
 
 // Botão A
 #define BUTTON_A_PIN    5     // Botão A (ativo em nível baixo)
 
 // I2C para o display SSD1306
 #define I2C_SDA         14
 #define I2C_SCL         15
 #define I2C_ADDR        0x3C
 
 // Dimensões do display
 #define SSD1306_WIDTH   128
 #define SSD1306_HEIGHT  64
 
 // Constantes de debouncing (em milissegundos)
 #define DEBOUNCE_DELAY_MS 200
 
 // Definição da deadzone para os ADCs (em unidades de 0 a 4095)
 #define DEADZONE 50
 
 // Fator de suavização para o filtro EMA (0 < ALPHA <= 1)
 // Valores menores deixam a leitura mais estável, porém com resposta mais lenta.
 #define ALPHA 0.1f
 
 // Calibração do eixo Y: se o centro medido for 2100, então Y_OFFSET será 52 para ajustar para 2048.
 #define Y_OFFSET 52
 
 // ----- Variáveis globais voláteis para uso nas interrupções -----
 volatile bool pwm_enabled = true;    // Habilita/desabilita os LEDs controlados por PWM
 volatile bool led_green_on = false;    // Estado do LED verde (liga/desliga)
 volatile uint8_t border_style = 0;     // Alterna o estilo da borda do display
 
 // Variáveis para debouncing dos botões (armazenam o último instante de acionamento)
 volatile absolute_time_t last_js_btn_time;
 volatile absolute_time_t last_btn_a_time;
 
 // Instância do display
 ssd1306_t ssd;
 
 // Variáveis para PWM dos LEDs vermelho e azul
 uint slice_red, slice_blue;
 uint chan_red, chan_blue;
 const uint16_t PWM_WRAP = 255;  // Resolução do PWM
 
 // ====================================================================
 // Função callback unificada para os botões (usada em IRQ)
 // ====================================================================
 void gpio_callback(uint gpio, uint32_t events) {
     absolute_time_t now = get_absolute_time();
 
     // --- Botão do joystick: alterna a borda e o LED verde ---
     if (gpio == JOYSTICK_BTN_PIN) {
         if (absolute_time_diff_us(last_js_btn_time, now) < DEBOUNCE_DELAY_MS * 1000) return;
         last_js_btn_time = now;
 
         led_green_on = !led_green_on;
         border_style = (border_style + 1) % 2;
     }
     // --- Botão A: habilita/desabilita a função dos LEDs PWM ---
     else if (gpio == BUTTON_A_PIN) {
         if (absolute_time_diff_us(last_btn_a_time, now) < DEBOUNCE_DELAY_MS * 1000) return;
         last_btn_a_time = now;
 
         pwm_enabled = !pwm_enabled;
         if (!pwm_enabled) {
             pwm_set_chan_level(slice_red, chan_red, 0);
             pwm_set_chan_level(slice_blue, chan_blue, 0);
         }
     }
 }
 
 // ====================================================================
 // Função principal
 // ====================================================================
 int main() {
     stdio_init_all();
 
     // ----- Configuração dos botões com pull-up e interrupções (ativo em nível baixo) -----
     gpio_init(JOYSTICK_BTN_PIN);
     gpio_set_dir(JOYSTICK_BTN_PIN, GPIO_IN);
     gpio_pull_up(JOYSTICK_BTN_PIN);
     gpio_set_irq_enabled_with_callback(JOYSTICK_BTN_PIN, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
 
     gpio_init(BUTTON_A_PIN);
     gpio_set_dir(BUTTON_A_PIN, GPIO_IN);
     gpio_pull_up(BUTTON_A_PIN);
     gpio_set_irq_enabled(BUTTON_A_PIN, GPIO_IRQ_EDGE_FALL, true);
 
     // ----- Configuração dos LEDs -----
     gpio_init(LED_RED_PIN);
     gpio_set_dir(LED_RED_PIN, GPIO_OUT);
     gpio_init(LED_GREEN_PIN);
     gpio_set_dir(LED_GREEN_PIN, GPIO_OUT);
     gpio_init(LED_BLUE_PIN);
     gpio_set_dir(LED_BLUE_PIN, GPIO_OUT);
 
     // Configuração do PWM para o LED vermelho (GPIO11)
     gpio_set_function(LED_RED_PIN, GPIO_FUNC_PWM);
     slice_red = pwm_gpio_to_slice_num(LED_RED_PIN);
     chan_red = pwm_gpio_to_channel(LED_RED_PIN);
     pwm_set_wrap(slice_red, PWM_WRAP);
     pwm_set_clkdiv(slice_red, 125.f);
     pwm_set_enabled(slice_red, true);
 
     // Configuração do PWM para o LED azul (GPIO13)
     gpio_set_function(LED_BLUE_PIN, GPIO_FUNC_PWM);
     slice_blue = pwm_gpio_to_slice_num(LED_BLUE_PIN);
     chan_blue = pwm_gpio_to_channel(LED_BLUE_PIN);
     pwm_set_wrap(slice_blue, PWM_WRAP);
     pwm_set_clkdiv(slice_blue, 125.f);
     pwm_set_enabled(slice_blue, true);
 
     // ----- Configuração do ADC para o joystick -----
     adc_init();
     // Na simulação:
     // ADC0 (GPIO26) para o eixo Y (vertical) e
     // ADC1 (GPIO27) para o eixo X (horizontal)
     adc_gpio_init(JOYSTICK_X_PIN);  // ADC0
     adc_gpio_init(JOYSTICK_Y_PIN);  // ADC1
 
     // ----- Configuração do I2C para o display -----
     i2c_init(i2c1, 400 * 1000);
     gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
     gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
     gpio_pull_up(I2C_SDA);
     gpio_pull_up(I2C_SCL);
 
     // ----- Inicializa e configura o display SSD1306 -----
     ssd1306_init(&ssd, SSD1306_WIDTH, SSD1306_HEIGHT, false, I2C_ADDR, i2c1);
     ssd1306_config(&ssd);
 
     // Inicializa os instantes de última ativação (debouncing)
     last_js_btn_time = get_absolute_time();
     last_btn_a_time = get_absolute_time();
 
     // Variáveis para armazenar as leituras filtradas (iniciadas no valor central)
     uint16_t filtered_adc_x = 2048;
     uint16_t filtered_adc_y = 2048;
 
     // ----- Loop principal -----
     while (true) {
         // Leitura dos ADCs:
         // ADC0 será usado como eixo Y e ADC1 como eixo X.
         adc_select_input(0);
         uint16_t raw_adc_y = adc_read();  // Eixo Y (vertical)
         adc_select_input(1);
         uint16_t raw_adc_x = adc_read();  // Eixo X (horizontal)
 
         // Filtragem (EMA) para suavizar as variações
         filtered_adc_x = (uint16_t)(ALPHA * raw_adc_x + (1.0f - ALPHA) * filtered_adc_x);
         filtered_adc_y = (uint16_t)(ALPHA * raw_adc_y + (1.0f - ALPHA) * filtered_adc_y);
 
         // Calibração do eixo Y: ajuste para que o centro fique em 2048
         uint16_t calibrated_adc_y;
         if (filtered_adc_y >= Y_OFFSET)
             calibrated_adc_y = filtered_adc_y - Y_OFFSET;
         else
             calibrated_adc_y = 0;
         if (calibrated_adc_y > (4095 - Y_OFFSET))
             calibrated_adc_y = 4095 - Y_OFFSET;
         // Remapeia para a faixa completa 0-4095
         uint16_t new_adc_y = (uint16_t)(((uint32_t)calibrated_adc_y * 4095) / (4095 - Y_OFFSET));
 
         // Aplica a deadzone para o eixo X e para o eixo Y (já calibrado)
         if ((filtered_adc_x > (2048 - DEADZONE)) && (filtered_adc_x < (2048 + DEADZONE)))
             filtered_adc_x = 2048;
         if ((new_adc_y > (2048 - DEADZONE)) && (new_adc_y < (2048 + DEADZONE)))
             new_adc_y = 2048;
 
         // --- Cálculo do brilho dos LEDs ---
         // LED vermelho varia com o eixo X e o azul com o eixo Y.
         uint16_t brightness_red = 0;
         if (filtered_adc_x < 2048)
             brightness_red = ((uint32_t)(2048 - DEADZONE - filtered_adc_x) * PWM_WRAP) / (2048 - DEADZONE);
         else if (filtered_adc_x > (2048 + DEADZONE))
             brightness_red = ((uint32_t)(filtered_adc_x - (2048 + DEADZONE)) * PWM_WRAP) / (2047 - DEADZONE);
         else
             brightness_red = 0;
         
         uint16_t brightness_blue = 0;
         if (new_adc_y < 2048)
             brightness_blue = ((uint32_t)(2048 - DEADZONE - new_adc_y) * PWM_WRAP) / (2048 - DEADZONE);
         else if (new_adc_y > (2048 + DEADZONE))
             brightness_blue = ((uint32_t)(new_adc_y - (2048 + DEADZONE)) * PWM_WRAP) / (2047 - DEADZONE);
         else
             brightness_blue = 0;
 
         // Atualiza os PWM dos LEDs se habilitados (botão A)
         if (pwm_enabled) {
             pwm_set_chan_level(slice_red, chan_red, brightness_red);
             pwm_set_chan_level(slice_blue, chan_blue, brightness_blue);
         }
 
         // Atualiza o LED verde conforme o botão do joystick
         gpio_put(LED_GREEN_PIN, led_green_on);
 
         // --- Atualização do display ---
         // Mapeia:
         // - filtered_adc_x (eixo X) para a coordenada horizontal
         // - new_adc_y (eixo Y calibrado) para a vertical (invertendo para que "para cima" fique para cima)
         uint8_t pos_x = (uint32_t)filtered_adc_x * (SSD1306_WIDTH - 8) / 4095;
         uint8_t pos_y = (uint32_t)(4095 - new_adc_y) * (SSD1306_HEIGHT - 8) / 4095;
 
         ssd1306_fill(&ssd, false);
 
         // Desenha a borda conforme o estilo selecionado
         if (border_style == 0) {
             ssd1306_rect(&ssd, 0, 0, SSD1306_WIDTH, SSD1306_HEIGHT, true, false);
         } else if (border_style == 1) {
             ssd1306_rect(&ssd, 0, 0, SSD1306_WIDTH, SSD1306_HEIGHT, true, false);
             ssd1306_rect(&ssd, 2, 2, SSD1306_WIDTH - 4, SSD1306_HEIGHT - 4, true, false);
         }
 
         // Desenha o quadrado móvel representando a posição do joystick
         ssd1306_rect(&ssd, pos_y, pos_x, 8, 8, true, true);
 
         // Exibe valores e status no display (opcional)
         char buf[32];
         sprintf(buf, "X: %d", filtered_adc_x);
         ssd1306_draw_string(&ssd, buf, 0, 0);
         sprintf(buf, "Y: %d", new_adc_y);
         ssd1306_draw_string(&ssd, buf, 0, 8);
         sprintf(buf, "PWM: %s", pwm_enabled ? "ON" : "OFF");
         ssd1306_draw_string(&ssd, buf, 0, 16);
 
         ssd1306_send_data(&ssd);
         sleep_ms(50);
     }
 
     return 0;
 }
 