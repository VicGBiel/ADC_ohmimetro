#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "lib/ws2812.pio.h"
#include "lib/ssd1306.h"
#include "lib/font.h"
#include "pico/bootrom.h"


#define NUM_PIXELS 25 // Número de LEDs na matriz 
#define IS_RGBW false // Define se os LEDs são RGBW ou apenas RGB
#define WS2812_PIN 7 // Pino onde os LEDs WS2812 estão conectados
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C
#define ADC_PIN 28 // GPIO para o voltímetro
#define botaoB 6 // Botão B
#define preto 0x000000  
#define marrom 0x021000  
#define vermelho 0x005000 
#define laranja 0x104000 
#define amarelo 0x203000
#define verde 0x500000 
#define azul 0x000060  
#define violeta 0x005050
#define cinza 0x101010  
#define branco 0x505050  


// Vetor para o display
const char* cores[] = {
  "preto", "marrom", "vermelho", "laranja", "amarelo",
  "verde", "azul", "violeta", "cinza", "branco"
};


// Vetor para o WS2812
const uint32_t cores_rgb[] = {
  preto, marrom, vermelho, laranja, amarelo,
  verde, azul, violeta, cinza, branco
};


const float E24[] = {
  10, 11, 12, 13, 15, 16, 18, 20,
  22, 24, 27, 30, 33, 36, 39, 43,
  47, 51, 56, 62, 68, 75, 82, 91, 100
};


bool led_buffer[NUM_PIXELS]= {}; // Inicializa o buffer de LEDs com zeros
int R_conhecido = 9810;   // Resistor de 10k ohm (9,81k ohm medidos)
float R_x = 0.0;           // Resistor desconhecido
float ADC_VREF = 3.33;     // Tensão de referência do ADC
int ADC_RESOLUTION = 4048; // Resolução do ADC (Medido)
ssd1306_t ssd; 
 

// Protótipos de funções
void gpio_setup(); // Configura os pinos GPIO
void display_setup(); // Configura o display
void WS2812_setup(); // Configura a matriz de LEDS
void gpio_irq_handler(uint gpio, uint32_t events); // Tratamento de interrupções
int corCalc(float resistor, int* d1, int* d2, int* mult, int* corrigido); // Função para calcular a cor do resistor
static inline void put_pixel(uint32_t pixel_grb); // Envia um pixel para o barramento WS2812
void atualizaFita(int d1, int d2, int mult); // Atualiza todos os LEDs 


int main(){
  gpio_setup();
  display_setup();
  WS2812_setup();
  adc_init();
  adc_gpio_init(ADC_PIN); // GPIO 28 como entrada analógica

  float tensao;
  char str_x[10]; // Buffer para armazenar a string
  char str_y[10]; // Buffer para armazenar a string
  char res_com[10]; //buffer para armazenar o valor comercial

  bool cor = true;

  int faixa1, faixa2, mult, R_comercial;

  while (true)
  {
    adc_select_input(2); // Seleciona o ADC para eixo X. O pino 28 como entrada analógica

    float soma = 0.0f;
    for (int i = 0; i < 500; i++){
      soma += adc_read();
      sleep_ms(1);
    }

    float media = soma/500.0f;

    // Fórmula simplificada: R_x = R_conhecido * ADC_encontrado /(ADC_RESOLUTION - adc_encontrado)
    R_x = (R_conhecido * media) / (ADC_RESOLUTION - media);

    // Calcula os valores de cada faixa dk resistor
    corCalc(R_x, &faixa1, &faixa2, &mult, &R_comercial);
    sprintf(str_x, "%1.0f", media); // Converte o inteiro em string
    sprintf(str_y, "%1.0f", R_x);   // Converte o float em string
    sprintf(res_com, "%d", R_comercial);

    // Atualiza as cores da matriz de leds
    atualizaFita(faixa1, faixa2, mult);

    //  Atualiza o conteúdo do display com animações

    ssd1306_fill(&ssd, !cor);                          // Limpa o display
    ssd1306_rect(&ssd, 3, 3, 122, 60, cor, !cor);      // Desenha um retângulo
    ssd1306_line(&ssd, 3, 25, 123, 25, cor);           // Desenha uma linha
    ssd1306_line(&ssd, 3, 37, 123, 37, cor);           // Desenha uma linha
    ssd1306_draw_string(&ssd, "Ohmimetro", 25, 6); // Desenha uma string
    ssd1306_draw_string(&ssd, "Resitencia", 25, 16);  // Desenha uma string
    ssd1306_draw_string(&ssd, str_y, 40, 28);  // Desenha uma string
    ssd1306_draw_string(&ssd, "ADC", 13, 41);          // Desenha uma string
    ssd1306_draw_string(&ssd, "Comercial", 50, 41);    // Desenha uma string
    ssd1306_line(&ssd, 44, 37, 44, 60, cor);           // Desenha uma linha vertical
    ssd1306_draw_string(&ssd, str_x, 10, 52);           // Desenha uma string
    ssd1306_draw_string(&ssd, res_com, 59, 52);          // Desenha uma string
    ssd1306_send_data(&ssd);                           // Atualiza o display

    sleep_ms(2000);

    ssd1306_fill(&ssd, !cor);                          // Limpa o display
    ssd1306_rect(&ssd, 3, 3, 122, 60, cor, !cor);      // Desenha um retângulo
    ssd1306_draw_string(&ssd, "Cores", 35, 6); // Desenha uma string
    ssd1306_draw_string(&ssd, cores[faixa1], 30, 20); // Desenha uma string
    ssd1306_draw_string(&ssd, cores [faixa2], 30, 30);  // Desenha uma string
    ssd1306_draw_string(&ssd, cores[mult], 30, 40);  // Desenha uma string
    ssd1306_send_data(&ssd);                           // Atualiza o display

    sleep_ms(2000);
  }
}

