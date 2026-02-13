#define F_CPU 16000000 //Frequencia do clock

/*======================================Bibliotecas=========================================*/
#include <stdio.h> //Manipulacao de strings (sprintf)
#include <avr/io.h> //Acesso aos registradores do microcontrolador
#include <util/delay.h> //Funcoes de atraso
#include <avr/interrupt.h> //Manipulacao de interrupcoes
#include "LCD.h" //Funcoes para o controle do display LCD
#include "ADC.h" // Funcoes para leitura do conversor AD

/*=========================================Macros===========================================*/
#define set_bit(y,bit)(y|=(1<<bit)) //Seta um bit especifico (1)
#define clr_bit(y,bit)(y&=~(1<<bit)) //Limpa um bit especifico (0)

/*========================================Buffers===========================================*/
char Intensidade[10]; //Armazena os textos "Int - %d, (i)", "Tempo - %2.d, (i)"
char Modo[10]; //Armazena os textos dos modos "Manual, fade, flash, timer"

/*========================================Variaveis=========================================*/
volatile unsigned char a=1; //Nivel de intensidade da lampada (Utilizada pelo Timer0 - TCNT0)
static unsigned char pia = 0; //Armazena o valor anterior do potenciometro
unsigned char m = 0; //Modo atual de operacao
unsigned char i = 1; //No modo manual (Intensidade), no flash e timer (Tempo)
unsigned char j = 0; //Auxiliar para laco for
unsigned char k = 0; //Auxiliar para laco for
unsigned char pi = 0; //Intensidade lida do potenciometro (Valor atual antes de atualizar a)
unsigned int v = 0; //Valor bruto do ADC (0 ~ 1023) usado para classificar pi

/*==================================Interrupcao do timer 0==================================*/
ISR(TIMER0_OVF_vect) {
    set_bit(PORTD, PIND3); //Aciona um pulso no PD3 (Optoacoplador TRIAC)
    _delay_us(10);
    clr_bit(PORTD, PIND3); //Limpa o PD3 finalizando o pulso

    //Desativa o Timer ate o proximo zero-cross, evitando multiplos disparos por semiciclo.
    TIMSK0 = 0b00000000; 
    TCCR0B = 0b00000000;
}

/*====================================Interrupcao PCINT0====================================*/
ISR(PCINT0_vect);

