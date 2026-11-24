/* ADC por interrupção: potenciômetro (A0) e LDR (A1) */

uint8_t adc_ocupado(void)
{
    return BIT_TST(ADCSRA, ADSC);               /* ADSC fica em 1 enquanto converte */
}

void adc_iniciar_conversao(uint8_t canal)
{
    ADMUX = (1 << REFS0) | (canal & 0x0F);      /* AVcc + canal (0 = pot, 1 = LDR) */
    BIT_SET(ADCSRA, ADSC);                      /* começa a conversão */
}

uint8_t adc_ler_resultado(uint16_t *valor)
{
    uint8_t havia = 0;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {         /* variável de 16 bits: a ISR não pode mudar no meio da leitura */
        if (adc_pronto) {
            *valor = adc_valor;
            adc_pronto = 0;
            havia = 1;
        }
    }
    return havia;
}

uint16_t ler_ms_no_segundo(void)
{
    uint16_t ms;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        ms = ms_no_segundo;
    }
    return ms;
}
