/* BÔNUS: "apague as luzes", 5 s com o LDR no escuro = +60 s */

void acrescimos_desenhar(void)
{
    mostrar_tempo();
    lcd_1.setCursor(5, 1);
    lcd_1.print(" L:");
    if (ldr_valor < 1000) lcd_1.print('0');
    if (ldr_valor < 100)  lcd_1.print('0');
    if (ldr_valor < 10)   lcd_1.print('0');
    lcd_1.print(ldr_valor);
    lcd_1.print("  ");
    lcd_1.print((uint8_t)(acr_escuro_ms / 1000));
    lcd_1.print('s');
}

void acrescimos_entrar(void)
{
    estado = JOGO_ACRESCIMOS;
    fase1_leds_apagar();

    ldr_valor = 1023;
    ldr_escuro = 0;
    acr_adc_div = 0;
    acr_tela_div = 0;
    acr_escuro_ms = 0;                          /* acendeu: zera a contagem */
    acr_janela_s = ACR_JANELA_S;

    lcd_linha0("APAGUE AS LUZES");
    acrescimos_desenhar();
}

void acrescimos_sair(uint8_t ganhou)
{
    fase2_entrar();                             /* vai pra fase 2 de qualquer jeito */
    if (ganhou) {
        tempo_restante_s += ACR_BONUS_S;        /* +60 s */
        mostrar_linha1_tempo_erros();
        melodia_tocar(MELODIA_BONUS, TAMANHO(MELODIA_BONUS));
        lcd_linha0("BONUS!  +60s");
        uart_enviar_linha("EVENTO BONUS +60s");
    } else {
        lcd_linha0("SEM BONUS");
        uart_enviar_linha("EVENTO SEM BONUS");
    }
    mensagem_ms = MENSAGEM_MS;
}

void acrescimos_atualizar(void)         /* chamada a cada 1 ms */
{
    uint16_t valor;
    if (adc_ler_resultado(&valor)) ldr_valor = valor; /* pega a leitura nova do LDR */

    acr_adc_div++;
    if (acr_adc_div >= LDR_PERIODO_MS && !adc_ocupado()) {
        acr_adc_div = 0;
        adc_iniciar_conversao(LDR_CANAL);       /* pede outra leitura */
    }

    if (ldr_escuro) {                           /* histerese: entra abaixo de 40, sai acima de 60 */
        if (ldr_valor > LDR_ESCURO_SAI) ldr_escuro = 0;
    } else {
        if (ldr_valor < LDR_ESCURO_ENTRA) ldr_escuro = 1;
    }

    if (ldr_escuro) {
        acr_escuro_ms++;                        /* mais 1 ms no escuro */
        if (acr_escuro_ms >= ACR_ESCURO_MS) {
            acrescimos_sair(1);                 /* ficou 5 s no escuro: ganhou */
            return;
        }
    } else {
        acr_escuro_ms = 0;                      /* acendeu: zera a contagem */
    }

    acr_tela_div++;
    if (acr_tela_div >= LDR_TELA_MS) {
        acr_tela_div = 0;
        acrescimos_desenhar();
    }
}