ISR(PCINT0_vect) { //Interrupcao acionada ha mudanca de nivel nos botoes (PB0, PB1, PB2)
    //Botao PB0
    if (!(PINB & (1 << 0))) { //Teste do PB0 - Pressionar (Para decremento)
        if (m == 0) { //Apenas quando for modo Manual
            //Decrementa intensidade (a) e contador (i)
            i--;
            a--;
            if (i < 1) { //"Reinicia" o decremento caso chegue no valor minimo de i (1 ~ 5)
                i = 5;
            }
            if (a < 1){ //"Reinicia" o decremento caso chegue no valor minimo de a (1 ~ 5)
                a = 5;
            }
        }
        else if (m == 2) { //Apenas quando for modo flash
            i--; //Decrementa o tempo (i)
            if (i < 1) { //"Reinicia" o decremento caso chegue no valor minimo de i (1 ~ 5)
                i = 5;
            }
        } 
        else if (m == 3) { //Apenas quando for modo timer
            i--; //Decrementa o tempo (i)
            if (i < 5) { //"Reinicia" o decremento caso chegue no valor minimo de i (5 ~ 10)
                i = 10;
            }
        }
        _delay_ms(150); //Debounce
    }
    //Botao PB1
    if (!(PINB & (1 << 1))) {//Teste do PB1 - Pressionar (Para incremento)
        if (m == 0) { //Apenas quando for modo manual
            //Incrementa intensidade (a) e contador (i)
            i++;
            a++;
            if (i > 5) { //"Reinicia" o incremento caso chegue no valor maximo de i (1 ~ 5)
                i = 1;
            }
            if (a > 5){ //"Reinicia" o incremento caso chegue no valor maximo de a (1 ~ 5)
                a = 1;
            }
        }
        else if (m == 2) { //Apenas quando for modo flash
            i++; //Incrementa o tempo (i)
            if (i > 5) { //"Reinicia" o incremento caso chegue no valor maximo de i (1 ~ 5)
                i = 1;
            }
        }
        else if (m == 3) { //Apenas quando for modo timer
            i++; //Incrementa o tempo (i)
            if (i > 10) { //"Reinicia" o incremento caso chegue no valor maximo de i (5 ~ 10)
                i = 5;
            }
        }
        _delay_ms(150); //Debounce
    }
    //Botao PB2
    if (!(PINB & (1 << 2))) { //Teste do PB2 - Pressionar (Para incremento)
        //Define os valores de inicializacao apropriados para cada modo
        m++; //Incrementa o modo (m)
        if (m > 3){ //"Reiniciar" o incremento caso chegue no valor maximo de m (0 ~ 3)
            m = 0;
        }
        if (m == 0) { //Apenas quando for modo manual
            i = 1; //Contagem de intensidade comeca do nivel 1
            a = 1; //Intensidade comeca no nivel 1 (Minimo)
        } 
        else if (m == 1) { //Apenas quando for modo fade
            a = 1; //Intensidade comeca no nivel 1 (Minimo)
        }
        else if (m == 2) { //Apenas quando for modo flash
            i = 1; //Contagem de tempo comeca em 1 segundo
        } 
        else if (m == 3) { //Apenas quando for modo timer
            i = 5; //Contagem de tempo comeca em 5 segundos
            a = 5; //Intensidade comeca no nivel 5 (Maximo)

            //Desativa a interrupcao do timer 0 para evitar conflitos com os modos anteriores
            TIMSK0 = 0b00000000;
            TCCR0B = 0b00000000;
        }
        _delay_ms(150);
    }
}
/*====================================Interrupcao PCINT2====================================*/
ISR(PCINT2_vect) { //Interrupcao acionada quando detecta borda do zero-cross (Optoacoplador)
    //Ativa o timer0 para gerar o pulso de controle do LED
    TIMSK0 = 0b00000001;
    TCCR0B = 0b00000101;
    
    if((PIND & (1 << 2))){ //Teste de deteccao do zero-cross
        //Definicao do valor inicial de contagem para o estouro da interrupcao, quanto menor o valor inicial, maior o tempo ate o estourar, portanto menor o pulso e menor o brilho
        if(a == 1){ //Intensidade nivel 1
            TCNT0 = 131; //8ms
             }
        else if(a == 2){ //Intensidade nivel 2
            TCNT0 = 147; //7ms
        }
        else if(a == 3){ //Intensidade nivel 3
            TCNT0 = 163; //6ms
        }
        else if(a == 4){ //Intensidade nivel 4
            TCNT0 = 178; //5ms
        }
        else if(a == 5){ //Intensidade nivel 5
            TCNT0 = 194; //4ms
        }
        else if(a == 6){ //Intensidade nivel 4
            TCNT0 = 178; //5ms
        }
        else if(a == 7){ //Intensidade nivel 3
            TCNT0 = 163; //6ms
        }
        else if(a == 8){ //Intensidade nivel 2
            TCNT0 = 147; //7ms
        }
        else if(a > 8){ //Reinicia a contagem de nivel caso atinja o valor maximo (1 ~ 8)
            a = 1;
        }
        //OBS: Duplicou-se os niveis de intensidade 2, 3 e 4 (6 ~ 8) para que houvesse um loop de intensidade sem interrupcao simplesmente com o incremento da variavel a
    }
}   
/*=====================================Funcao Principal=====================================*/
int main(void) {
    DDRB = 0b00101000; //Definindo quais PINB sao entradas (PB0, PB1 e PB2 sao so botoes, PB6 e PB7 sao as entradas do cristal) ou saidas (PB3 e PB5 sao En e Rs do LCD)
    PORTB = 0b00000111; //Ativando resistor o Pull-Up dos botoes (PB0, PB1 e PB2)
    DDRC = 0b00000000; //Definindo quais PINC sao entradas (PC0 eh o potenciometro)
    PORTC = 0b00000000; //Nao eh necessario nenhum resistor pull-up no PORTC
    DDRD = 0b11111000; //Definindo quais PIND sao entradas (PD2 eh o 4n35, entrada do zero cross) e saidas (PD3 eh o MOC3021, PD4 ~ 7 sao as entradas das portas do LCD)
    PORTD = 0b11111000; //Ativando o resistor
    PCICR = 0b00000111; //Habilitando interrupcoes por mudanca nos PORTS B, C e D
    PCMSK0 = 0b00000111; //Habilitando PB0, PB1 e PB2 como fontes de interrupcao
    PCMSK2 = 0b00000100; //Habilitando PD2 como fonte de interrupcao (Deteccao do zero-cross)

    sei(); //Habilita interrupcoes globais

    //Inicializando os perifericos
    inicializa_ADC(); //Inicializa o conversor AD
    inic_LCD_4bits(); //Inicializa o LCD

/*=======================================Loop Principal=====================================*/
    while (1) { //Loop infinito
        
        v = le_ADC(0); //Le o potenciometro no canal 0 do ADC e armazena o valor em v
        _delay_ms(250); //Debounce
        
        //Traducao dos valores analogicos e conversao em uma escala de 1 a 5 (1023/5 = 204.6)
        if(v < 205) { //Valor analogico menor que 205, potenciometro tem intensidade 1
            pi = 1;
        }
        else if(v < 410) { //Valor analogico entre 205 e 410, potenciometro tem intensidade 2
            pi = 2;
        }
        else if(v < 615) { //Valor analogico entre 410 e 615, potenciometro tem intensidade 3
            pi = 3;
        }
        else if(v < 820) { //Valor analogico entre 615 e 820, potenciometro tem intensidade 4
            pi = 4;
        }
        else { //Valor analogico maior que 820, potenciometro tem intensidade 5
            pi = 5;
        }

/*========================================Switch Case=======================================*/
        switch (m) { //Atualiza os textos dos modos
            case 0:
                sprintf(Modo, "Manual:   "); //Armazena "Manual: " dentro do buffer Modo
                break;
            case 1:
                sprintf(Modo, "Fade      "); //Armazena "Fade" dentro do buffer Modo
                break;
            case 2:
                sprintf(Modo, "Flash:    "); //Armazena "Flash: " dentro do buffer Modo
                break;
            case 3:
                sprintf(Modo, "Timer:    "); //Armazena "Timer: " dentro do buffer Modo
                break; 
        }

        //Escreve o modo atual selecionado na primeira metade do LCD
        cmd_LCD(0x80, 0); 
        escreve_LCD(Modo);

        //Exibicao do LCD conforme o modo
        if (m == 0) { //Apenas quando o modo for manual
            cmd_LCD(0xC0, 0); //Escreve a intensidade atual na segunda metade do LCD
            sprintf(Intensidade, "Int %d      ", a);
            escreve_LCD(Intensidade);
        } 
        else if (m == 1) { //Apenas quando o modo for fade
            cmd_LCD(0xC0, 0);//Limpa qualquer texto na segunda metade do LCD
            sprintf(Intensidade, "          ");
            escreve_LCD(Intensidade);
        }
        
        //Comportamento de cada modo
        if (m == 0) { //Apenas quando o modo for manual
            if (pia != pi) { //Atualiza o brilho pelo potenciometro apenas quando ele for diferente do seu estado anterior
                a = pi; //Valor atual do potenciometro vira intensidade atual
                pia = pi; //Valor atual do potenciometro eh salvo no seu estado anterior
            }
        }
        else if (m == 1) { //Apenas quando o modo for fade
            for(j = 0; j < 8; j++){ //Loop de crescimento do suave da intensidade (1 ~ 8)
                a++;
                _delay_ms(90); //Delay para definir frequencia em que a intensidade muda
                if (m == 2){ //Caso mude o modo, quebra o for evitando que o codigo trave
                    break;
                }
            }
        }
        else if (m == 2) { //Apenas quando o modo for flash
            a = 5; //Define intensidade 5 da lampada (Maximo)
            for(j = 0; j < i; j++) { //Espera i segundos
                for(k = 0; k < 2; k++) { //Atualizacao do display imediata ao clicar o botao
                    cmd_LCD(0xC0, 0);
                    sprintf(Intensidade, "Tempo - %.2d", i);
                    escreve_LCD(Intensidade);
                    _delay_ms(10);
                    if (m == 3) { //Caso mude o modo, quebra o for evitando que o codigo trave
                        break;
                    }
                }
                _delay_ms(1000);
                if (m == 3) { //Caso mude o modo, quebra o for evitando que o codigo trave
                    break;
                }
            }
            
            a = 1; //Define intensidade 1 da lampada (Minimo)
            for(j = 0; j < i; j++) { //Espera i segundos
                for(k = 0; k < 2; k++) { // Atualizacao do display imediata ao clicar o botao
                    cmd_LCD(0xC0, 0);
                    sprintf(Intensidade, "Tempo - %.2d", i);
                    escreve_LCD(Intensidade);
                    _delay_ms(10);
                    if (m == 3) { //Caso mude o modo, quebra o for evitando que o codigo trave
                        break;
                    }
                }
                _delay_ms(1000);
                if (m == 3) { //Caso mude o modo, quebra o for evitando que o codigo trave
                    break;
                }
            }
        }
        else if (m == 3) { //Apenas se o modo for Timer
            a = 5; //Define intensidade 5 da lampada (Maximo)
            for(j = 0; j < i; j++) { //Espera i segundos
                for(k = 0; k < 2; k++) { //Atualizacao do display imediata ao clicar o botao
                    cmd_LCD(0xC0, 0);
                    sprintf(Intensidade, "Tempo - %.2d", i);
                    escreve_LCD(Intensidade);
                    _delay_ms(10);
                    if (!(PINB & (1<<0)) || !(PINB & (1<<1))) { //Quebra o laco for quando clicar os botoes PB0 e PB1 para alterar o tempo e reiniciar a contagem.
                        break;
                    }
                    if (m == 0){ //Caso mude o modo, quebra o for evitando que o codigo trave
                        break;
                    }
                }
                _delay_ms(1000);
                if (!(PINB & (1<<0)) || !(PINB & (1<<1))) { //Quebra o laco for quando clicar os botoes PB0 e PB1 para alterar o tempo e reiniciar a contagem.
                    break;
                }
                if (m == 0){ //Caso mude o modo, quebra o for evitando que o codigo trave
                    break;
                }

            }
            //Apos completar a contagem, retorna ao modo manual com valores resetados
            a = 1;
            i = 1;
            m = 0;
        }
    }
}