void gpio_setup(){
  // Para ser utilizado o modo BOOTSEL com botão B
  gpio_init(botaoB);
  gpio_set_dir(botaoB, GPIO_IN);
  gpio_pull_up(botaoB);
  gpio_set_irq_enabled_with_callback(botaoB, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
  // Aqui termina o trecho para modo BOOTSEL com botão B
}

void display_setup(){
  // I2C Initialisation. Using it at 400Khz.
  i2c_init(I2C_PORT, 400 * 1000);
  gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);                    // Set the GPIO pin function to I2C
  gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);                    // Set the GPIO pin function to I2C
  gpio_pull_up(I2C_SDA);                                        // Pull up the data line
  gpio_pull_up(I2C_SCL);                                        // Pull up the clock line                                           // Inicializa a estrutura do display
  ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT); // Inicializa o display
  ssd1306_config(&ssd);                                         // Configura o display
  ssd1306_send_data(&ssd);                                      // Envia os dados para o display
  // Limpa o display. O display inicia com todos os pixels apagados.
  ssd1306_fill(&ssd, false);
  ssd1306_send_data(&ssd);
}

void WS2812_setup(){
  PIO pio = pio0;
  int sm = 0;
  uint offset = pio_add_program(pio, &ws2812_program);
  ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, IS_RGBW);
  atualizaFita(0, 0, 0);
}

void gpio_irq_handler(uint gpio, uint32_t events)
{
  reset_usb_boot(0, 0);
}

int corCalc(float resistor, int* d1, int* d2, int* mult, int* corrigido) {

  float base = resistor;
  int multiplicador = 0;

  // Normaliza o valor para a faixa de 10 a 91 (escala E24)
  while (base >= 100) {
      base /= 10.0;
      multiplicador++;
  }

  // Encontra o valor mais próximo da série E24
  float menor_dif = 1e9;
  float mais_proximo = E24[0];
  for (int i = 0; i < 25; i++) {
      float dif = fabs(base - E24[i]);
      if (dif < menor_dif) {
        menor_dif = dif;
        mais_proximo = E24[i];
      }
  }

  int valor_corrigido = (int)(mais_proximo * pow(10, multiplicador));

  // Extrai os dígitos
  if (mais_proximo == 100){
    *d1 = (int)(mais_proximo) / 100;
    *d2 = (int)(mais_proximo) % 100; 
    multiplicador++;
  } else {
    *d1 = (int)(mais_proximo) / 10;
    *d2 = (int)(mais_proximo) % 10;
  }
  
  *mult = multiplicador;
  *corrigido = valor_corrigido;

  return 1;
}

static inline void put_pixel(uint32_t pixel_grb) {
  pio_sm_put_blocking(pio0, 0, pixel_grb << 8u);
}

void atualizaFita(int d1, int d2, int mult) {

  //Acende apenas os pares de LED 13-6, 12-7, 11-8
  for (int i = 0; i < NUM_PIXELS; i++) {
      if (i == 13 || i == 6) {
          put_pixel(cores_rgb[d1]);
      } else if (i == 12 || i == 7) {
          put_pixel(cores_rgb[d2]);
      } else if (i == 11 || i == 8) {
          put_pixel(cores_rgb[mult]);
      } else {
          put_pixel(0); // Apaga os outros LEDs
      }
  }
}
