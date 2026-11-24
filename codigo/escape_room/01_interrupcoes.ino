/* interrupções: as ISRs só levantam flag ou guardam valor, quem faz o trabalho é o loop */

ISR(TIMER1_COMPA_vect)                  /* a cada 1 ms */
{
    ms_no_segundo++;                            /* mais 1 ms */
    if (ms_no_segundo >= 1000) {
        ms_no_segundo = 0;
        flag_1s = 1;                            /* passou 1 s, avisa o loop */
    }
    flag_1ms = 1;                               /* só avisa o loop */
}

ISR(INT0_vect)                          /* START: borda de descida no D2 */
{
    flag_start = 1;                             /* só avisa o loop */
}

ISR(ADC_vect)                           /* fim de uma conversao */
{
    uint8_t baixo = ADCL;                       /* ADCL primeiro: trava o ADCH até ele ser lido */
    uint8_t alto  = ADCH;                       /* só os 2 bits de cima (o resultado tem 10 bits) */
    adc_valor = ((uint16_t)alto << 8) | baixo;  /* junta: alto vai 8 casas pra esquerda */
    adc_pronto = 1;                             /* avisa que tem leitura nova */
}

ISR(USART_RX_vect)                      /* chegou um byte pela serial */
{
    char c = UDR0;                              /* byte que chegou */
    uint8_t prox = (uart_rx_fim + 1) & (RX_TAM - 1); /* próxima posição, voltando pro 0 no fim */
    if (prox != uart_rx_ini) {                  /* se o buffer não tá cheio */
        uart_rx[uart_rx_fim] = c;
        uart_rx_fim = prox;
    }
}

ISR(USART_UDRE_vect)                    /* registrador de envio livre */
{
    if (uart_tx_ini != uart_tx_fim) {
        UDR0 = uart_tx[uart_tx_ini];            /* manda o próximo byte */
        uart_tx_ini = (uart_tx_ini + 1) & (TX_TAM - 1);
    } else {
        BIT_CLR(UCSR0B, UDRIE0);                /* acabou o que mandar: desliga essa interrupção */
    }
}
